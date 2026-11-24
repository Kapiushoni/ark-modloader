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
- **Меню MODS** на виджетах UMG в стиле игры: пункт под EXIT в главном меню и кнопка в игре.
  Включение модов и их переключателей, прокрутка пальцем. Выбор запоминается.
- **Окна модов** из виджетов игры: надписи, кнопки, переключатели, числа с −/+ (C++ и Lua).
- **Настройки модов прямо в игре:** окно SETTINGS собирается из `<мод>.ini` автоматически.
- **Дамп SDK** одной кнопкой: все классы, поля (тип, смещение) и функции игры в `ArkMods/sdk/sdk.txt`.
- Интерфейс на **русском и английском** (по языку устройства или `language = ru|en` в `ArkMods/loader.ini`).
- **Настройки** каждого мода в `<мод>.ini`, правки подхватываются сразу.
- Доступ к движку: хуки UFunction (pre/post, с отменой вызова), поиск объектов, чтение и запись
  свойств через рефлексию, вызов функций, отрисовка поверх HUD, касания, консольные команды.
- Моды в комплекте:
  - **Cheat Menu** — полёт, бессмертие, бесконечные показатели, скорость;
  - **Spawner** — динозавры нужного уровня перед персонажем и ресурсы в инвентарь;
  - **Teleport** — три сохраняемые точки и «вверх на 1000», если застрял;
  - **Rates** — множители XP, приручения, сбора, урона, голода/жажды (на лету);
  - **Hello Menu** и **Lua Example** — примеры для своих модов.

## Установка

Готовые архивы лежат в [Releases](../../releases/latest).

### Телефон (root) — `ArkModLoader-<версия>-magisk.zip`
Нужен телефон **arm64** с Android 7+ и root: **Magisk с включённым Zygisk** или
**KernelSU / APatch + ZygiskNext**.
1. Magisk → Модули → Установить из хранилища → выберите zip.
2. Перезагрузите телефон и запустите игру: в главном меню под EXIT появится пункт MODS.

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

Первый свой мод: **[mods/QUICKSTART.md](mods/QUICKSTART.md)** (шаблоны на Lua и C++ в `mods/template/`).
Полная справка по API: **[mods/README.md](mods/README.md)**.

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
То же делает GitHub Actions (`.github/workflows/build.yml`) на каждый push: архивы лежат в артефактах
сборки. Тег `v<версия>` (совпадающий с `kVersion` в `loader/src/main.cpp`) дополнительно публикует релиз.
Устройство загрузчика и найденные смещения движка описаны в [NOTES.md](NOTES.md).

## Как это работает
- **MuMu:** в `libgnustl_shared.so` игры добавляется зависимость на `libarkmodloader.so` (через LIEF,
  на компьютере пользователя). Оригинал сохраняется и возвращается при удалении.
- **Телефон:** Zygisk-модуль подгружает загрузчик в процесс игры при запуске. Символы `libUE4.so`
  загрузчик находит через `dl_iterate_phdr` и хеш-таблицы ELF, потому что игра живёт в другом
  linker namespace.
- Загрузчик перехватывает `UObject::ProcessEvent`, ждёт инициализации движка и загружает моды.

## Лицензия
Copyright (C) 2026 Kapiushoni.

ArkModLoader — свободная программа под лицензией **GNU GPL версии 3 или более поздней**
([LICENSE](LICENSE)): её можно использовать, изменять и распространять, но изменённые версии и
программы на её основе должны распространяться под той же лицензией и вместе с исходным кодом.

Сторонний код в репозитории сохраняет свои лицензии (обе совместимы с GPL):
- `loader/lua/` — Lua 5.4.7, MIT ([loader/lua/LICENSE](loader/lua/LICENSE));
- `magisk/zygisk/zygisk.hpp` — Zygisk API, разрешительная лицензия в начале файла.

Версии до 0.9.0 включительно выпускались под MIT; уже скачанные копии остаются под MIT.
