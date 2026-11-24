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
cp loader/build/zygisk/libarkml_zygisk.so "$OUT/zygisk/arm64-v8a.so"
cp loader/build/libarkmodloader.so "$OUT/loader/"
cp loader/lua/LICENSE "$OUT/loader/LUA_LICENSE.txt"
cp loader/build/mods/libhello_menu.so loader/build/mods/libcheat_menu.so "$OUT/mods/"
cp mods/lua_example/lua_example.lua "$OUT/mods/"
# Forward-slash paths and Unix permissions inside the zip (Compress-Archive would use backslashes).
python -I - "$OUT" "dist/ArkModLoader-$VER-magisk.zip" <<'PY'
import os, sys, zipfile
src, dst = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as z:
    for root, _, files in os.walk(src):
        for f in sorted(files):
            p = os.path.join(root, f)
            arc = os.path.relpath(p, src).replace(os.sep, "/")
            data = open(p, "rb").read()
            if arc.endswith((".sh", ".prop", "update-binary", "updater-script", "VERSION")):
                assert b"\r" not in data, arc + " has CRLF"
            info = zipfile.ZipInfo(arc, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o100755 if arc.endswith((".sh", "update-binary")) else 0o100644) << 16
            z.writestr(info, data)
print(dst)
PY
unzip -l "dist/ArkModLoader-$VER-magisk.zip" 2>/dev/null || python -I -c "import zipfile,sys;[print(i.file_size,i.filename) for i in zipfile.ZipFile(sys.argv[1]).infolist()]" "dist/ArkModLoader-$VER-magisk.zip"
