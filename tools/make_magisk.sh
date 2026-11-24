#!/usr/bin/env bash
# Builds dist/ArkModLoader-<version>-magisk.zip: Magisk/KernelSU module (Zygisk) for phones.
set -euo pipefail
cd "$(dirname "$0")/.."
VER=$(grep -oE 'kVersion = "[0-9.]+"' loader/src/main.cpp | grep -oE '[0-9.]+')
IFS=. read -r MA MI PA <<<"$VER"
VCODE=$((MA * 10000 + MI * 100 + PA))
OUT=dist/magisk-$VER
cmake --build loader/build >/dev/null
rm -rf "$OUT" "dist/ArkModLoader-$VER-magisk.zip"
mkdir -p "$OUT/zygisk" "$OUT/loader" "$OUT/mods"
cp -r magisk/META-INF magisk/customize.sh magisk/uninstall.sh magisk/README.txt "$OUT/"
sed -e "s/@VERSION@/$VER/" -e "s/@VERSIONCODE@/$VCODE/" magisk/module.prop > "$OUT/module.prop"
echo "$VER" > "$OUT/VERSION"
cp LICENSE "$OUT/LICENSE.txt"
cp loader/build/zygisk/libarkml_zygisk.so "$OUT/zygisk/arm64-v8a.so"
cp loader/build/libarkmodloader.so "$OUT/loader/"
cp loader/lua/LICENSE "$OUT/loader/LUA_LICENSE.txt"
cp loader/build/mods/libhello_menu.so loader/build/mods/libcheat_menu.so "$OUT/mods/"
for m in lua_example spawner teleport rates; do cp "mods/$m/$m.lua" "$OUT/mods/"; done
# Forward-slash paths and Unix permissions inside the zip (Compress-Archive would use backslashes).
PY=""; for c in python3 python; do "$c" -c "import zipfile" 2>/dev/null && { PY=$c; break; }; done  # skips the Windows Store stub
"$PY" -I tools/zipdir.py "$OUT" "dist/ArkModLoader-$VER-magisk.zip"
unzip -l "dist/ArkModLoader-$VER-magisk.zip" 2>/dev/null || "$PY" -I -c "import zipfile,sys;[print(i.file_size,i.filename) for i in zipfile.ZipFile(sys.argv[1]).infolist()]" "dist/ArkModLoader-$VER-magisk.zip"
