# ARK Mobile mod loader — notes

## Target
- `com.studiowildcard.wardrumstudios.ark` 2.0.29 (versionCode 152), UE **4.17** (PhysX paths `++UE4+Release-4.17`).
- `libUE4.so` arm64, 115 MB, **272k exported dynamic symbols** incl. game module (`AShooter*`, `APrimal*`).
- Runs on MuMu 12 (Android 12) through Intel `libhoudini` (ARM→x86 translation).

## Key symbols (file offsets)
| symbol | offset |
|---|---|
| `UObject::ProcessEvent` `_ZN7UObject12ProcessEventEP9UFunctionPv` | 0x3648550 |
| `AActor::ProcessEvent` | 0x4667b60 |
| `FName::GetNames()` → `TNameEntryArray&` | 0x34f38e8 |
| `GUObjectArray` | 0x74f21d8 |
| `GWorld` / `GEngine` | 0x762b238 / 0x7628518 |
| `StaticFindObject(UClass*,UObject*,const wchar_t*,bool)` | 0x365c628 |
| `StaticLoadObject(...)` | 0x365e864 |

## Layouts (verified at runtime)
- `TCHAR` = `wchar_t` (4 bytes).
- UObjectBase: vtbl 0x0, flags 0x8, InternalIndex 0xC, Class 0x10, FName 0x18 (8 bytes), Outer 0x20.
- FUObjectArray.ObjObjects at +0x10 → {Objects*, Max, Num}; FUObjectItem = 0x18.
- TNameEntryArray: 128 chunk ptrs × 16384, then NumElements, NumChunks (0x408 total).
- FNameEntry: Index (bit0 = wide) @0, HashNext @8, name @0x10.

## Injection
- Root on MuMu: patched `libgnustl_shared.so` (DT_NEEDED `libarkmodloader.so`, via LIEF) copied
  into `/data/app/.../lib/arm64/` with `system:system 755 apk_data_file`. No APK re-sign needed.
- `tools/deploy.sh [secs]` deploys + restarts + prints log; `tools/deploy.sh --restore` reverts.
- Original lib backup: `backup/libgnustl_shared.so.orig`.

## Results (2026-11-23)
- Loader loads, finds libUE4, names + objects resolve correctly (`Package /Script/CoreUObject` …).
- **Own arm64 inline hook on ProcessEvent works under houdini** (thousands of calls, function names logged).
- Dobby master fails to build with NDK r27 (asm + OSMemory errors) — not used.
- Google Play Games sign-in on launch pushes the game to background and Android kills it; not a loader crash.
  Fixed by signing in to Google in MuMu.

## v0.1.0 (2026-11-23) — mod system
- Reflection offsets (runtime-verified): UField::Next 0x28; UStruct Super 0x30 / Children 0x38 /
  PropertiesSize 0x40; UProperty ArrayDim 0x30 / ElementSize 0x34 / Flags 0x38 / Offset 0x50.
- Public C API `loader/include/arkmod.h` (v1): hooks by function path (pre/post, can block),
  per-frame ticks, game-thread tasks, find/iterate objects, property offsets, call UFunctions,
  FText helpers, console command, raw inline hooks.
- Mods: `*.so` in `/sdcard/Android/data/<pkg>/files/ArkMods/`, copied to app-private
  `files/arkmods_cache/` before dlopen. Log also written to `ArkMods/loader.log`.
- Example `mods/hello_menu`: renames main-menu "PLAY ARK" → "PLAY ARK (MODDED)" — verified on screen.

## v0.2.0 (2026-11-23) — hot-load + in-game verification
- Loader rescans ArkMods every 3 s and loads new `.so` files (by file name; to update a mod, use a new name or restart).
- Verified in single-player M_TheIsland: local PC `PlayerControllerBlueprint_C`, pawn `PlayerPawnTest_Female_C`,
  position via RootComponent.RelativeLocation tracks movement; `exec_command("stat fps")` works (overlay shows ~15 FPS on MuMu).
- Offsets: PlayerController.Player 1584, Controller.Pawn 1496.

