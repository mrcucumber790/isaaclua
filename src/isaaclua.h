#pragma once
#include <stdio.h>

#include "game.h"

typedef struct lua_State lua_State;

/* on-screen message log (drawn from the HUD render hook) */
enum { OVERLAY_INFO = 0, OVERLAY_WARN = 1, OVERLAY_ERROR = 2 };
void overlay_add(const char *text, int kind);
void plugin_log(const char *s);

extern bool g_in_render;      // RenderText only draws while POST_RENDER runs
extern bool g_in_entity_render;  // POST_*_RENDER of an entity is running (keys: kb_key_triggered)
extern u32 g_render_frames;   // Isaac.GetFrameCount()

void api_open(lua_State *L);
void api_set_vector_ctor(lua_State *L);
void api_push_entity(lua_State *L, u8 *e);
u8 *api_player(int index);
bool api_entity_alive(const u8 *e, s32 type);
int api_room_index(const u8 *e);
bool isaaclua_mem_ok(const void *p, size_t n, bool write);  // index in the room's entity list or -1
void api_report_missing(lua_State *L, const char *what);
u8 *api_entity_ptr(lua_State *L, int idx);   // engine pointer of a live Entity userdata, else NULL
void api_push_vector(lua_State *L, Vector2 v);
void *api_sym(const char *name);              // exported Repentance.nro symbol (main.c)
#define ISAACLUA_SYM_HOOKED 8
void isaaclua_sym_got_hooked(const char *name, void *hook);  // core.sym answers name with hook (native.c)
extern void *isaaclua_sym_hooked[ISAACLUA_SYM_HOOKED];  // core.sym answers these with the loader's hook (native.c)

/* font.c: Font */
void font_open(lua_State *L);

/* modfs.c: mod resources/ folders seen by the game */
void modfs_add_mod(const char *mod_dir);
void modfs_set_mod_off(const char *mod_dir, bool off);
size_t modfs_count(void);
const char *modfs_safe_path(const char *in, char *out, size_t cap);
const char *modfs_resolve(const char *path, char *buf, size_t cap);
void *modfs_hook(void *orig);

/* native.c: core.* FFI for src/api.lua */
void native_open(lua_State *L);
void isaaclua_level_init(void *level);  // main.c: Level::Init through the POST_NEW_LEVEL hook

bool isaaclua_save_ready_early(void);  // native.c: whether the user is known yet (no user at plugin start)
char *isaaclua_save_load_early(const char *file);  // native.c: the same, also before the game mounts it
char *isaaclua_save_load(const char *file);  // native.c: sdmc:/isaaclua/<file> (save data), NULL if none
bool isaaclua_save_store(const char *file, const char *data, size_t len);
bool isaaclua_save_store_early(const char *file, const char *data, size_t len);  // also before the game mounts it
extern bool g_engine_ready;   // set on the first game update: sprites may load from then on

/* keyboard.c: on-screen keyboard and pad-to-key bindings */
void kb_filter_npad(void *state, u32 id);  // after each nn::hid::GetNpadState of the game
void kb_frame(void);                        // HUD render, before POST_RENDER
void kb_draw(void);                         // HUD render, after POST_RENDER
void kb_end_render(void);
void kb_end_update(void);
void kb_load_config(void);
bool kb_key_down(int key);
bool kb_key_triggered(int key);
bool kb_is_open(void);
bool kb_mouse(float *x, float *y, int *buttons);  // cursor mode on: its screen position, buttons 1 left / 2 right
bool kb_pad_down(int button);  // PC Controller.* ids 0-15
bool kb_pad_triggered(int button);
extern float g_ui_scale;             // sprite.c: screen drawing scale of the running mod
void ui_scale_point(float *x, float *y);
extern float g_menu_scale;           // isaaclua.ini "menu_scale", 0 = fit the screen
extern char g_mod_language[8];       // isaaclua.ini "language", empty = the game's

/* sprite.c: Sprite, Isaac.GetItemConfig, Isaac.WorldToScreen */
void sprite_open(lua_State *L);
void sprite_push_entity(lua_State *L, u8 *e);
void std_string_init(StdString *s, const char *p);  // read-only std::string over p (kept alive by the caller)
void api_init_push(const u8 *e);
void api_init_pop(void);
