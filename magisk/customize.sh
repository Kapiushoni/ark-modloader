# ArkModLoader — установка Magisk-модуля (выполняется Magisk / KernelSU / APatch).
PKG=com.studiowildcard.wardrumstudios.ark
WANT_VER=152

ui_print "- ArkModLoader $(cat "$MODPATH/VERSION") для ARK Mobile"

[ "$ARCH" = "arm64" ] || abort "! Нужен телефон arm64 (у вас: $ARCH)"
[ "$API" -ge 24 ] 2>/dev/null || abort "! Нужен Android 7.0 или новее"

# Zygisk: встроенный в Magisk или ZygiskNext (KernelSU / APatch).
if [ "$KSU" = "true" ] || [ "$APATCH" = "true" ]; then
  if [ -d /data/adb/modules/zygisksu ] && [ ! -f /data/adb/modules/zygisksu/disable ]; then
    ui_print "- Zygisk: ZygiskNext найден"
  else
    ui_print "! Для KernelSU/APatch нужен модуль ZygiskNext — без него моды не загрузятся"
  fi
else
  Z=$(magisk --sqlite "SELECT value FROM settings WHERE key='zygisk'" 2>/dev/null)
  case "$Z" in
    *value=1*) ui_print "- Zygisk включён" ;;
    *) if [ -d /data/adb/modules/zygisksu ]; then
         ui_print "- Zygisk: ZygiskNext найден"
       else
         ui_print "! Zygisk ВЫКЛЮЧЕН: Magisk -> Настройки -> Zygisk, затем перезагрузка"
       fi ;;
  esac
fi

# Игра и её версия (не обязательно: игру можно поставить и после модуля).
if pm path "$PKG" >/dev/null 2>&1; then
  VER=$(dumpsys package "$PKG" 2>/dev/null | grep -m1 -o 'versionCode=[0-9]*' | cut -d= -f2)
  if [ "$VER" = "$WANT_VER" ]; then
    ui_print "- ARK Mobile 2.0.29 найдена"
  else
    ui_print "! ARK Mobile версии $VER, загрузчик сделан под 2.0.29 ($WANT_VER) — может не работать"
  fi
else
  ui_print "! ARK Mobile пока не установлена — поставьте её, модуль подхватит игру сам"
fi

set_perm_recursive "$MODPATH" 0 0 0755 0644

ui_print "- Перезагрузите телефон и запустите игру."
ui_print "  MODS — пункт под EXIT в главном меню и кнопка сверху в игре."
ui_print "  Моды: Android/data/$PKG/files/ArkMods"
ui_print "  Не добавляйте игру в DenyList Magisk — туда модули не загружаются."
