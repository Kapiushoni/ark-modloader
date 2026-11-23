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
