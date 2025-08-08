/*
  VitaShell
  Copyright (C) 2015-2018, TheFloW

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <curl/curl.h>
#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <libgen.h>

#include "main.h"
#include "io_process.h"
#include "network_download.h"
#include "package_installer.h"
#include "archive.h"
#include "file.h"
#include "message_dialog.h"
#include "language.h"
#include "utils.h"

#define VITASHELL_USER_AGENT "VitaShell/1.00 libhttp/1.1"

static char *parse_filename(const char *ptr, size_t len)
{
  char *copy;
  char *p;
  char *q;
  char  stop = '\0';

  /* simple implementation of strndup() */
  copy = malloc(len + 1);
  if(!copy)
    return NULL;
  memcpy(copy, ptr, len);
  copy[len] = '\0';

  p = copy;
  if(*p == '\'' || *p == '"') {
    /* store the starting quote */
    stop = *p;
    p++;
  }
  else
    stop = ';';

  /* scan for the end letter and stop there */
  q = strchr(p, stop);
  if(q)
    *q = '\0';

  /* if the filename contains a path, only use filename portion */
  q = strrchr(p, '/');
  if(q) {
    p = q + 1;
    if(!*p) {
      free(copy);
      return NULL;
    }
  }

  /* If the filename contains a backslash, only use filename portion. The idea
     is that even systems that do not handle backslashes as path separators
     probably want the path removed for convenience. */
  q = strrchr(p, '\\');
  if(q) {
    p = q + 1;
    if(!*p) {
      free(copy);
      return NULL;
    }
  }

  /* make sure the filename does not end in \r or \n */
  q = strchr(p, '\r');
  if(q)
    *q = '\0';

  q = strchr(p, '\n');
  if(q)
    *q = '\0';

  if(copy != p)
    memmove(copy, p, strlen(p) + 1);

  return copy;
}


//static char* fname = NULL;

size_t header_cb(char *buffer, size_t size, size_t nitems, void *userdata)
{
    const size_t cb = size * nitems;
    const char *end = (char *)buffer + cb;
    const char *str = buffer;

    if((cb > 20) && curl_strnequal(buffer, "Content-disposition:", 20)) {
        const char *p = buffer + 20;

        for(;;) {
          char *filename;
          size_t len;

          while((p < end) && *p && !isalpha((int)*p))
            p++;
          if(p > end - 9)
            break;

          if(memcmp(p, "filename=", 9)) {
            // no match, find next parameter 
            while((p < end) && *p && (*p != ';'))
              p++;
            if((p < end) && *p)
              continue;
            else
              break;
          }
          p += 9;

          len = cb - (size_t)(p - str);

          filename = parse_filename(p, len);

          if(filename) {
            *(char**)userdata = strdup(filename);
            free(filename);
          }
          break;
        }
    }

    return nitems * size;
}

int getDownloadFileInfo(const char* url, int64_t* size, char** fileName, long* response_code)
{
    CURL *curl;
    CURLcode res;
    static char* fname = NULL;

    curl = curl_easy_init();

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYSTATUS, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

        if (fileName)
        {
            curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
            curl_easy_setopt(curl, CURLOPT_HEADERDATA, &fname);
        }

        res = curl_easy_perform(curl);

        // try to get size
        curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, size);

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, response_code);

        if (fileName)
        {
            // try to get filename
            char *url = NULL;
            curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &url);

            if (fname)
            {
              *fileName = strdup(fname);
              free(fname);
              fname = NULL;
            }
            else if (url)
            {
              *fileName = strdup(basename(url));
            }
        }

        curl_easy_cleanup(curl);
    }

    return 0;
}

static size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream)
{
  FileProcessParam *param = (FileProcessParam*)stream;
  size_t written = sceIoWrite(param->fp, ptr, size*nmemb);

  if (param) {
      if (param->value)
          (*param->value) += written;

      if (param->SetProgress)
          param->SetProgress(param->value ? *param->value : 0, param->max);

      if (param->cancelHandler && param->cancelHandler()) {
        return CURL_WRITEFUNC_ERROR;
      }
  }

  return written;
}

int downloadFile(const char* url, const char* path, FileProcessParam *param)
{
    CURL *curl;
    FILE *fp;
    CURLcode res;

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYSTATUS, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);

        param->fp = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);

        long response_code = 0;
        if (param->fp >= 0)
        {
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, param);
            res = curl_easy_perform(curl);

            sceIoClose(param->fp);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            curl_easy_cleanup(curl);
        }
        if (response_code != 200) return -1;
    }

    return 0;

}

int downloadFileProcess(const char *url, const char *dest, int successStep) {
  SceUID thid = -1;

  // Lock power timers
  powerLock();

  // Set progress to 0%
  sceMsgDialogProgressBarSetValue(SCE_MSG_DIALOG_PROGRESSBAR_TARGET_BAR_DEFAULT, 0);
  sceKernelDelayThread(DIALOG_WAIT); // Needed to see the percentage

  // File size
  int64_t size = 0;
  long code = 0;

  getDownloadFileInfo(url, &size, NULL, &code);
  if (size < 0 || code != 200)
  {
    closeWaitDialog();
    setDialogStep(DIALOG_STEP_CANCELED);
    errorDialog(code);
  }

  // Update thread
  thid = createStartUpdateThread(size, 1);

  // Download
  uint64_t value = 0;
  
  FileProcessParam param;
  param.value = &value;
  param.max = size;
  param.SetProgress = SetProgress;
  param.cancelHandler = cancelHandler;

  int res = downloadFile(url, dest, &param);
  if (res < 0) {
    sceIoRemove(dest);
    closeWaitDialog();
    setDialogStep(DIALOG_STEP_CANCELED);
    errorDialog(res);
    goto EXIT;
  }

  // Set progress to 100%
  sceMsgDialogProgressBarSetValue(SCE_MSG_DIALOG_PROGRESSBAR_TARGET_BAR_DEFAULT, 100);
  sceKernelDelayThread(COUNTUP_WAIT);
  
  // Close
  if (successStep != 0) {
    sceMsgDialogClose();
    setDialogStep(successStep);
  }

EXIT:
  if (thid >= 0)
    sceKernelWaitThreadEnd(thid, NULL, NULL);
  
  // Unlock power timers
  powerUnlock();

  return sceKernelExitDeleteThread(0);
}
