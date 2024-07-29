cp build/eboot.bin release/eboot.bin
cp build/VitaShell.vpk release/VitaShell.vpk
cp build/VitaShell.vpk_param.sfo release/param.sfo
cp pkg/sce_sys/livearea/contents/bg.png release/bg.png
cp pkg/sce_sys/livearea/contents/startup.png release/startup.png
cp pkg/sce_sys/livearea/contents/template.xml release/template.xml
cp pkg/sce_sys/icon0.png release/icon0.png
echo -n -e \\x00\\x00\\x02\\x05 > release/version.bin
