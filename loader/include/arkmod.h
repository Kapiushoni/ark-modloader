/*
 * ArkModLoader public API (C ABI) — ARK Mobile 2.0.29, UE 4.17, arm64.
 *
 * A mod is an arm64 Android shared library placed in
 *   /sdcard/Android/data/com.studiowildcard.wardrumstudios.ark/files/ArkMods/
 * that exports:
 *   const char* ArkMod_Name(void);                 // optional
 *   int         ArkMod_Init(const ArkModAPI* api); // required, return 0 on success
 *
 * Optional export (API v2):
 *   void        ArkMod_Shutdown(void);             // undo your changes before unload/reload
 *
 * ArkMod_Init and ArkMod_Shutdown run on the game thread. Hook, tick, draw and touch
 * callbacks run on the game thread too; engine calls are safe there.
 *
 * Mods are hot-reloaded when their .so changes and can be switched on/off in the in-game
 * MODS panel. Everything a mod registered through this API is removed automatically when
 * it is unloaded. Mods that call inline_hook() cannot be unloaded at runtime (the change
 * applies after a game restart).
 *
 * Paths look like "/Script/Engine.Actor.ReceiveBeginPlay" or
 * "/Game/PrimalEarth/UI/MainMenuUI.MainMenuUI_C.Construct" (all '.' separated,
 * exactly as the loader log prints them). A pattern without a leading '/'
 * matches as a suffix: "Actor.ReceiveBeginPlay".
 */
#ifndef ARKMOD_H
#define ARKMOD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ARKMOD_API_VERSION 2

typedef struct UObject UObject;

typedef struct ArkHookCtx {
    UObject* self;    /* object ProcessEvent was called on */
    UObject* func;    /* UFunction */
    void*    params;  /* parameter struct (use property_offset(func, "Name")) */
    void*    user;    /* pointer given at registration */
} ArkHookCtx;

typedef int  (*ArkPreHook)(ArkHookCtx* ctx);   /* return 0 to skip the original call */
typedef void (*ArkPostHook)(ArkHookCtx* ctx);
typedef void (*ArkTask)(void* user);
typedef int  (*ArkObjectVisitor)(UObject* obj, void* user); /* return 0 to stop */
typedef void (*ArkDrawFn)(void* user, float screen_w, float screen_h);
/* phase: 0 = down, 1 = move, 2 = up. Return 1 on "down" to take the touch: its move/up
   events then come to you and the game never sees it. */
typedef int  (*ArkTouchFn)(void* user, int phase, int finger, float x, float y);

/* FText as laid out by UE 4.17 (opaque, 24 bytes). */
typedef struct ArkText { void* data; void* ref; uint32_t flags; uint32_t pad; } ArkText;

typedef struct ArkModAPI {
    uint32_t    version;
    const char* mods_dir;

    void     (*log)(const char* fmt, ...);
    void*    (*find_symbol)(const char* mangled_name);       /* dlsym in libUE4.so */

    /* objects */
    UObject* (*find_object)(const char* path);              /* "/Script/UMG.TextBlock" */
    int      (*object_name)(UObject* obj, char* buf, int n);
    int      (*object_path)(UObject* obj, char* buf, int n);
    UObject* (*object_class)(UObject* obj);
    int      (*is_a)(UObject* obj, UObject* cls);
    int      (*for_each_object)(UObject* cls, ArkObjectVisitor cb, void* user); /* live instances, no CDOs */
    UObject* (*get_world)(void);

    /* reflection */
    int      (*property_offset)(UObject* struct_or_func, const char* name); /* -1 if missing */
    UObject* (*find_function)(UObject* obj_or_class, const char* name);
    int      (*params_size)(UObject* func);
    void     (*process_event)(UObject* obj, UObject* func, void* params);   /* bypasses hooks */

    /* hooks & scheduling */
    int      (*hook_function)(const char* path, ArkPreHook pre, ArkPostHook post, void* user);
    void     (*run_on_game_thread)(ArkTask task, void* user);
    void     (*on_tick)(ArkTask task, void* user);           /* every frame, game thread */
    void*    (*inline_hook)(void* target, void* replacement);/* returns original or NULL */

    /* helpers (game thread) */
    void     (*make_text)(const char* utf8, ArkText* out);
    int      (*text_to_utf8)(const ArkText* text, char* buf, int n);
    int      (*exec_command)(const char* cmd);               /* via local PlayerController */

    /* ---- v2 (check api->version >= 2) ---- */

    /* Settings from ArkMods/<mod>.ini ("key = value" lines; <mod> = file name without lib/.so).
       Missing keys are appended to the file with the default, so users can see what to edit.
       Edits are picked up while the game runs. */
    const char* (*config_string)(const char* key, const char* def);
    int      (*config_int)(const char* key, int def);
    float    (*config_float)(const char* key, float def);

    /* In-game UI (game thread). Coordinates are screen pixels. */
    void     (*ui_toggle)(const char* label, int* value);    /* ON/OFF row in the MODS panel */
    void     (*on_draw)(ArkDrawFn fn, void* user);           /* draw over the HUD every frame */
    void     (*draw_rect)(float x, float y, float w, float h, float r, float g, float b, float a);
    void     (*draw_text)(const char* ascii, float x, float y, float scale, float r, float g, float b);
    void     (*on_touch)(ArkTouchFn fn, void* user);
} ArkModAPI;

#ifdef __cplusplus
}
#endif
#endif /* ARKMOD_H */
