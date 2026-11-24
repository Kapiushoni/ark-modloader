# Runs (as root, on boot) after the module is removed in the Magisk app.
# The game's files were never changed; only drop the module's staged copies. ArkMods is kept.
PKG=com.studiowildcard.wardrumstudios.ark
for d in /data/user/*/"$PKG" /data/data/"$PKG"; do
  [ -d "$d" ] || continue
  rm -rf "$d/files/arkml_bundle" "$d/files/arkmods_cache" "$d/code_cache/libarkmodloader.so"
done
