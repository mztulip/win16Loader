#!/bin/bash
# patch_freedos.sh - patche x86BOOT.img po swiezym pobraniu/rozpakowaniu
#
# Uruchom raz po rozpakowaniu FD14-FloppyEdition.zip:
#   bash patch_freedos.sh

IMG="FD14-FloppyEdition/144m/x86BOOT.img"

if [ ! -f "$IMG" ]; then
    echo "ERROR: $IMG nie istnieje. Rozpakuj najpierw FD14-FloppyEdition.zip"
    exit 1
fi

echo "Patchowanie $IMG..."

# Wyciagnij FDCONFIG.SYS, zmien timeout menu na 0 (auto-English), wgraj z powrotem
mcopy -i "$IMG" ::/fdconfig.sys /tmp/fdconfig_patch.sys
sed -i 's/MENUDEFAULT=1,[0-9]*/MENUDEFAULT=1,0/' /tmp/fdconfig_patch.sys
mdel  -i "$IMG" ::fdconfig.sys
mcopy -i "$IMG" /tmp/fdconfig_patch.sys ::fdconfig.sys
rm -f /tmp/fdconfig_patch.sys

echo "OK - FreeDOS bedzie startowac bez pytania o jezyk (English domyslnie)"