## Cheat menu mod (2026-11-23)
- `IsAllowedToCheat` = 0 → ShooterCheatManager Fly/God etc. do nothing (console `fly`/`cheat fly` too).
- Works instead: `CharacterMovementComponent.SetMovementMode(5)` (fly), status component
  `CurrentStatusValues`/`MaxStatusValues` (float[12]: 0 health, 1 stamina, 2 torpor, 3 oxygen, 4 food, 5 water),
  `MaxWalkSpeed`.
- ARK HUD never fires `HUD.ReceiveDrawHUD`; menu draws after inline-hooked `AShooterHUD::DrawHUD`
  using `HUD.DrawRect/DrawText` via ProcessEvent (Canvas valid there).
- `PlayerController.GetInputTouchState` never sees touches (mobile UI eats them) → input via inline hooks on
  `FSlateApplication::OnTouchStarted/Moved/Ended`; touches on the menu are consumed.
- Hot-reloading a mod that inline-hooks the same function again fails (prologue already patched) → restart.
- CMake leaves stale `.so` in `loader/build/mods` — delete removed mods there or deploy.sh re-pushes them.
- Flight v3 (current, user's choice): FLY toggle in menu + on-screen UP/DOWN buttons
  (tap = keep moving until tapped again, press >0.35 s = move while held; hover zeroes small Z drift).
  The v2 Minecraft-style controls below were tried and reverted at the user's request.
- Flight v2 (Minecraft-style, reverted), hooks: `APrimalCharacter::OnStartJump` (double jump toggles fly; in flight
  jump = rise toggle), `OnRunToggle`/`OnStartRunning` (in flight = fast toggle),
  `AShooterCharacter::OnPressCrouchProneToggle`/`Crouch(bool)` (in flight = descend toggle).
  NOT hookable: `OnPressCrouch`/`OnPressProne` are 4-byte stubs (inline hook writes 16 bytes!).
- Raw bits seen in disasm: APrimalCharacter running flag = `*(u32*)(this+0xb90) & 4`; jump pressed = `*(u16*)(this+0x6e4) & 4`.

## Known limitations / next
- `inline_hook` only handles prologues without PC-relative instructions (no relocation yet).
- `make_text` leaks one FText ref per call (negligible).
- No mod unload/reload.

## v0.4.0 (2026-11-23) — settings, live reload, in-game mod list, git
- API v2 (appended, v1 mods still work): config_string/int/float (ArkMods/<id>.ini, missing keys
  auto-appended, re-read on change), ui_toggle, on_draw + draw_rect/draw_text, on_touch; optional
  `ArkMod_Shutdown`.
- MODS panel moved into the loader (`ui.cpp`: AShooterHUD::DrawHUD + Slate touch hooks); lists mods
  with ON/OFF (persisted in ArkMods/mods.ini) and each mod's toggles.
- Live reload (`mods.cpp`): scan every 3 s; new → load, changed → Shutdown + unload + load fresh copy,
  removed → unload. Unload drops everything the mod registered (owner = module base via dladdr of the
  callback / return address). No dlclose (code may still be on a stack); mods that used inline_hook
  can't be unloaded live → "restart the game to apply".
- Bug found in testing: re-adding a deleted mod reused cache name `<id>.1.so` while the old copy was
  still mapped → overwrote live code → SIGSEGV. Fixed: process-wide copy counter + cache wiped at start.
- cheat_menu and hello_menu ported to v2 (no own inline hooks → fully hot-reloadable).
- Verified on MuMu (main menu): load, config edit live, reload, delete/unload, re-add ×2, no crash.
  In-game panel verified on M_TheIsland: mod list, toggles, FLY + UP/DOWN, disable (unload) and re-enable Cheat Menu.
- Git: repo initialised; `main` = 0.2.0 baseline, work on branch `v0.4`. Game binaries / re dumps ignored.
- Attempted relocating hook engine (v0.3.0) was abandoned (see conversation); hook.cpp is the 0.2.0 one.

## v0.5.0 (2026-11-23) — Lua mods + scrollable MODS panel
- Lua 5.4.7 vendored in `loader/lua` (MIT, sha256 verified), one lua_State per `ArkMods/*.lua`.
- `lua_runtime.cpp`: `ark` API (log/config/toggle/on_tick/on_draw/on_touch/hook/draw_*/find/objects/
  player/controller/world/time), reflection-backed UObject / UStruct userdata (__index/__newindex by
  property name, bound UFunction calls with named or positional params), validity check via
  GUObjectArray[idx]==ptr (never dereferences a stale pointer), errors via pcall + traceback (rate-limited).
- UProperty subclass data at +0x78 verified (Struct / PropertyClass / Inner / Enum; Bool ByteOffset 0x79,
  ByteMask 0x7A).
- Safety: unload deferred while a script is on the stack (`luart::AnyBusy`); dead Script shells kept so
  late callbacks bail out; Lua hooks ignored off the game thread; dispatch runs one task batch per call.
- MODS panel scrolls (drag; tap vs drag threshold; scrollbar); fully-visible rows only.
- Verified in-game: Lua overlay (struct read), UFunction call with FString return, toggle tap writes
  JumpZVelocity (525 -> 1575), shutdown() restores it, live reload, syntax + runtime errors logged
  without crash, 10-row scroll test.

## v0.6.0 (2026-11-24) — remembered toggles + Windows installer
- Toggles tapped in the MODS panel are saved as `toggle.<label> = 0/1` in the mod's ini (other lines
  kept) and restored when the toggle is registered — works for C and Lua mods without changes.
  Verified: SPEED BOOST / SUPER JUMP / SHOW POSITION survived a game restart.
