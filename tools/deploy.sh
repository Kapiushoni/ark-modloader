#!/usr/bin/env bash
# Deploy loader into the installed ARK Mobile on MuMu (root), restart the game, show loader log.
# usage: tools/deploy.sh [wait_seconds]   |   tools/deploy.sh --restore
set -euo pipefail
export MSYS_NO_PATHCONV=1
cd "$(dirname "$0")/.."

ADB="/c/Program Files/Netease/MuMuPlayer/nx_main/adb.exe"
S=127.0.0.1:16384
PKG=com.studiowildcard.wardrumstudios.ark
"$ADB" connect $S >/dev/null
LIBDIR=$("$ADB" -s $S shell "su -c 'dirname \$(pm path $PKG | head -1 | cut -d: -f2)'" | tr -d '\r')/lib/arm64
sh_su() { "$ADB" -s $S shell "su -c '$1'"; }

install_lib() {  # local_path name
  "$ADB" -s $S push "$1" "/data/local/tmp/$2" >/dev/null
  sh_su "cp /data/local/tmp/$2 $LIBDIR/$2 && chown system:system $LIBDIR/$2 && chmod 755 $LIBDIR/$2 && chcon u:object_r:apk_data_file:s0 $LIBDIR/$2 && rm /data/local/tmp/$2"
}

sh_su "am force-stop $PKG"
if [[ "${1:-}" == "--restore" ]]; then
  install_lib backup/libgnustl_shared.so.orig libgnustl_shared.so
  sh_su "rm -f $LIBDIR/libarkmodloader.so"
  echo "restored original libs"; exit 0
fi

install_lib loader/build/libarkmodloader.so libarkmodloader.so
install_lib work/libgnustl_shared.so libgnustl_shared.so

MODS=/sdcard/Android/data/$PKG/files/ArkMods
"$ADB" -s $S shell mkdir -p $MODS
for f in $("$ADB" -s $S shell ls $MODS | tr -d '\r'); do
  case "$f" in *.so) "$ADB" -s $S shell rm -f "$MODS/$f" ;; esac
done
for m in loader/build/mods/*.so; do [ -e "$m" ] && "$ADB" -s $S push "$m" $MODS/ >/dev/null; done
echo "mods on device: $("$ADB" -s $S shell ls $MODS | tr -d '\r' | tr '\n' ' ')"

"$ADB" -s $S logcat -c
"$ADB" -s $S shell monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
sleep "${1:-45}"
echo "--- pid: $("$ADB" -s $S shell pidof $PKG | tr -d '\r') ---"
"$ADB" -s $S logcat -d -s ArkModLoader:* DEBUG:* AndroidRuntime:E libc:F
