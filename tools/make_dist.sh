#!/usr/bin/env bash
# Builds dist/ArkModLoader-<version>/ (+ .zip): loader, bundled mods, Windows installer.
set -euo pipefail
cd "$(dirname "$0")/.."
VER=$(grep -oE 'kVersion = "[0-9.]+"' loader/src/main.cpp | grep -oE '[0-9.]+')
OUT=dist/ArkModLoader-$VER
cmake --build loader/build >/dev/null
rm -rf "$OUT" "$OUT.zip"
mkdir -p "$OUT/files" "$OUT/mods" "$OUT/installer"
cp loader/build/libarkmodloader.so "$OUT/files/"
cp loader/build/mods/libhello_menu.so loader/build/mods/libcheat_menu.so "$OUT/mods/"
cp mods/lua_example/lua_example.lua "$OUT/mods/"
cp installer/patch_needed.py "$OUT/installer/"
# PowerShell 5.1 needs a UTF-8 BOM to read Cyrillic; cmd wants CRLF in .bat files.
printf '\xEF\xBB\xBF' > "$OUT/installer/install.ps1"; sed 's/$/\r/' installer/install.ps1 >> "$OUT/installer/install.ps1"
for b in install uninstall; do sed 's/$/\r/' installer/$b.bat > "$OUT/$b.bat"; done
sed 's/$/\r/' installer/README.txt > "$OUT/README.txt"
cp loader/lua/LICENSE "$OUT/files/LUA_LICENSE.txt"
echo "$VER" > "$OUT/VERSION"
(cd dist && powershell -NoProfile -Command "Compress-Archive -Path 'ArkModLoader-$VER' -DestinationPath 'ArkModLoader-$VER.zip' -Force")
echo "built $OUT and $OUT.zip"; ls -la "$OUT" "$OUT"/*/
