#!/bin/bash
# patch_freedos.sh - rozpakuj FreeDOS i spatchuj FDCONFIG.SYS
#
# Uruchom raz po sklonowaniu repo:
#   bash patch_freedos.sh

set -e
cd "$(dirname "$0")"

ZIP="FD14-FloppyEdition.zip"
IMG="FD14-FloppyEdition/144m/x86BOOT.img"

# Rozpakuj zip jesli katalog jeszcze nie istnieje
if [ ! -f "$IMG" ]; then
    if [ ! -f "$ZIP" ]; then
        echo "ERROR: $ZIP nie istnieje"
        exit 1
    fi
    echo "Rozpakowywanie $ZIP..."
    unzip -q "$ZIP"
fi

echo "Patchowanie $IMG..."

# Wyciagnij FDCONFIG.SYS, zmien timeout menu na 0 (auto-English), wgraj z powrotem
mcopy -i "$IMG" ::/fdconfig.sys /tmp/fdconfig_patch.sys
sed -i 's/MENUDEFAULT=1,[0-9]*/MENUDEFAULT=1,0/' /tmp/fdconfig_patch.sys
mdel  -i "$IMG" ::fdconfig.sys
mcopy -i "$IMG" /tmp/fdconfig_patch.sys ::fdconfig.sys
rm -f /tmp/fdconfig_patch.sys

echo "OK - FreeDOS bedzie startowac bez pytania o jezyk (English domyslnie)"