- Installer (`installer/`, packaged by `tools/make_dist.sh` into `dist/ArkModLoader-<ver>[.zip]`):
  install.bat / uninstall.bat -> install.ps1. Finds MuMu + adb, starts the emulator, checks root and
  versionCode 152, optional game install from .xapk (-Xapk or Downloads), backs up libgnustl_shared.so
  to %LOCALAPPDATA%\ArkModLoader\backup\<ver>, patches it on the PC with Python+LIEF (private venv in
  %LOCALAPPDATA%\ArkModLoader\python; game files are never shipped), installs loader + mods, launches
  the game and waits for the loader's log line. Uninstall restores the exact backup, or (no backup)
  strips the DT_NEEDED with `patch_needed.py --remove`.
- Verified: clean install, update install, uninstall (sha256 identical to original), no-backup
  uninstall (game starts fine). NOT tested: -Xapk game install (would delete the user's save).
- Test finding fixed: the `py` launcher existed without a registered Python — installer now picks the
  first interpreter that actually runs.
- No-root / phone installer (APK repack) deliberately not done.

## v0.7.0 (2026-11-24) — Magisk module for phones (Zygisk)
- `magisk/` + `tools/make_magisk.sh` -> `dist/ArkModLoader-<ver>-magisk.zip` (Magisk / KernelSU+ZygiskNext,
  arm64). Game files are NOT modified: Zygisk module (`magisk/zygisk/module.cpp`, target `arkml_zygisk`)
  matches nice_name == game package in preAppSpecialize, reads `loader/libarkmodloader.so` + `mods/*` +
  `VERSION` from the module dir; in postAppSpecialize stages the mods in `<app>/files/arkml_bundle` and
  loads the loader (module fd via android_dlopen_ext -> memfd -> copy in code_cache). Other apps:
  DLCLOSE_MODULE_LIBRARY. `zygisk.hpp` vendored from zygisk-module-sample with API version 5 -> 4.
- Injected loader sits in the default linker namespace, libUE4 in the app's classloader namespace ->
  `dlopen("libUE4.so", NOLOAD)` fails. New `elfsym.cpp`: dl_iterate_phdr + GNU/SysV hash lookup of
  dynamic symbols (IFUNC resolved with bionic's arm64 args); `engine::Sym` falls back to it.
  Wait for libUE4 extended to 30 min (loader now starts at process start).
- Loader copies `files/arkml_bundle/*` into ArkMods once per bundle VERSION (marker `ArkMods/.bundled_version`).
- Verified on MuMu: forced ELF path (`-DARKML_FORCE_ELFSYM`) with the real libUE4 — symbols, ProcessEvent
  hook, engine ready, MODS panel, all mods load; bundle seeding + no re-seed after deleting a mod;
  normal dlopen path still works. ELF lookup == dlsym for libc/libm/liblog on x86_64 and arm64.
- NOT tested: the Zygisk module itself on a real Magisk phone (MuMu has its own root, no Zygisk).
