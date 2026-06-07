#!/bin/bash
set -e
cd "$(dirname "$0")"

export WATCOM=/opt/watcom
export PATH=$WATCOM/binl64:$PATH
export INCLUDE="$WATCOM/h/win;$WATCOM/h"

[ -e APP.ICO ] || ln -sf App.ico APP.ICO

wcl -ml -l=windows -zWs -od -q WinMain.c MainWnd.c AboutDlg.c -fe=Win16App.exe

$WATCOM/binl64/wrc -r -bt=windows Resource.rc

$WATCOM/binl64/wrc -bt=windows Resource.res Win16App.exe

echo "Done: Win16App.exe ($(stat -c%s Win16App.exe) bytes)"
