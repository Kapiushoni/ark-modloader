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

## v0.8.0 (2026-11-24) — MODS panel on UMG + MODS entry in the main menu
- `umg.cpp`: runtime UMG toolkit. Widgets via `StaticConstructObject_Internal` (optional template = live
  widget to copy its look; `Detach` then clears Slot/Slots/delegates so the copy doesn't share the
  template's place or its OnClicked bindings), BlueprintCallable setters via ProcessEvent with
  reflection-built params, overlay via `UWidget::TakeWidget` + `UShooterGameViewportClient::
  AddViewportWidgetContent` (re-added after each map load; root object created with RootSet 1<<30,
  verified against GEngine's FUObjectItem flags).
- Main menu: `MainMenuUI_C` tree = ... VerticalBox `ButtonPanel` -> CanvasPanel `ExitPanel` -> Button
  `ExitButton` -> ScaleBox -> TextBlock "EXIT". MODS entry = copies of those four (slots copied with
  `CopySlot`), ButtonPanel's anchor span grown by 4/3 (it is anchor-stretched 0.03..0.33), and each frame
  the copies mirror the originals' RenderTransform/RenderOpacity/Visibility (menu animations).
  Live widgets vs. the class's template tree: template has the class (MainMenuUI_C) in its outer chain.
- Clicks: inline hook on `UButton::SlateHandleClicked` (FReply returned via x8, passed through);
  button -> action map; actions applied on the next tick, never inside Slate's click.
- Findings: UMG `ScrollBox` created at runtime lays out content with no width (rows invisible) in this
  build; ARK's `ScrollBoxAutoSize` (copied from the main menu's patch-notes `ScrollBoxAutoSize_0`)
  works, content needs a SizeBox MinDesiredWidth. `EButtonTouchMethod` in 4.17 = {DownAndUp, PreciseTap}
  (PreciseTap = 1). `SlateChildSize` lives in /Script/UMG. Copies of hidden style holders inherit
  Collapsed -> set Visibility explicitly.
- In-game MODS button moved to top centre (the right edge has ARK's notices); hidden while the panel
  is open. Mods' on_draw/on_touch unchanged (HUD canvas / Slate hooks).
- Verified on MuMu: entry under EXIT in menu style, slides away with the menu, panel opens in menu and
  in game, mod ON/OFF (Hello Menu unload/load), toggles (FLY -> flying), saved to ini, swipe scroll
  with 20+ rows, tap vs swipe, X closes, no crash.
- NOT tested: returning from a game to the main menu (a new MainMenuUI instance gets its own entry by design).

## v0.9.0 (2026-11-24) — mod windows + SETTINGS (API v3)
- API v3 (appended to ArkModAPI): ui_window/label/button/check/number/set_text/show/visible,
  config_range. Lua: `ark.window(title)` -> w:label/button/check/number/show/hide/toggle/visible/
  set_title; controls :set_text, .on, .value; `ark.config(key, def, min, max, step)`.
- ui.cpp: model (gWindows/gControls, ids, under gMu; any thread) vs. widgets (built in UiTick on the
  game thread). Shared `BuildFrame` for the MODS panel and windows; lists always built first and
  attached last (ScrollBoxAutoSize + SizeBox). One frame open at a time. Callbacks are called from
  ApplyControl outside the lock; removed windows are taken off screen on the next tick
  (RemoveFromParent). Windows are listed under their mod ("OPEN >").
- SETTINGS: rows for mods whose ini has non-`toggle.` keys; the window is rebuilt from the ini on
  each open (file order kept: Config.order), numbers via -/+ (declared range or step guessed from the
  value), true/false|on/off|yes/no as checks, other text read-only. Writes via ConfigWrite.
- Verified on MuMu: Lua window with label/button/check/number (callbacks, set_text, hide from Lua),
  removing the mod while its window is open (window and row disappear, no crash), Cheat Menu SETTINGS
  (C config_range: multiplier step 0.5) writes fly_speed/speed_multiplier to the ini, Hello Menu text
  setting read-only.
- SDK dump (`sdk.cpp`; panel row "DUMP SDK", Lua `ark.dump_sdk([filter])`): classes/structs/enums
  sorted by path, fields with type/offset/size, functions with params and flags. Layouts verified in
  disasm: UEnum Names TArray<{FName,int64}> at 0x40 (GetNameByIndex), UFunction::FunctionFlags at 0x88
  (ProcessEvent tests bit 0x400), MapProperty ValueProp at 0x80, EnumProperty/ClassProperty second
  pointer at 0x80. Main menu: 3016 classes (1026 *_C), 1356 structs, 670 enums, 3.6 MB; cross-checked
  TextBlock.Text 0x1b0, CurrentStatusValues[12] @0x960, EButtonTouchMethod {DownAndUp, PreciseTap}.
- API v3 objects: `events.cpp` registers an FUObjectArray create listener (vtable {~D1,~D0,
  NotifyUObjectCreated}; CDOs/archetypes skipped; matching ones queued, delivered next tick if the
  slot still holds the same object). C on_new_object/load_object/spawn_actor; Lua ark.on_new_object,
  ark.load (StaticLoadObject with UObject class), ark.spawn (GameplayStatics BeginDeferred... +
  FinishSpawningActor, collision AdjustIfPossibleButAlwaysSpawn), ark.delay/every/cancel (per-script
  tick), TArray element get/set + whole-array assignment for plain element types (FMemory::Realloc).
- Verified in game: timers (every x3 + cancel), HiddenActors 0 -> 2 -> 0 with element check, Dodo
  spawned next to the player and reported by on_new_object. Timers are frame-driven (first delay fired
  after 7 s because the game stalls while starting).
- Config writing: Lua `ark.save_config(key, value)`, C `config_set` (both via ConfigWrite).
- New Lua mods (items 8-10 of the plan), written from the SDK dump: `mods/spawner` (PrimalDinoCharacter
  .SpawnDino with base level, fallback ark.spawn; PrimalItem.AddNewItem resources into
  MyInventoryComponent), `mods/teleport` (3 points in teleport.ini, K2_TeleportTo, "up 1000"),
  `mods/rates` (ShooterGameMode *Multiplier fields x user factor, originals restored on shutdown).
  STATUS: they load without errors on MuMu, but have NOT been tried in game yet. Dino paths other
  than Carno/Ptero/Direbear/Galli/... are guessed from the PC layout (the mod reports "not in this
  version" if ark.load fails). Rates: unverified whether every field is read live by the game.
  TODO: make_dist.sh / make_magisk.sh still bundle only lua_example.lua; add these mods + docs.

## v0.9.0 continued (2026-11-24) — bundled mods verified, Russian UI
- Spawner verified in game: all 12 dino + 13 resource paths load; SpawnDino's level parameters are
  ignored on mobile (always level 1) -> PrimalCharacterStatusComponent.SetBaseLevel(lvl, false) after
  spawning (Raptor level 30: HP 320/320 shown by the game; Trike 375 -> 900 HP). AddNewItem gave
  100 Metal Ingot / Cementing Paste into the inventory.
- Teleport verified: K2_TeleportTo moves the player (+3000 X confirmed), points saved in teleport.ini.
- Rates verified on XP: AddExperience(100, generic) gave +200 at x1 and +400 at x2 -> the game reads
  ShooterGameMode.XPMultiplier live. Other fields not individually measured.
- Finding: tapping the respawn screen's button through the MODS button area once ended the game with
  System.exit(0) (clean exit, not a crash; cause not identified). The in-game MODS button is now hidden
  while the player has no pawn (death/respawn screens); respawn after that worked normally.
- i18n.cpp: UI language from ArkMods/loader.ini `language = auto|en|ru` (auto = persist.sys.locale),
  Russian strings for the panel/SETTINGS/SDK rows; Lua `ark.lang()`. Cyrillic renders in the game font
  but is wider than Slate measures -> state column is a fixed-width SizeBox, text left-aligned.
  Spawner/Teleport/Rates are bilingual. `ark.save_config` / C `config_set` write ini values.
- make_dist.sh / make_magisk.sh bundle lua_example, spawner, teleport, rates.
- CI: `.github/workflows/build.yml` (ubuntu, nttld/setup-ndk r27c): build, make_dist + make_magisk,
  artifacts; tag v* = release (tag must equal kVersion). Packaging no longer needs PowerShell:
  `tools/zipdir.py` (forward slashes, 755 for scripts, CRLF check); scripts pick the first python that
  runs (skips the Windows Store stub). NOT run on GitHub yet (nothing pushed).
- Templates: mods/template/my_mod.lua (verified: MaxWalkSpeed 492.8 -> 1200 via the MY FEATURE row,
  restored on unload) and my_mod.cpp (builds with the one-line clang++ from QUICKSTART, loads, init 0).
  mods/QUICKSTART.md = "first mod in 5 minutes".
