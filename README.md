# ArkModLoader

Загрузчик модов для **ARK: Survival Evolved Mobile 2.0.29** (Android, Unreal Engine 4.17).
Моды можно писать на C++ (`.so`) или на Lua (обычный текстовый файл). Они подключаются и
обновляются на ходу, без перезапуска игры. В игре появляется кнопка **MODS**: в ней моды и их
функции включаются и выключаются.

*English: mod loader for ARK Mobile 2.0.29 with C++ and Lua mods, live reload and an in-game MODS
panel. Install on the MuMu emulator (Windows installer) or on a rooted phone (Magisk/Zygisk module).*

> Только для одиночной игры и неофициальных серверов. Проект не связан со Studio Wildcard.
> Файлы игры в репозитории и релизах не распространяются.

## Возможности
- Моды на **C++** (API в [`loader/include/arkmod.h`](loader/include/arkmod.h)) и на **Lua 5.4**.
- **Живая перезагрузка:** новый, изменённый или удалённый мод применяется через пару секунд.
- **Меню MODS** в игре: включение модов и их переключателей. Выбор запоминается.
- **Настройки** каждого мода в `<мод>.ini`, правки подхватываются сразу.
- Доступ к движку: хуки UFunction (pre/post, с отменой вызова), поиск объектов, чтение и запись
  свойств через рефлексию, вызов функций, отрисовка поверх HUD, касания, консольные команды.
- В комплекте моды **Cheat Menu** (полёт, бессмертие, бесконечные показатели, скорость), **Hello Menu** и пример на Lua.

## Установка

Готовые архивы лежат в [Releases](../../releases/latest).

### Телефон (root) — `ArkModLoader-<версия>-magisk.zip`
Нужен телефон **arm64** с Android 7+ и root: **Magisk с включённым Zygisk** или
**KernelSU / APatch + ZygiskNext**.
1. Magisk → Модули → Установить из хранилища → выберите zip.
2. Перезагрузите телефон, запустите игру и зайдите в одиночную игру. Справа вверху появится кнопка MODS.

Файлы игры модуль не меняет, поэтому удаляется обычной кнопкой в Magisk. Игру нельзя добавлять
в DenyList: в такие приложения модули не загружаются.

### Эмулятор MuMu Player 12 (Windows) — `ArkModLoader-<версия>.zip`
1. В MuMu включите root: Настройки → Другое → Root-права.
2. Установите [Python 3](https://www.python.org/) с галочкой «Add to PATH».
3. Распакуйте архив и запустите `install.bat`. Удаление: `uninstall.bat`.

## Моды
Папка с модами:
`Android/data/com.studiowildcard.wardrumstudios.ark/files/ArkMods/`

| Файл | Что это |
|---|---|
| `*.so`, `*.lua` | моды |
| `<мод>.ini` | настройки мода |
| `mods.ini` | какие моды включены |
| `loader.log` | лог загрузчика |

Как писать свои моды: **[mods/README.md](mods/README.md)**.

## Сборка
Нужны Android NDK r27 и CMake.
```sh
cmake -S loader -B loader/build -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24
cmake --build loader/build
tools/make_dist.sh      # dist/ArkModLoader-<версия>.zip   (установщик для MuMu)
tools/make_magisk.sh    # dist/ArkModLoader-<версия>-magisk.zip
```
Устройство загрузчика и найденные смещения движка описаны в [NOTES.md](NOTES.md).

## Как это работает
- **MuMu:** в `libgnustl_shared.so` игры добавляется зависимость на `libarkmodloader.so` (через LIEF,
  на компьютере пользователя). Оригинал сохраняется и возвращается при удалении.
- **Телефон:** Zygisk-модуль подгружает загрузчик в процесс игры при запуске. Символы `libUE4.so`
  загрузчик находит через `dl_iterate_phdr` и хеш-таблицы ELF, потому что игра живёт в другом
  linker namespace.
- Загрузчик перехватывает `UObject::ProcessEvent`, ждёт инициализации движка и загружает моды.

## Лицензия
[MIT](LICENSE). Lua 5.4.7 — MIT, заголовок Zygisk API — лицензия в начале файла.
