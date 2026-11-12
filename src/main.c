/* isaaclua: Lua 5.3 mods for The Binding of Isaac: Repentance on Switch (1.7.9b).
 *
 * Same mechanism as isaacqol: we are an extra NSO that rtld loads before the game. Our DT_INIT
 * redirects nn::ro::LoadModule; when Repentance.nro is loaded we check it is the analysed build,
 * create the Lua state, run every rom:/mods/<name>/main.lua and hook the engine (GOT slots and
 * vtable entries of Repentance.nro, never its code) to fire the PC-style callbacks.
 *
 * Coexistence with isaacqol (subsdk9): both plugins hook LoadModule and HUD::Render. LoadModule is
 * chained whatever the init order (see install_loadmodule_hooks); HUD::Render is installed on the
 * first game update, when isaacqol has certainly installed its own hook, and chains to it. */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "isaaclua.h"
#include "version.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "nn.h"
#include "prelude_lua.h"

#define HIDDEN __attribute__((visibility("hidden")))

GameApi G;
u32 g_render_frames = 0;

/* ------------------------------------------------------------ platform */

typedef struct { u64 addr, size; u32 type, attr, perm, ipc_refcount, device_refcount, pad; } MemoryInfo;
u32 qol_svcQueryMemory(MemoryInfo *out, u32 *pageinfo, u64 addr);
void qol_svcOutputDebugString(const char *str, u64 len);
extern char __module_start[];

void plugin_log(const char *s) { qol_svcOutputDebugString(s, strlen(s)); }

/* Lua's print() and lauxlib's error output (luaconf.h) */
void isaaclua_write(const char *s, size_t l) {
    static char line[160];
    static size_t n = 0;
    for (size_t i = 0; i < l; i++) {
        if (s[i] == '\n' || n == sizeof line - 1) {
            line[n] = 0;
            overlay_add(line, OVERLAY_INFO);
            n = 0;
            if (s[i] == '\n') continue;
        }
        line[n++] = s[i];
    }
}
void isaaclua_writef(const char *fmt, const char *arg) {
    char buf[256];
    snprintf(buf, sizeof buf, fmt, arg);
    overlay_add(buf, OVERLAY_ERROR);
}

/* ------------------------------------------------------------ overlay */

#define OVERLAY_LINES 8
typedef struct { char text[128]; int kind; int ttl; } OverlayLine;
static OverlayLine s_overlay[OVERLAY_LINES];

/* isaaclua.ini autotest = <minutes>: the Lua side plays by itself (src/autotest.lua) and writes
 * sdmc:/isaaclua/autotest.log: a status line every few seconds (free memory, Lua memory, errors, what it did)
 * and every new error's first line, the file rewritten at each status line so a crash keeps what came before. */
#ifdef ISAACLUA_DEBUG
int g_autotest = 20;  // minutes (per batch)
bool g_autotest_shots = true;  // isaaclua.ini autotest_shots = 0: no screenshots
#else
int g_autotest;
#endif
static char s_alog[64 * 1024], s_alog_file[32] = "autotest.log";
static size_t s_alog_n, s_alog_head;
#ifdef ISAACLUA_DEBUG
static void autotest_flush_pending(void);
#endif
void autotest_log(const char *line, bool flush) {
    size_t n = strlen(line);
    if (n > 1024) n = 1024;
    if (s_alog_n + n + 1 > sizeof s_alog) {  // full: drop the oldest lines after the first one
        size_t cut = s_alog_head + sizeof s_alog / 2;
        while (cut < s_alog_n && s_alog[cut - 1] != '\n') cut++;
        memmove(s_alog + s_alog_head, s_alog + cut, s_alog_n - cut);
        s_alog_n -= cut - s_alog_head;
    }
    memcpy(s_alog + s_alog_n, line, n);
    s_alog_n += n;
    s_alog[s_alog_n++] = '\n';
    if (!s_alog_head) s_alog_head = s_alog_n;
    if (flush && nnfs_IsMounted("sdmc")) {
        isaaclua_save_store(s_alog_file, s_alog, s_alog_n);
#ifdef ISAACLUA_DEBUG
        autotest_flush_pending();
#endif
    }
}
/* errors and warnings also go to sdmc:/isaaclua/errors.log (the game's save data, readable with a save
 * manager such as JKSV): each distinct message once, with its traceback, rewritten every few seconds
 * while something new came in. The file starts afresh on every launch. */
static char s_log[48 * 1024];
static size_t s_log_n;
static bool s_log_dirty;
static u32 s_log_seen[256];
static int s_log_nseen;
static void log_add(const char *text, int kind) {
    u32 h = 2166136261u;
    for (const char *c = text; *c; c++) h = (h ^ (u8)*c) * 16777619u;
    for (int i = 0; i < s_log_nseen; i++)
        if (s_log_seen[i] == h) return;
    if (s_log_nseen < (int)(sizeof s_log_seen / sizeof *s_log_seen)) s_log_seen[s_log_nseen++] = h;
    if (s_log_n == 0) s_log_n = (size_t)snprintf(s_log, sizeof s_log, "isaaclua " ISAACLUA_VERSION_TEXT "\n");
    const char *tail = "... (log full)\n";
    if (s_log_n + strlen(tail) + 1 >= sizeof s_log) return;
    size_t room = sizeof s_log - s_log_n - strlen(tail) - 1;
    int k = snprintf(s_log + s_log_n, room, "%s %s\n", kind == OVERLAY_ERROR ? "ERROR" : "WARN ", text);
    if (k < 0 || (size_t)k >= room) {
        memcpy(s_log + s_log_n, tail, strlen(tail) + 1);
        s_log_n += strlen(tail);
    } else
        s_log_n += (size_t)k;
    s_log_dirty = true;
    if (g_autotest) {
        char line[160];
        snprintf(line, sizeof line, "  %s %.*s", kind == OVERLAY_ERROR ? "ERROR" : "WARN ", (int)strcspn(text, "\n"), text);
        autotest_log(line, false);
    }
}
#ifdef ISAACLUA_DEBUG
/* a file the game only tries before the plain one: a floor's own look (monster_downpour.pcx), a skin colour
 * (costume_black.pcx), a champion's, the Japanese and Korean fonts the Switch build lacks */
static bool probed_variant(const char *path) {
    static const char *const kSuffix[] = {"_downpour", "_dross", "_mines", "_ashpit", "_mausoleum", "_gehenna",
                                          "_corpse", "_black", "_grey", "_white", "_blue", "_green", "_red",
                                          "_shadow", "_champion"};
    if (strstr(path, "font/MPlus_") || strstr(path, "font/KR_")) return true;
    const char *dot = strrchr(path, '.');
    if (!dot) return false;
    for (size_t i = 0; i < sizeof kSuffix / sizeof *kSuffix; i++) {
        size_t n = strlen(kSuffix[i]);
        if ((size_t)(dot - path) >= n && strncasecmp(dot - n, kSuffix[i], n) == 0) return true;
    }
    return false;
}
#endif
bool isaaclua_save_flush(bool force);  // native.c
static void log_flush(void) {  // once per rendered frame; errors.log is written at most every 20 s (a commit costs
    static s64 last;           // about 0.1 s on the console: more often, it showed as a stutter)
    isaaclua_save_flush(false);
#ifdef ISAACLUA_DEBUG
    const char *miss;
    extern const char *modfs_take_missing(void);
    for (int i = 0; i < 8 && (miss = modfs_take_missing()); i++) {
        /* ModManager::UpdateRooms tries every mod's content(-dlc3)/rooms/<stage>.stb: most have none */
        if (!*miss || (strstr(miss, "/rooms/") && strstr(miss, ".stb"))) continue;  // "": a sprite with no file, as on PC
        if (strstr(miss, "bosspools.xml") || strstr(miss, "itempools.xml") || probed_variant(miss)) continue;  // optional, the game falls back
        char msg[300];
        snprintf(msg, sizeof msg, "file not found (mods and game): %s", miss);
        overlay_add(msg, OVERLAY_WARN);
    }
#endif
    s64 f = nnosGetSystemTickFrequency(), now = f ? nnosGetSystemTick() / (f / 1000) : 0;
    if (!last) last = now - 17000;  // the first errors (loading the mods) are written within 3 s
    if (!s_log_dirty || now - last < 20000 || !nnfs_IsMounted("sdmc")) return;
    last = now;
    s_log_dirty = !isaaclua_save_store("errors.log", s_log, s_log_n);
}

static int s_errors;  // errors shown so far (host test, autotest)
int overlay_errors(void) { return s_errors; }
void overlay_add(const char *text, int kind) {
    char tagged[1024];
    if (kind == OVERLAY_ERROR) s_errors++;
    if (kind != OVERLAY_INFO) log_add(text, kind);
    snprintf(tagged, sizeof tagged, "[isaaclua] %s", text);
    plugin_log(tagged);
    memmove(&s_overlay[0], &s_overlay[1], sizeof(OverlayLine) * (OVERLAY_LINES - 1));
    OverlayLine *l = &s_overlay[OVERLAY_LINES - 1];
    size_t n = strcspn(text, "\n");  // first line only on screen (the log keeps the traceback)
    if (n >= sizeof l->text) n = sizeof l->text - 1;
    memcpy(l->text, text, n);
    l->text[n] = 0;
    l->kind = kind;
    l->ttl = kind == OVERLAY_ERROR ? 900 : 420;  // render frames, counted only while shown
}

static char s_perf_line[200];
const char *perf_line(void) { return s_perf_line; }
static void overlay_draw(void) {
    log_flush();
    u8 *mgr = manager_ptr();
    if (!mgr || !G.draw_utf8 || !G.color_ctor) return;
    if (s_perf_line[0]) {
        ColorStorage c;
        G.color_ctor(&c, 0.6f, 1.0f, 0.6f, 1.0f);
        G.draw_utf8(mgr + kManager_HudFont, s_perf_line, 4.0f, (G.height ? *G.height : 270.0f) - 9.0f, 0.5f, 0.5f, &c, 0, false);
    }
    float y = 4.0f;  // top centre, clear of hearts / pickups counters
    for (int i = 0; i < OVERLAY_LINES; i++) {
        OverlayLine *l = &s_overlay[i];
        if (l->ttl <= 0) continue;
        l->ttl--;
        float a = l->ttl < 60 ? (float)l->ttl / 60.0f : 1.0f;
        ColorStorage c;
        if (l->kind == OVERLAY_ERROR) G.color_ctor(&c, 1.0f, 0.4f, 0.4f, a);
        else if (l->kind == OVERLAY_WARN) G.color_ctor(&c, 1.0f, 0.85f, 0.4f, a);
        else G.color_ctor(&c, 1.0f, 1.0f, 1.0f, a);
        G.draw_utf8(mgr + kManager_HudFont, l->text, 130.0f, y, 0.5f, 0.5f, &c, 0, false);
        y += 7.0f;
    }
}

/* ------------------------------------------------------------ ELF module view */

typedef struct { s64 tag; u64 val; } Elf64_Dyn;
typedef struct { u64 offset, info; s64 addend; } Elf64_Rela;
typedef struct { u32 name; u8 info, other; uint16_t shndx; u64 value, size; } Elf64_Sym;

typedef struct {
    uintptr_t base;
    const Elf64_Rela *jmprel;
    size_t jmprel_count;
    const Elf64_Sym *symtab;
    const char *strtab;
    u32 nsyms;  // from DT_HASH (0 if absent)
} ModView;

static int core_repplus(lua_State *l);
static int core_rp_proc_item(lua_State *l);
static int core_rp_je(lua_State *l);
static int rp_je_action(int trig, void *im, u32 action, u32 ctrl, u32 *pp);
static void repplus_install(const ModView *v, const struct nnro_Module *m);
static void daily_install(const ModView *v);
static bool mod_open(ModView *v, uintptr_t b) {
    memset(v, 0, sizeof *v);
    v->base = b;
    u32 mod0 = *(const u32 *)(b + 4);
    if (mod0 == 0 || mod0 > 0x10000000) return false;
    const u8 *m = (const u8 *)(b + mod0);
    if (memcmp(m, "MOD0", 4) != 0) return false;
    const Elf64_Dyn *dyn = (const Elf64_Dyn *)(b + mod0 + *(const s32 *)(m + 4));
    u64 pltrelsz = 0;
    for (; dyn->tag != 0; dyn++) {
        if (dyn->tag == 23) v->jmprel = (const Elf64_Rela *)(b + dyn->val);       // DT_JMPREL
        else if (dyn->tag == 2) pltrelsz = dyn->val;                              // DT_PLTRELSZ
        else if (dyn->tag == 6) v->symtab = (const Elf64_Sym *)(b + dyn->val);    // DT_SYMTAB
        else if (dyn->tag == 5) v->strtab = (const char *)(b + dyn->val);         // DT_STRTAB
        else if (dyn->tag == 4) v->nsyms = ((const u32 *)(b + dyn->val))[1];      // DT_HASH nchain
    }
    v->jmprel_count = pltrelsz / sizeof(Elf64_Rela);
    return v->symtab && v->strtab;
}

/* GOT slot used by calls to `name` from inside the module */
static u64 *mod_got_slot(const ModView *v, const char *name) {
    for (size_t i = 0; i < v->jmprel_count; i++) {
        const Elf64_Rela *r = &v->jmprel[i];
        if ((u32)r->info != 1026) continue;  // R_AARCH64_JUMP_SLOT
        if (strcmp(v->strtab + v->symtab[r->info >> 32].name, name) == 0) return (u64 *)(v->base + r->offset);
    }
    return NULL;
}

static bool same_region(uintptr_t a, uintptr_t b) {
    MemoryInfo mi;
    u32 pi;
    if (qol_svcQueryMemory(&mi, &pi, a) != 0) return false;
    return b >= mi.addr && b < mi.addr + mi.size;
}

static bool writable(const void *p) {
    MemoryInfo mi;
    u32 pi;
    return qol_svcQueryMemory(&mi, &pi, (u64)(uintptr_t)p) == 0 && (mi.perm & 2);
}

/* Is [p, p+n) mapped (and writable when asked)? core.peek/poke use it so a stale or garbage pointer in Lua
 * becomes a Lua error with a traceback instead of a crash (HW 0.99.44: i32 read of 0x5db7ec9820 in
 * POST_PLAYER_INIT on continue). Recent regions are cached: peek is hot, the syscall is not cheap. */
bool isaaclua_mem_ok(const void *p, size_t n, bool write) {
#ifdef ISAACLUA_HOST_TEST
    (void)p, (void)n, (void)write;
    return true;
#else
    static struct { uintptr_t lo, hi; u32 perm; } cache[8];
    static unsigned next;
    uintptr_t a = (uintptr_t)p, e = a + (n ? n : 1);
    if (e < a) return false;
    for (unsigned i = 0; i < 8; i++)
        if (a >= cache[i].lo && e <= cache[i].hi) return !write || (cache[i].perm & 2);
    MemoryInfo mi;
    u32 pi;
    if (qol_svcQueryMemory(&mi, &pi, a) != 0 || !(mi.perm & 1) || e > mi.addr + mi.size) return false;
    cache[next & 7].lo = mi.addr, cache[next & 7].hi = mi.addr + mi.size, cache[next & 7].perm = mi.perm;
    next++;
    return !write || (mi.perm & 2);
#endif
}

/* Replace a GOT slot, returning what to call to continue (an earlier hook, or the PLT stub). */
/* What a GOT slot of the module leads to. nn::ro loads Repentance.nro with lazy binding: until the first
 * call, a slot holds the module's own PLT0 resolver, which binds the slot named by x16, so calling it from
 * a hook binds a random slot (x16 is whatever our code left there) and crashes in nn::ro::detail::Bind.
 * An unbound slot is resolved here instead: the module's own definition, else `real` (an import of ours). */
static void *got_target(const ModView *v, const u64 *slot, const char *name, void *real) {
    uintptr_t cur = (uintptr_t)*slot;
    if (!same_region(v->base, cur)) return (void *)cur;  // bound, or another plugin's hook
    for (size_t i = 0; i < v->jmprel_count; i++) {
        const Elf64_Rela *r = &v->jmprel[i];
        if ((u64 *)(v->base + r->offset) != slot) continue;
        const Elf64_Sym *sym = &v->symtab[r->info >> 32];
        if (sym->shndx != 0 && sym->value != 0) return (void *)(v->base + sym->value);
    }
    if (!real) {
        char msg[160];
        snprintf(msg, sizeof msg, "[isaaclua] unbound import left alone: %s", name);
        plugin_log(msg);
    }
    return real;
}

/* point the slot at `hook`; returns what the module called before (NULL: slot left alone) */
static void *hook_got_to(const ModView *v, const char *name, void *hook, void *real) {
    u64 *slot = mod_got_slot(v, name);
    if (!slot) return NULL;
    void *prev = got_target(v, slot, name, real);
    if (!prev) return NULL;
    *slot = (u64)(uintptr_t)hook;
    isaaclua_sym_got_hooked(name, hook);
    return prev;
}
static void *hook_got(const ModView *v, const char *name, void *hook) { return hook_got_to(v, name, hook, NULL); }

/* ------------------------------------------------------------ Lua state and dispatch */

static lua_State *L;
static int s_run_ref = LUA_NOREF, s_loadmod_ref = LUA_NOREF, s_perf_ref = LUA_NOREF, s_modoff_ref = LUA_NOREF, s_enablemod_ref = LUA_NOREF;

/* isaaclua.ini perf = 1: Lua time per game update and per mod, game updates and renders per second */
extern bool g_perf;
static int s_perf_depth;
static s64 s_perf_t0, s_perf_lua, s_perf_frame_lua, s_perf_frame_max, s_perf_window;
static int s_perf_updates, s_perf_renders;
static void perf_begin(void) {
    if (g_perf && s_perf_depth++ == 0) s_perf_t0 = nnosGetSystemTick();
}
static void perf_end(void) {
    if (g_perf && --s_perf_depth == 0) s_perf_frame_lua += nnosGetSystemTick() - s_perf_t0;
}
static bool s_cb_used[80];
/* which keys (entity type, variant...) a callback id has callbacks for, from prelude.lua (core.callback_keys):
 * an entity callback with no callback for this entity's key is not run at all (each Run is a pcall, an entity
 * userdata and a Lua table lookup; StageAPI keys its many POST_EFFECT_UPDATE / NPC_UPDATE callbacks) */
#define kMaxCbKeys 48
static bool s_cb_anykey[80];
static int s_cb_nkeys[80];
static lua_Integer s_cb_keys[80][kMaxCbKeys];
static bool cb_key_ok(int id, bool has_key, lua_Integer key) {
    if (!has_key || key == -1 || s_cb_anykey[id]) return true;
    for (int i = 0; i < s_cb_nkeys[id]; i++) if (s_cb_keys[id][i] == key) return true;
    return false;
}

enum {
    MC_NPC_UPDATE = 0, MC_POST_UPDATE = 1, MC_POST_RENDER = 2, MC_USE_ITEM = 3, MC_POST_PEFFECT_UPDATE = 4,
    MC_USE_CARD = 5, MC_FAMILIAR_UPDATE = 6, MC_EVALUATE_CACHE = 8, MC_POST_PLAYER_INIT = 9, MC_USE_PILL = 10,
    MC_POST_GAME_STARTED = 15, MC_PRE_GAME_EXIT = 17, MC_POST_NEW_LEVEL = 18, MC_POST_NEW_ROOM = 19,
    MC_PRE_USE_ITEM = 23, MC_POST_PLAYER_UPDATE = 31, MC_POST_PICKUP_INIT = 34, MC_POST_PICKUP_UPDATE = 35,
    MC_POST_TEAR_UPDATE = 40, MC_POST_EFFECT_UPDATE = 55, MC_POST_PLAYER_RENDER = 32, MC_POST_PICKUP_RENDER = 36,
    MC_POST_EFFECT_INIT = 54, MC_POST_EFFECT_RENDER = 56, MC_INPUT_ACTION = 13, MC_PRE_PICKUP_COLLISION = 38,
    MC_POST_GET_COLLECTIBLE = 63, MC_PRE_GET_COLLECTIBLE = 62, MC_POST_NPC_INIT = 27, MC_POST_NPC_RENDER = 28,
    MC_POST_GAME_END = 16, MC_PRE_ROOM_ENTITY_SPAWN = 71, MC_EXECUTE_CMD = 22, MC_GET_SHADER_PARAMS = 21,
    MC_ENTITY_TAKE_DMG = 11, MC_POST_TEAR_INIT = 39, MC_POST_TEAR_RENDER = 41, MC_POST_PROJECTILE_INIT = 43,
    MC_POST_PROJECTILE_UPDATE = 44, MC_POST_PROJECTILE_RENDER = 45, MC_POST_BOMB_INIT = 57, MC_POST_BOMB_UPDATE = 58,
    MC_POST_BOMB_RENDER = 59, MC_POST_FIRE_TEAR = 61, MC_PRE_SPAWN_CLEAN_AWARD = 70, MC_POST_NPC_DEATH = 29,
    MC_POST_LASER_INIT = 47, MC_POST_LASER_UPDATE = 48, MC_POST_LASER_RENDER = 49,
    MC_POST_KNIFE_INIT = 50, MC_POST_KNIFE_UPDATE = 51, MC_POST_KNIFE_RENDER = 52, MC_POST_ENTITY_KILL = 68,
    MC_FAMILIAR_INIT = 7, MC_PRE_FAMILIAR_COLLISION = 26, MC_PRE_NPC_COLLISION = 30, MC_PRE_PLAYER_COLLISION = 33,
    MC_PRE_TEAR_COLLISION = 42, MC_PRE_PROJECTILE_COLLISION = 46, MC_PRE_KNIFE_COLLISION = 53, MC_PRE_BOMB_COLLISION = 60, MC_GET_TRINKET = 66, MC_POST_ENTITY_REMOVE = 67,
    MC_POST_CURSE_EVAL = 12, MC_PRE_MOD_UNLOAD = 73, MC_PRE_ENTITY_SPAWN = 24, MC_POST_FAMILIAR_RENDER = 25,
    MC_PRE_NPC_UPDATE = 69, MC_GET_CARD = 20, MC_POST_PICKUP_SELECTION = 37, MC_GET_PILL_COLOR = 64,
    MC_GET_PILL_EFFECT = 65, MC_PRE_ENTITY_DEVOLVE = 72,
};
static const int kSupported[] = {MC_NPC_UPDATE, MC_POST_UPDATE, MC_POST_RENDER, MC_USE_ITEM,
                                 MC_POST_PEFFECT_UPDATE, MC_USE_CARD, MC_FAMILIAR_UPDATE, MC_EVALUATE_CACHE,
                                 MC_POST_PLAYER_INIT, MC_USE_PILL, MC_POST_GAME_STARTED, MC_PRE_GAME_EXIT,
                                 MC_POST_NEW_LEVEL, MC_POST_NEW_ROOM, MC_PRE_USE_ITEM, MC_POST_PLAYER_UPDATE,
                                 MC_POST_PICKUP_INIT, MC_POST_PICKUP_UPDATE, MC_POST_TEAR_UPDATE,
                                 MC_POST_EFFECT_UPDATE, MC_POST_PLAYER_RENDER, MC_POST_PICKUP_RENDER,
                                 MC_POST_EFFECT_INIT, MC_POST_EFFECT_RENDER, MC_INPUT_ACTION,
                                 MC_PRE_PICKUP_COLLISION, MC_POST_GET_COLLECTIBLE, MC_PRE_GET_COLLECTIBLE, MC_PRE_ROOM_ENTITY_SPAWN,
                                 MC_EXECUTE_CMD, MC_GET_SHADER_PARAMS,
                                 MC_ENTITY_TAKE_DMG, MC_POST_TEAR_INIT, MC_POST_TEAR_RENDER, MC_POST_PROJECTILE_INIT,
                                 MC_POST_PROJECTILE_UPDATE, MC_POST_PROJECTILE_RENDER, MC_POST_BOMB_INIT,
                                 MC_POST_BOMB_UPDATE, MC_POST_BOMB_RENDER, MC_POST_FIRE_TEAR, MC_POST_NPC_INIT, MC_POST_NPC_RENDER,
                                 MC_POST_GAME_END, MC_PRE_SPAWN_CLEAN_AWARD, MC_POST_NPC_DEATH,
                                 MC_POST_LASER_INIT, MC_POST_LASER_UPDATE, MC_POST_LASER_RENDER,
                                 MC_POST_KNIFE_INIT, MC_POST_KNIFE_UPDATE, MC_POST_KNIFE_RENDER, MC_POST_ENTITY_KILL,
                                 MC_FAMILIAR_INIT, MC_PRE_FAMILIAR_COLLISION, MC_PRE_NPC_COLLISION, MC_PRE_PLAYER_COLLISION,
                                 MC_PRE_TEAR_COLLISION, MC_PRE_PROJECTILE_COLLISION, MC_PRE_KNIFE_COLLISION, MC_PRE_BOMB_COLLISION, MC_GET_TRINKET, MC_POST_ENTITY_REMOVE,
                                 MC_POST_CURSE_EVAL, MC_PRE_MOD_UNLOAD, MC_PRE_ENTITY_SPAWN,
                                 MC_POST_FAMILIAR_RENDER, MC_PRE_NPC_UPDATE, MC_GET_CARD,
                                 MC_POST_PICKUP_SELECTION, MC_GET_PILL_COLOR, MC_GET_PILL_EFFECT, MC_PRE_ENTITY_DEVOLVE};  // mods never unload on Switch  // shader params: run from api.lua

/* Run(id, key, entity?, extra?) — entity NULL / extra < 0 are left out */
static void run(int id, bool has_key, lua_Integer key, u8 *ent, lua_Integer extra, int extra_kind) {
    if (!L || id < 0 || id >= 80 || !s_cb_used[id] || !cb_key_ok(id, has_key, key)) return;
    int top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, s_run_ref);
    lua_pushinteger(L, id);
    if (has_key) lua_pushinteger(L, key);
    else lua_pushnil(L);
    int n = 2;
    if (ent) api_push_entity(L, ent), n++;
    if (extra_kind == 1) lua_pushinteger(L, extra), n++;
    else if (extra_kind == 2) lua_pushboolean(L, (int)extra), n++;
    /* the entity the game is updating right now exists, whatever list holds it (a player the game updates
     * outside the PlayerManager list and the room's list, as with Andromeda in batch 1): alive meanwhile, as on PC */
    if (ent) api_init_push(ent);
    perf_begin();
    if (lua_pcall(L, n, 0, 0) != LUA_OK) overlay_add(lua_tostring(L, -1), OVERLAY_ERROR);
    perf_end();
    if (ent) api_init_pop();
    lua_settop(L, top);
}

/* Run(id, key, args...) with args from fmt: i integer, b boolean, e entity, r a new RNG; returns the
 * callbacks' result as a boolean (true = a callback returned true) */
static int run_vraw(int id, bool has_key, lua_Integer key, const char *fmt, va_list ap);
static bool run_args(int id, bool has_key, lua_Integer key, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int top = run_vraw(id, has_key, key, fmt, ap);
    va_end(ap);
    if (top < 0) return false;
    bool r = lua_toboolean(L, -1);
    lua_settop(L, top);
    return r;
}
/* run_args with the callbacks' result left on the stack (nil after an error): returns the top to restore with
 * lua_settop, or -1 when nothing ran */
static int run_raw(int id, bool has_key, lua_Integer key, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int top = run_vraw(id, has_key, key, fmt, ap);
    va_end(ap);
    return top;
}
static int run_vraw(int id, bool has_key, lua_Integer key, const char *fmt, va_list ap) {
    if (!L || id < 0 || id >= 80 || !s_cb_used[id] || !cb_key_ok(id, has_key, key)) return -1;
    int top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, s_run_ref);
    lua_pushinteger(L, id);
    if (has_key) lua_pushinteger(L, key);
    else lua_pushnil(L);
    for (const char *c = fmt; *c; c++) {
        if (*c == 'i') lua_pushinteger(L, va_arg(ap, lua_Integer));
        else if (*c == 'b') lua_pushboolean(L, va_arg(ap, int));
        else if (*c == 'e') api_push_entity(L, va_arg(ap, u8 *));
        else if (*c == 'r' && lua_getglobal(L, "RNG") != LUA_TNIL) lua_call(L, 0, 1);  // RNG() (a callable class table)
        else if (*c == 'v') api_push_vector(L, *va_arg(ap, const Vector2 *));
        else if (*c == 'f') lua_pushnumber(L, va_arg(ap, double));
        else if (*c == 'R') {  // const EntityRef *: api.lua builds the Lua EntityRef
            const void *ref = va_arg(ap, const void *);
            if (lua_getglobal(L, "_isaaclua_entref") == LUA_TFUNCTION) {
                lua_pushlightuserdata(L, (void *)ref);
                lua_call(L, 1, 1);
            }
        }
    }
    perf_begin();
    int st = lua_pcall(L, 2 + (int)strlen(fmt), 1, 0);
    perf_end();
    if (st != LUA_OK) {
        overlay_add(lua_tostring(L, -1), OVERLAY_ERROR);
        lua_pop(L, 1);
        lua_pushnil(L);
    }
    return top;
}

/* A callback's number as PC reads it (luaL_checkinteger in the LuaEngine wrappers, isaac-ng.exe 0x801c10 and
 * others): nil = no answer; integers, whole floats (2.0) and numeric strings count; anything else is the PC's
 * "number expected" error, which PC logs and then keeps the game's own value. */
static bool cb_integer(int idx, const char *what, lua_Integer *out) {
    if (lua_isnil(L, idx)) return false;
    int ok = 0;
    lua_Integer v = lua_tointegerx(L, idx, &ok);
    if (!ok) {
        char msg[160];
        snprintf(msg, sizeof msg, "%s: number expected, got %s", what, luaL_typename(L, idx));
        overlay_add(msg, OVERLAY_ERROR);
        return false;
    }
    *out = v;
    return true;
}
/* a bool answer as PC reads it (lua_type != nil, then lua_toboolean): any value but nil counts, and only
 * false is false (1, a table or an entity are true, as on PC) */
static int cb_bool(int idx) { return lua_isnil(L, idx) ? -1 : lua_toboolean(L, idx); }

static int core_log(lua_State *l) {
    overlay_add(luaL_checkstring(l, 1), OVERLAY_WARN);
    return 0;
}
/* core.readfile(path) -> the whole file as a string, or nil (mods' content/ xml; there is no io library) */
static int core_readfile(lua_State *l) {
    FILE *f = fopen(luaL_checkstring(l, 1), "r");
    if (!f) return 0;
    luaL_Buffer b;
    luaL_buffinit(l, &b);
    char chunk[512];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) luaL_addlstring(&b, chunk, n);
    fclose(f);
    luaL_pushresult(&b);
    return 1;
}
static int core_error(lua_State *l) {
    overlay_add(luaL_checkstring(l, 1), OVERLAY_ERROR);
    return 0;
}
static int core_supported(lua_State *l) {
    lua_Integer id = luaL_checkinteger(l, 1);
    bool ok = false;
    for (size_t i = 0; i < sizeof kSupported / sizeof *kSupported; i++) ok |= kSupported[i] == id;
    if (id >= 0 && id < 80 && !s_cb_used[id]) s_cb_used[id] = true, s_cb_anykey[id] = true;  // AddCallback calls this for every registration
    lua_pushboolean(l, ok);
    return 1;
}
/* core.callback_keys(id, keys): the keys id's callbacks are for (nil: any key) */
static int core_callback_keys(lua_State *l) {
    lua_Integer id = luaL_checkinteger(l, 1);
    if (id < 0 || id >= 80) return 0;
    if (!lua_istable(l, 2)) { s_cb_anykey[id] = true; return 0; }
    lua_Integer n = luaL_len(l, 2);
    if (n > kMaxCbKeys) { s_cb_anykey[id] = true; return 0; }
    for (lua_Integer i = 1; i <= n; i++) {
        lua_rawgeti(l, 2, i);
        s_cb_keys[id][i - 1] = lua_tointeger(l, -1);
        lua_pop(l, 1);
    }
    s_cb_nkeys[id] = (int)n;
    s_cb_anykey[id] = false;
    return 0;
}

/* wall clock, UTC seconds: time(NULL) on the console counts from boot (it gave ~1 day), the game's own
 * nn::time clock (already initialized by the game) has the real date */
u32 nntime_GetCurrentTime(s64 *posix) __asm__("_ZN2nn4time23StandardUserSystemClock14GetCurrentTimeEPNS0_9PosixTimeE");
static s64 isaaclua_now(void) {
#ifndef ISAACLUA_HOST_TEST
    s64 t = 0;
    if (nntime_GetCurrentTime(&t) == 0 && t > 1000000000LL) return t;
#endif
    return (s64)time(NULL);
}

/* a minimal os library (PC Isaac only exposes these without --luadebug as well) */
static int os_time(lua_State *l) {
    lua_pushinteger(l, (lua_Integer)isaaclua_now());
    return 1;
}
static int os_clock(lua_State *l) {
    lua_pushnumber(l, (lua_Number)clock() / CLOCKS_PER_SEC);
    return 1;
}
static int os_difftime(lua_State *l) {
    lua_pushnumber(l, luaL_checknumber(l, 1) - luaL_optnumber(l, 2, 0));
    return 1;
}

static void *l_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    (void)ud, (void)osize;
    if (nsize == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, nsize);
}

/* PC (ModManager::ListMods 0x870b40) sorts the mods by the <name> of their metadata.xml (the folder name
 * when it has none), byte by byte (comparator 0x870b00): " Custom Stage API" loads before "Damn, Fiend Folio
 * Reheated", "!!~External item descriptions" first. Mods rely on it for their dependencies. */
struct mod_key {
    char key[160];
    nnfs_DirectoryEntry ent;
};
static void mod_meta_name(const char *dir, char *out, size_t cap) {
    char path[820];
    snprintf(path, sizeof path, "rom:/mods/%s/metadata.xml", dir);
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (f) {
        char buf[4096];
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        buf[n] = 0;
        const char *m = strstr(buf, "<metadata");
        const char *a = m ? strstr(m, "<name>") : NULL;
        const char *e = a ? strstr(a + 6, "</name>") : NULL;
        if (a && e) {
            size_t k = (size_t)(e - a - 6);
            if (k >= cap) k = cap - 1;
            memcpy(out, a + 6, k);
            out[k] = 0;
        }
    }
    if (!out[0]) snprintf(out, cap, "%s", dir);
}
static int cmp_mod_keys(const void *a, const void *b) {
    const struct mod_key *x = (const struct mod_key *)a, *y = (const struct mod_key *)b;
    int c = strcmp(x->key, y->key);
    return c ? c : strcmp(x->ent.name, y->ent.name);
}
static void sort_mods(nnfs_DirectoryEntry *ents, s64 count) {
    struct mod_key *k = count > 0 ? (struct mod_key *)malloc((size_t)count * sizeof *k) : NULL;
    if (!k) return;
    for (s64 i = 0; i < count; i++) k[i].ent = ents[i], mod_meta_name(ents[i].name, k[i].key, sizeof k[i].key);
    qsort(k, (size_t)count, sizeof *k, cmp_mod_keys);
    for (s64 i = 0; i < count; i++) ents[i] = k[i].ent;
    free(k);
}

/* the folders switched off in the Mods menu, one per line (the game's disable.it can't be written in romfs) */
static const char kModsOffFile[] = "mods_off.txt";
#ifdef ISAACLUA_DEBUG
static bool s_remote;  // rom:/isaaclua_remote.ini: remote control (src/remote.c)
#endif
static bool name_in_list(const char *list, const char *name) {
    size_t n = strlen(name);
    for (const char *l = list; l && *l;) {
        const char *e = strchr(l, '\n');
        size_t k = e ? (size_t)(e - l) : strlen(l);
        if (k == n && memcmp(l, name, n) == 0) return true;
        l += k + (e != NULL);
    }
    return false;
}

#ifdef ISAACLUA_DEBUG
/* Autotest in batches (isaaclua.ini autotest_batch = <mods>, default 10; 0: all mods at once): of the mods that
 * are on, sorted by folder, one batch per launch is loaded (Lua and content/), played for the autotest's time,
 * then the game relaunches itself (nn::oe::RequestToRelaunchApplication) with the next batch. The save data keeps
 * isaaclua/autotest_state.txt ("<batch> running|done", written before the batch's mods load) and appends one
 * line per batch to autotest_summary.txt; a batch still "running" at the next start crashed, and is skipped.
 * Each batch has its own autotest_<batch>.log. */
int g_autotest_batch_size = 10;
char g_autotest_only[64];  // isaaclua.ini autotest_only = "1, 8, 14": only these batches (empty: all)
static bool batch_wanted(int k) {
    if (!g_autotest_only[0]) return true;
    for (const char *p = g_autotest_only; *p;) {
        char *e;
        long n = strtol(p, &e, 10);
        if (e == p) { p++; continue; }
        if (n == k) return true;
        p = e;
    }
    return false;
}
/* what the state file is for: this build, and the batch list when there is one (a new list starts over) */
static const char *state_tag(void) {
    static char tag[96];
    size_t n = 0;
    n += (size_t)snprintf(tag, sizeof tag, "%s", ISAACLUA_VERSION_TEXT);
    if (g_autotest_only[0]) {
        tag[n++] = '/';
        for (const char *p = g_autotest_only; *p && n < sizeof tag - 1; p++)
            if ((*p >= '0' && *p <= '9') || *p == ',') tag[n++] = *p;
        tag[n] = 0;
    }
    return tag;
}
static int s_batch, s_batches;  // this launch's batch (1..), how many; 0: no batches
static char *s_batch_on;        // its mods, one folder name per line
/* written at once when the save can be mounted early, else kept until the game mounts it */
static char s_sum_pending[2048], s_state_pending[160];
static bool summary_write(const char *add) {
    char *old = isaaclua_save_load_early("autotest_summary.txt");
    size_t a = old ? strlen(old) : 0, b = strlen(add);
    char *buf = (char *)malloc(a + b + 1);
    bool ok = false;
    if (buf) {
        if (a) memcpy(buf, old, a);
        memcpy(buf + a, add, b);
        ok = isaaclua_save_store_early("autotest_summary.txt", buf, a + b);
        free(buf);
    }
    free(old);
    return ok;
}
static void summary_add(const char *line) {
    size_t n = strlen(s_sum_pending);
    snprintf(s_sum_pending + n, sizeof s_sum_pending - n, "%s\n", line);
    if (summary_write(s_sum_pending)) s_sum_pending[0] = 0;
}
static void state_set(int batch, const char *what) {  // "<batch> <what> <version>": another version starts over
    snprintf(s_state_pending, sizeof s_state_pending, "%d %s %s\n", batch, what, state_tag());
    if (isaaclua_save_store_early("autotest_state.txt", s_state_pending, strlen(s_state_pending))) s_state_pending[0] = 0;
}
static void autotest_flush_pending(void) {  // the save data is mounted
    if (s_state_pending[0] && isaaclua_save_store("autotest_state.txt", s_state_pending, strlen(s_state_pending)))
        s_state_pending[0] = 0;
    if (s_sum_pending[0] && summary_write(s_sum_pending)) s_sum_pending[0] = 0;
}
/* in every load_mods: picks this launch's batch once the save data can be read (the user is known only after
 * KAGE::System::Manager::Initialize, later than the plugin's start; force: pick anyway), and switches the other
 * mods off in on[] */
static void autotest_pick_batch(const nnfs_DirectoryEntry *ents, s64 count, bool *on, bool force) {
    static bool done;
    if (done && s_batch_on) {
        for (s64 i = 0; i < count; i++) on[i] = on[i] && name_in_list(s_batch_on, ents[i].name);
        return;
    }
    if (done || !g_autotest || g_autotest_batch_size <= 0) return;
    if (!force && !isaaclua_save_ready_early()) return;
    done = true;
    int eligible = 0;
    for (s64 i = 0; i < count; i++) eligible += on[i];
    s_batches = (eligible + g_autotest_batch_size - 1) / g_autotest_batch_size;
    char *st = isaaclua_save_load_early("autotest_state.txt"), what[16] = "", ver[96] = "";
    int k = 1;
    if (st) {
        char *e;
        k = (int)strtol(st, &e, 10);
        while (*e == ' ') e++;
        size_t w = strcspn(e, " \n");
        snprintf(what, sizeof what, "%.*s", (int)w, e);
        e += w;
        while (*e == ' ') e++;
        snprintf(ver, sizeof ver, "%.*s", (int)strcspn(e, " \n"), e);
        if (strcmp(ver, state_tag()) != 0) {  // left by another version or batch list: start over
            char line[160];
            snprintf(line, sizeof line, "isaaclua %s: starting over", state_tag());
            summary_add(line);
            free(st), st = NULL, k = 1, what[0] = 0;
        }
    }
    if (st && strcmp(what, "running") == 0) {
        char line[96];
        snprintf(line, sizeof line, "batch %d: CRASHED or stopped (autotest_%02d.log)", k, k);
        summary_add(line);
        k++;
    }
    free(st);
    if (k < 1) k = 1;
    while (k <= s_batches && !batch_wanted(k)) k++;
    if (k > s_batches) {  // all done: a normal game with every mod, no autotest
        if (!what[0] || strcmp(what, "finished") != 0) summary_add("all batches done"), state_set(k, "finished");
        g_autotest = 0, s_batches = 0;
        return;
    }
    s_batch = k;
    size_t cap = 64;
    for (s64 i = 0; i < count; i++) cap += strlen(ents[i].name) + 2;
    s_batch_on = (char *)calloc(1, cap);
    char *line = (char *)malloc(cap);
    size_t n = 0;
    int idx = 0;
    if (line) n = (size_t)snprintf(line, cap, "batch %d/%d start:", k, s_batches);
    for (s64 i = 0; i < count; i++) {
        if (!on[i]) continue;
        bool mine = idx++ / g_autotest_batch_size == k - 1;
        on[i] = mine;
        if (!mine) continue;
        if (s_batch_on) strcat(s_batch_on, ents[i].name), strcat(s_batch_on, "\n");
        if (line) n += (size_t)snprintf(line + n, cap - n, " %s", ents[i].name);
    }
    if (line) summary_add(line), free(line);
    state_set(k, "running");
    snprintf(s_alog_file, sizeof s_alog_file, "autotest_%02d.log", k);
}
int autotest_batch(const char **names, int *count) {
    *names = s_batch_on ? s_batch_on : "";
    *count = s_batches;
    return s_batch;
}
void autotest_next(const char *result) {  // the batch played its time: note it, relaunch with the next one
    char line[300];
    snprintf(line, sizeof line, "batch %d done: %s", s_batch, result);
    summary_add(line);
    if (!s_batch) return;
    state_set(s_batch + 1, "done");
    nnoe_RequestToRelaunchApplication();
}
#endif

#ifdef ISAACLUA_DEBUG
/* remote control (src/remote.lua): the mods on the card and their switches, and a relaunch to apply them */
static int c_remote_mods(lua_State *l) {  // core.remote_mods() -> {{name =, on =}, ...}
    nnfs_DirectoryHandle d;
    lua_newtable(l);
    if (nnfs_OpenDirectory(&d, "rom:/mods", NNFS_DIR_DIRECTORIES) != 0) return 1;
    enum { kMax = 512 };
    nnfs_DirectoryEntry *ents = (nnfs_DirectoryEntry *)calloc(kMax, sizeof *ents);
    s64 count = 0;
    if (!ents || nnfs_ReadDirectory(&count, ents, d, kMax) != 0) count = 0;
    nnfs_CloseDirectory(d);
    sort_mods(ents, count);
    char *off = isaaclua_save_load_early(kModsOffFile);
    for (s64 i = 0; i < count; i++) {
        char path[820];
        snprintf(path, sizeof path, "rom:/mods/%s/disable.it", ents[i].name);
        FILE *f = fopen(path, "r");
        if (f) fclose(f);
        lua_createtable(l, 0, 2);
        lua_pushstring(l, ents[i].name);
        lua_setfield(l, -2, "name");
        lua_pushboolean(l, !f && !name_in_list(off, ents[i].name));
        lua_setfield(l, -2, "on");
        lua_rawseti(l, -2, (lua_Integer)i + 1);
    }
    free(off);
    free(ents);
    return 1;
}
static void mod_switched(const char *name, bool off);
/* core.remote_switch(name, on): what the Mods menu does when a mod is switched (the entry and the saved list too) */
static u8 **mod_entries(u8 ***end);
static const char *entry_name(const u8 *entry);
static void mods_off_save(void);
static int c_remote_switch(lua_State *l) {
    const char *name = luaL_checkstring(l, 1);
    bool on = lua_toboolean(l, 2);
    u8 **e, **b = mod_entries(&e);
    for (u8 **i = b; b && i < e; i++)
        if (strcmp(entry_name(*i), name) == 0) {
            (*i)[312] = on;  /* kModEntry_On */
            mod_switched(name, !on);
            mods_off_save();
            lua_pushboolean(l, 1);
            return 1;
        }
    return 0;
}
static int c_remote_set_off(lua_State *l) {  // core.remote_set_off("name\nname\n"): the Mods menu's switches
    size_t n;
    const char *t = luaL_checklstring(l, 1, &n);
    lua_pushboolean(l, isaaclua_save_store(kModsOffFile, t, n));
    return 1;
}
static int c_remote_relaunch(lua_State *l) {  // core.remote_relaunch(): the game starts again (mods load anew)
    (void)l;
    nnoe_RequestToRelaunchApplication();
    return 0;
}
static int c_remote_log(lua_State *l) {  // core.remote_log(): errors.log as it is now (this launch)
    lua_pushlstring(l, s_log, s_log_n);
    return 1;
}
int remote_shot(lua_State *L), remote_fs(lua_State *L), remote_rm(lua_State *L), remote_sd(lua_State *L),
    remote_put(lua_State *L), remote_get(lua_State *L), remote_mv(lua_State *L);  // remote.c
static void remote_open(lua_State *l) {
    lua_pushcfunction(l, c_remote_log);
    lua_setfield(l, -2, "remote_log");
    lua_pushcfunction(l, c_remote_mods);
    lua_setfield(l, -2, "remote_mods");
    lua_pushcfunction(l, c_remote_set_off);
    lua_setfield(l, -2, "remote_set_off");
    lua_pushcfunction(l, c_remote_switch);
    lua_setfield(l, -2, "remote_switch");
    lua_pushcfunction(l, c_remote_relaunch);
    lua_setfield(l, -2, "remote_relaunch");
    lua_pushcfunction(l, remote_shot);
    lua_setfield(l, -2, "remote_shot");
    lua_pushcfunction(l, remote_fs);
    lua_setfield(l, -2, "remote_fs");
    lua_pushcfunction(l, remote_rm);
    lua_setfield(l, -2, "remote_rm");
    lua_pushcfunction(l, remote_sd);
    lua_setfield(l, -2, "remote_sd");
    lua_pushcfunction(l, remote_put);
    lua_setfield(l, -2, "remote_put");
    lua_pushcfunction(l, remote_get);
    lua_setfield(l, -2, "remote_get");
    lua_pushcfunction(l, remote_mv);
    lua_setfield(l, -2, "remote_mv");
}
#endif

static bool s_defer_lua;  // the mods' Lua waits for the game's own mod content (hk_manager_load_configs)
static bool s_defer_fs;   // their resources too (debug: the autotest's batch is known only then)
/* fs: register the mods' resources; lua: run their main.lua */
static int load_mods(bool fs, bool lua) {
    nnfs_DirectoryHandle d;
    if (nnfs_OpenDirectory(&d, "rom:/mods", NNFS_DIR_DIRECTORIES) != 0) return -1;
    enum { kMax = 512 };
    nnfs_DirectoryEntry *ents = (nnfs_DirectoryEntry *)calloc(kMax, sizeof *ents);
    s64 count = 0;
    if (!ents || nnfs_ReadDirectory(&count, ents, d, kMax) != 0) count = 0;
    nnfs_CloseDirectory(d);
    sort_mods(ents, count);
    /* as on PC, require() finds modules in every enabled mod (its own first): EID and others load
     * Mod Config Menu with require("scripts.modconfig") before MCM's own turn comes */
    bool *on = (bool *)calloc((size_t)count + 1, sizeof *on);
    size_t cap = 1;
    char *off = isaaclua_save_load_early(kModsOffFile);  // the Mods menu's switches
    for (s64 i = 0; on && i < count; i++) {
        char path[820];
        snprintf(path, sizeof path, "rom:/mods/%s/disable.it", ents[i].name);  // same switch as on PC
        FILE *f = fopen(path, "r");
        if (f) fclose(f);
        else if (!name_in_list(off, ents[i].name)) on[i] = true, cap += 2 * strlen(ents[i].name) + 48;
    }
    free(off);
#ifdef ISAACLUA_DEBUG
    if (on) autotest_pick_batch(ents, count, on, lua);
    /* remote build: a launch that died while loading the mods (too many for the memory) is followed by one
     * with every mod off, so the console still reaches the relay and the mods can be switched from there */
    static int s_boot_check;  // 0 not yet, 1 mods as switched, 2 all off (decided once per launch)
    if (on && s_remote && !s_boot_check && isaaclua_save_ready_early()) {
        char *b = isaaclua_save_load_early("remote_boot.txt");
        s_boot_check = b && strncmp(b, "loading", 7) == 0 ? 2 : 1;
        free(b);
        if (s_boot_check == 2)
            overlay_add("remote: the last launch died loading the mods, all mods off this time", OVERLAY_WARN);
        isaaclua_save_store_early("remote_boot.txt", "loading\n", 8);
    }
    if (on && s_boot_check == 2)
        for (s64 i = 0; i < count; i++) on[i] = false;
#endif
    char *all = on ? (char *)malloc(cap) : NULL;
    size_t n = 0;
    for (s64 i = 0; all && i < count; i++)
        if (on[i])
            n += (size_t)snprintf(all + n, cap - n, "%srom:/mods/%s/?.lua;rom:/mods/%s/?/init.lua", n ? ";" : "",
                                  ents[i].name, ents[i].name);
    if (all) all[n] = 0;
    int loaded = 0;
    for (s64 i = 0; on && i < count; i++) {
        if (!on[i]) continue;
        char dir[800];
        snprintf(dir, sizeof dir, "rom:/mods/%s/", ents[i].name);
        if (fs) modfs_add_mod(dir);  // resources/ is used even if the mod's Lua fails, as on PC
        if (!lua) continue;
        lua_rawgeti(L, LUA_REGISTRYINDEX, s_loadmod_ref);
        lua_pushstring(L, dir);
        lua_pushstring(L, ents[i].name);
        lua_pushstring(L, all ? all : "");
        if (lua_pcall(L, 3, 1, 0) != LUA_OK) overlay_add(lua_tostring(L, -1), OVERLAY_ERROR);
        else if (lua_toboolean(L, -1)) loaded++;
        lua_pop(L, 1);
    }
    free(all);
    free(on);
    free(ents);
    return loaded;
}

static void *s_lua_thread;  // the game's main thread, where mods load and every callback runs
static bool lua_start(void) {
    s_lua_thread = nnosGetCurrentThread();
    L = lua_newstate(l_alloc, NULL);
    /* GC pause: 120 (a cycle at 1.2x the live size) kept the shared heap small before the 512 MB budget
     * (heap patch, 0.99.38), but ran the collector nearly all the time: on the console a room with Egor's
     * mods dropped to 48-57 fps with 15-20 ms Lua spikes at 120 and held 60 fps at Lua's default 200
     * (live Lua heap 50-110 MB, ~300 MB free) */
    if (L) lua_gc(L, LUA_GCSETPAUSE, 200);
    if (!L) return false;
    static const luaL_Reg libs[] = {
        {"_G", luaopen_base},          {LUA_LOADLIBNAME, luaopen_package}, {LUA_COLIBNAME, luaopen_coroutine},
        {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},
        {LUA_UTF8LIBNAME, luaopen_utf8}, {LUA_DBLIBNAME, luaopen_debug},
#if defined(LUA_COMPAT_BITLIB)
        {LUA_BITLIBNAME, luaopen_bit32},
#endif
        {NULL, NULL},
    };
    for (const luaL_Reg *lib = libs; lib->func; lib++) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }
    /* no C modules here: drop package.cpath and its two searchers (their "./x.so" paths crashed nn::fs) */
    lua_getglobal(L, "package");
    lua_pushliteral(L, "");
    lua_setfield(L, -2, "cpath");
    lua_getfield(L, -1, "searchers");
    lua_pushnil(L);
    lua_rawseti(L, -2, 4);
    lua_pushnil(L);
    lua_rawseti(L, -2, 3);
    lua_pop(L, 2);
    static const luaL_Reg os[] = {{"time", os_time}, {"clock", os_clock}, {"difftime", os_difftime}, {NULL, NULL}};
    luaL_newlib(L, os);
    lua_setglobal(L, "os");

    api_open(L);

    if (luaL_loadbuffer(L, (const char *)kPreludeLua, sizeof kPreludeLua - 1, "=prelude") != LUA_OK) {
        overlay_add(lua_tostring(L, -1), OVERLAY_ERROR);
        return false;
    }
    static const luaL_Reg core[] = {{"log", core_log}, {"error", core_error}, {"readfile", core_readfile},
                                    {"callback_supported", core_supported}, {"callback_keys", core_callback_keys}, {"repplus", core_repplus},
                                    {"repplus_proc_item", core_rp_proc_item}, {"repplus_je", core_rp_je}, {NULL, NULL}};
    luaL_newlib(L, core);
    native_open(L);
#ifdef ISAACLUA_DEBUG
    remote_open(L);
#endif
    lua_getfield(L, LUA_REGISTRYINDEX, "isaaclua.game");
    lua_setfield(L, -2, "game");
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        overlay_add(lua_tostring(L, -1), OVERLAY_ERROR);
        return false;
    }
    lua_getfield(L, -1, "LoadMod");
    s_loadmod_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_getfield(L, -1, "Run");
    s_run_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_getfield(L, -1, "PerfReport");
    s_perf_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_getfield(L, -1, "SetModOff");
    s_modoff_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_getfield(L, -1, "EnableMod");
    s_enablemod_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pop(L, 1);
    lua_getglobal(L, "Vector");
    api_set_vector_ctor(L);

    /* the resources are scanned now, before the game loads its HUD and menus (the AnmCache keeps what it read:
     * HW 0.99.16, scanned later, Minimap API's minimap1.anm2 stayed the game's), with the Mods menu's switches
     * (mods_off.txt in the save: the console's only user, early_uid); when they can't be read yet (several
     * users), at Manager::LoadConfigs, or a switched-off mod's files would be taken (HW 0.99.15: Good Trip's) */
    s_defer_fs = s_defer_lua && !isaaclua_save_ready_early();
    int n = s_defer_fs ? 0 : load_mods(true, !s_defer_lua);
    if (s_defer_lua && n >= 0) return true;  // the message comes with the Lua, after the game's mod content
    s_defer_lua = false;
    char msg[96];
    if (n < 0) snprintf(msg, sizeof msg, "isaaclua v%s (%s), no rom:/mods folder", ISAACLUA_VERSION_TEXT, LUA_RELEASE);
    else snprintf(msg, sizeof msg, "isaaclua v%s (%s), %d mod(s) loaded", ISAACLUA_VERSION_TEXT, LUA_RELEASE, n);
    overlay_add(msg, OVERLAY_INFO);
    return true;
}

/* ------------------------------------------------------------ the game's own mod content
 * The Switch build keeps the PC ModManager (ListMods, LoadConfigs: items, players, costumes, sounds,
 * music, challenges, pocket items, wisps... from each mod's content/), only nothing calls it and the
 * mods folder path is empty. Manager::Init calls Manager::LoadConfigs once at boot: after it we point
 * g_ModdingDataPath at rom:/mods/, list and load the mods there as ModManager::Reset does on PC, then
 * run the mods' Lua (on PC scripts also run after the content, so Isaac.GetItemIdByName sees mod items).
 * The pool merges PC does from ItemPool::Init / BossPool::Init are done by the hooks below. */
float powf(float, float);
float sqrtf(float);
typedef void (*VoidFn)(void *);
bool g_native_content = true;  // isaaclua.ini content = 0 turns it off
static VoidFn s_manager_load_configs, s_mm_list, s_mm_load, s_mm_item_pools, s_mm_boss_pools, s_mm_curses;
static int s_content_mods = -1;
static u8 *mod_manager(void) {
    u8 *mgr = manager_ptr();
    return mgr ? mgr + 0x36800 : NULL;  // Manager::Manager constructs it there
}
static void lua_load_deferred(void) {
    if (!s_defer_lua || !L) return;
    s_defer_lua = false;
    int n = load_mods(s_defer_fs, true);  // the resources were scanned in hk_manager_load_configs, if it came
    s_defer_fs = false;
    char msg[128];
    if (s_content_mods >= 0)
        snprintf(msg, sizeof msg, "isaaclua v%s (%s), %d mod(s) loaded, content of %d", ISAACLUA_VERSION_TEXT, LUA_RELEASE, n,
                 s_content_mods);
    else snprintf(msg, sizeof msg, "isaaclua v%s (%s), %d mod(s) loaded", ISAACLUA_VERSION_TEXT, LUA_RELEASE, n);
    overlay_add(msg, OVERLAY_INFO);
}
static void hk_manager_load_configs(void *mgr) {
    s_manager_load_configs(mgr);
    char *data = (char *)api_sym("_ZN15IsaacRepentance17g_ModdingDataPathE");
    char *save = (char *)api_sym("_ZN15IsaacRepentance21g_ModdingSaveDataPathE");
    u8 *mm = mod_manager();
    if (g_native_content && data && save && mm && s_mm_list && s_mm_load) {
        nnfs_DirectoryHandle d;
        if (nnfs_OpenDirectory(&d, "rom:/mods/", NNFS_DIR_DIRECTORIES) == 0) {  // nothing to list otherwise
            nnfs_CloseDirectory(d);
            strcpy(data, "rom:/mods/");
            strcpy(save, "rom:/mods/");  // per-mod save folder: read-only here, mods save via isaaclua
            ((u8 *)mgr)[0x36519] = 1;  // "mods on" (Menu_Mods): ListMods enables a mod only with it
            s_mm_list(mm);
            /* the Mods menu's switches (ListMods only knows disable.it): ModEntry +312 on, +313 on and content
             * taken (LoadConfigs reads only those). Read from the save before the game mounts it. */
            char *off = isaaclua_save_load_early(kModsOffFile);
#ifdef ISAACLUA_DEBUG
            if (s_defer_fs) load_mods(false, false);  // the user is known now: pick the autotest's batch
            bool filter = off || s_batch_on;
#else
            bool filter = off;
#endif
            for (u8 **i = *(u8 ***)mm, **e = *(u8 ***)(mm + 8); filter && i && i < e; i++) {
                const u8 *str = *i;
                char name[256];
                size_t len = (str[0] & 1) ? *(const size_t *)(str + 8) : (size_t)(str[0] >> 1);
                if (len >= sizeof name) continue;
                memcpy(name, (str[0] & 1) ? *(const char *const *)(str + 16) : (const char *)str + 1, len);
                name[len] = 0;
                if (off && name_in_list(off, name)) (*i)[312] = 0, (*i)[313] = 0;
#ifdef ISAACLUA_DEBUG
                if (s_batch_on && !name_in_list(s_batch_on, name)) (*i)[312] = 0, (*i)[313] = 0;
#endif
            }
            free(off);
            /* the mods' resources, now that the switches are known (before the game loads anything of theirs) */
            if (s_defer_fs) load_mods(true, false), s_defer_fs = false;
            s_mm_load(mm);
            if (s_mm_curses) s_mm_curses(mm);
            u8 **b = *(u8 ***)mm, **e = *(u8 ***)(mm + 8);
            s_content_mods = 0;
            for (u8 **i = b; b && i < e; i++) s_content_mods += (*i)[313] != 0;
            /* _isaaclua_content_dirs: the folders of the mods whose content/ the game took, in its order
             * (api.lua numbers their sounds and music as the game does). ModEntry +0: std::string name */
            if (L) {
                lua_newtable(L);
                int k = 0;
                for (u8 **i = b; b && i < e; i++) {
                    if (!(*i)[313]) continue;
                    const u8 *str = *i;
                    if (str[0] & 1) lua_pushlstring(L, *(const char **)(str + 16), *(const size_t *)(str + 8));
                    else lua_pushlstring(L, (const char *)str + 1, str[0] >> 1);
                    lua_rawseti(L, -2, ++k);
                }
                lua_setglobal(L, "_isaaclua_content_dirs");
                /* REPENTANCE_PLUS like the PC Repentance+ main.lua, while our Repentance+ mod (src/repplus.lua)
                 * is on: mods take their REP+ branch (MinimapAPI, EID...). Set before any mod's main.lua */
                for (u8 **i = b; b && i < e; i++) {
                    const u8 *str = *i;
                    size_t n = (str[0] & 1) ? *(const size_t *)(str + 8) : (size_t)(str[0] >> 1);
                    const char *nm = (str[0] & 1) ? *(const char **)(str + 16) : (const char *)str + 1;
                    if ((*i)[312] && n == 13 && memcmp(nm, "isaac-repplus", 13) == 0) {
                        lua_pushboolean(L, 1);
                        lua_setglobal(L, "REPENTANCE_PLUS");
                        if (lua_getglobal(L, "_isaaclua_repplus_api") == LUA_TFUNCTION) {
                            if (lua_pcall(L, 0, 0, 0) != LUA_OK) overlay_add(lua_tostring(L, -1), OVERLAY_ERROR), lua_pop(L, 1);
                        } else lua_pop(L, 1);
                    }
                }
            }
        }
    }
    lua_load_deferred();
}
static void hk_write_metadata(void *entry) { (void)entry; }  // the mods folder is read-only (romfs)
/* the mods' itempools.xml / bosspools.xml go in right after the game's own, as on PC: the game's load
 * (from ItemPool::Init / BossPool::Init) is followed by ModManager's merge of every mod's file */
typedef void (*LoadItemPoolsFn)(void *, const char *, bool);
static LoadItemPoolsFn s_load_item_pools;
static int s_pools_depth;
static void hk_load_item_pools(void *pool, const char *path, bool merge) {
    s_load_item_pools(pool, path, merge);
    if (s_pools_depth == 0 && s_content_mods > 0 && s_mm_item_pools) {
        s_pools_depth++;
        s_mm_item_pools(mod_manager());
        s_pools_depth--;
    }
}
typedef void (*LoadBossPoolsFn)(void *, const char *, void *);
static LoadBossPoolsFn s_load_boss_pools;
static void hk_load_boss_pools(void *pool, const char *path, void *mod) {
    s_load_boss_pools(pool, path, mod);
    if (!mod && s_pools_depth == 0 && s_content_mods > 0 && s_mm_boss_pools) {
        s_pools_depth++;
        s_mm_boss_pools(mod_manager());
        s_pools_depth--;
    }
}
/* mod rooms (content/rooms/<stage>.stb): the Switch build keeps ModManager::UpdateRooms but never calls it. On PC
 * RoomConfig::LoadStageBinary (which unloads and rereads the stage's own rooms) ends with it: same here. Mode -1
 * is resolved as LoadStageBinary does: greed when the game's difficulty (Game+0x2f0028) is 2 or 3. */
typedef void (*LoadStageFn)(void *, u32, int);
static LoadStageFn s_load_stage_binary, s_mm_rooms;
/* A stage read before the mods' content is listed or before the Game exists (the special rooms, stage 0,
 * can be read that early) gets the mods' rooms on the next Level::Init, before the floor is generated:
 * without it `goto s.dice.<mod variant>` finds nothing (Andromeda's abandoned planetarium, HW 0.64). */
enum { STAGES_MAX = 64 };
static u8 s_rooms_pending[STAGES_MAX];  // bit (mode + 1): -1 unresolved, 0 normal, 1 greed
static void hk_load_stage_binary(void *rc, u32 stage, int mode) {
    s_load_stage_binary(rc, stage, mode);
    if (!s_mm_rooms || s_content_mods == 0) return;
    if (s_content_mods < 0 || !game_ptr()) {  // UpdateRooms reads the Game's RoomConfig
        if (stage < STAGES_MAX && mode >= -1 && mode <= 1) s_rooms_pending[stage] |= (u8)(1u << (mode + 1));
        return;
    }
    if (mode == -1) mode = (*(u32 *)(game_ptr() + 0x2f0028) & ~1u) == 2;
    if (stage < STAGES_MAX) s_rooms_pending[stage] &= (u8)~(1u << (mode + 1));
    s_mm_rooms(mod_manager(), stage, mode);
}
static void flush_pending_rooms(void) {
    if (s_content_mods <= 0 || !s_mm_rooms || !game_ptr()) return;
    for (u32 st = 0; st < STAGES_MAX; st++) {
        u8 m = s_rooms_pending[st];
        if (!m) continue;
        s_rooms_pending[st] = 0;
        for (int mode = -1; mode <= 1; mode++) {
            if (!(m >> (mode + 1) & 1)) continue;
            int md = mode == -1 ? (*(u32 *)(game_ptr() + 0x2f0028) & ~1u) == 2 : mode;
            if (mode == -1 && (m >> (md + 1) & 1)) continue;  // the same mode is also pending: once
            s_mm_rooms(mod_manager(), st, md);
        }
    }
}
int isaaclua_content_mods(void) { return s_content_mods; }

/* ------------------------------------------------------------ engine hooks */

typedef void (*VoidFn)(void *);
static VoidFn s_game_update, s_hud_render, s_evaluate_items;
static bool s_have_game_update = false;
static const ModView *s_rep_for_late;  // Repentance.nro, for the hooks installed on the first update
static ModView s_rep;

/* nn::hid::GetNpadState, one hook per state type (same layout): the keyboard sees and filters the pads */
typedef void (*NpadFn)(void *state, const u32 *id);
void nnhid_GetNpadStateFullKey(void *, const u32 *) __asm__("_ZN2nn3hid12GetNpadStateEPNS0_16NpadFullKeyStateERKj");
void nnhid_GetNpadStateJoyDual(void *, const u32 *) __asm__("_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyDualStateERKj");
void nnhid_GetNpadStateJoyLeft(void *, const u32 *) __asm__("_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyLeftStateERKj");
void nnhid_GetNpadStateHandheld(void *, const u32 *) __asm__("_ZN2nn3hid12GetNpadStateEPNS0_17NpadHandheldStateERKj");
void nnhid_GetNpadStateJoyRight(void *, const u32 *) __asm__("_ZN2nn3hid12GetNpadStateEPNS0_17NpadJoyRightStateERKj");
enum { NPAD_KINDS = 5 };
static NpadFn s_npad_orig[NPAD_KINDS];
#define NPAD_HOOK(i) \
    static void hk_npad##i(void *st, const u32 *id) { \
        s_npad_orig[i](st, id); \
        kb_filter_npad(st, *id); \
    }
NPAD_HOOK(0)
NPAD_HOOK(1)
NPAD_HOOK(2)
NPAD_HOOK(3)
NPAD_HOOK(4)

static int install_npad_hooks(const ModView *v) {
    static const char *const kSyms[NPAD_KINDS] = {
        "_ZN2nn3hid12GetNpadStateEPNS0_16NpadFullKeyStateERKj",
        "_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyDualStateERKj",
        "_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyLeftStateERKj",
        "_ZN2nn3hid12GetNpadStateEPNS0_17NpadHandheldStateERKj",
        "_ZN2nn3hid12GetNpadStateEPNS0_17NpadJoyRightStateERKj",
    };
    static void (*const kHooks[NPAD_KINDS])(void *, const u32 *) = {hk_npad0, hk_npad1, hk_npad2, hk_npad3, hk_npad4};
    /* the SDK functions themselves, bound for us by rtld (the game's slots may not be bound yet) */
    static const NpadFn kReal[NPAD_KINDS] = {nnhid_GetNpadStateFullKey, nnhid_GetNpadStateJoyDual,
                                             nnhid_GetNpadStateJoyLeft, nnhid_GetNpadStateHandheld,
                                             nnhid_GetNpadStateJoyRight};
    int n = 0;
    for (int i = 0; i < NPAD_KINDS; i++) {
        s_npad_orig[i] = (NpadFn)hook_got_to(v, kSyms[i], (void *)kHooks[i], (void *)kReal[i]);
        n += s_npad_orig[i] != NULL;
    }
    return n;
}

/* Run-level events, detected after each game update. */
static bool s_in_run;  // a run is going (game_tick saw it, Game::Exit not yet)
/* Manager::execute_start_game makes every run: Game::Init, then Game::StartFromSavedState (continue) or another
 * Start*. With these hooks a run starts only there: a frame count going back (Glowing Hourglass, or after a mod's
 * goto on HW 0.70) is not a new run, as on PC. Without them the frame count is the signal. */
static VoidFn s_game_init;
static bool s_start_pending, s_start_continued;
static void hk_game_init(void *g) { s_game_init(g); s_start_pending = true, s_start_continued = false; }
typedef void (*StartSavedFn)(void *, const void *);
static StartSavedFn s_start_saved;
static void hk_start_saved(void *g, const void *state) { s_start_saved(g, state); s_start_continued = true; }
/* one game update ends: fold its Lua time in, and once a second build the line overlay_draw shows */
static void perf_update(void) {
    if (!g_perf || !L) return;
    s64 now = nnosGetSystemTick(), f = nnosGetSystemTickFrequency();
    if (f <= 0) return;
    s_perf_lua += s_perf_frame_lua;
    if (s_perf_frame_lua > s_perf_frame_max) s_perf_frame_max = s_perf_frame_lua;
    s_perf_frame_lua = 0;
    s_perf_updates++;
    if (!s_perf_window) s_perf_window = now;
    if (now - s_perf_window < f) return;
    double ms = (double)f / 1000.0, secs = (double)(now - s_perf_window) / (double)f;
    char mods[120] = "";
    int top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, s_perf_ref);
    lua_pushnumber(L, ms);
    lua_pushinteger(L, s_perf_updates);
    if (lua_pcall(L, 2, 1, 0) == LUA_OK && lua_isstring(L, -1)) snprintf(mods, sizeof mods, "%s", lua_tostring(L, -1));
    lua_settop(L, top);
    snprintf(s_perf_line, sizeof s_perf_line, "%.0f upd/s %.0f fps | lua %.1f ms/upd, max %.1f | %d MB | %s",
             s_perf_updates / secs, s_perf_renders / secs, s_perf_lua / ms / s_perf_updates, s_perf_frame_max / ms,
             lua_gc(L, LUA_GCCOUNT, 0) / 1024, mods);
    s_perf_window = now, s_perf_lua = s_perf_frame_max = 0, s_perf_updates = s_perf_renders = 0;
}

/* Level::Init (GOT): the new floor's rooms exist. The stage number changes earlier, when the stage
 * transition starts (StageTransition::Start -> Level::SetStage), while the old floor is still there:
 * POST_NEW_LEVEL waits for Level::Init, as on PC. Without the hook, the stage number is the signal. */
/* Game::End(ending): the game over screen (ending 1, as Game:End(1) on PC) or an ending starts.
 * POST_GAME_END(isGameOver) */
typedef void (*GameEndFn)(void *, int);
/* MC_PRE_SPAWN_CLEAN_AWARD(rng, spawn position): true skips the room's clear reward */
typedef void (*RoomFn)(void *);
static RoomFn s_clear_award;
static void hk_clear_award(void *room) {
    if (s_cb_used[MC_PRE_SPAWN_CLEAN_AWARD]) {
        Vector2 pos = {320, 280};
        Vector2 (*center)(const void *) = (Vector2 (*)(const void *))api_sym("_ZNK15IsaacRepentance4Room12GetCenterPosEv");
        if (room && center) pos = center(room);
        if (run_args(MC_PRE_SPAWN_CLEAN_AWARD, false, 0, "rv", &pos)) return;
    }
    s_clear_award(room);
}
/* MC_POST_NPC_DEATH(npc): Entity_NPC::trigger_death_remove (from Entity_NPC::Update, when the death is
 * carried out), run before it so the NPC is still whole */
static VoidFn s_npc_death;
static void hk_npc_death(void *e) {
    run(MC_POST_NPC_DEATH, true, ENT_I32(e, kEnt_Type), (u8 *)e, 0, 0);
    s_npc_death(e);
}
static GameEndFn s_game_end;
static void hk_game_end(void *g, int ending) {
    s_game_end(g, ending);
    run(MC_POST_GAME_END, false, 0, NULL, ending == 1, 2);
}
static VoidFn s_level_init;
static bool s_level_inited;
/* Level::Init: a new floor. POST_CURSE_EVAL(curses) right after it (on PC during the generation, so a
 * Curse of the Labyrinth added here does not change the layout); a number replaces the floor's curses
 * (u32 Level+0xc, Level::GetCurses) */
static void hk_level_init(void *level) {
    flush_pending_rooms();
    s_level_init(level);
    s_level_inited = true;
    if (!level) return;
    u32 *curses = (u32 *)((u8 *)level + 0xc);
    int top = run_raw(MC_POST_CURSE_EVAL, false, 0, "i", (lua_Integer)*curses);
    if (top >= 0) {
        lua_Integer v;
        if (cb_integer(-1, "MC_POST_CURSE_EVAL", &v)) *curses = (u32)v;
        lua_settop(L, top);
    }
}
/* Level::Init from Lua (the console's reseed): through the hook, so POST_CURSE_EVAL and POST_NEW_LEVEL
 * follow as on PC, where reseed runs the same Level::Init that fires them */
void isaaclua_level_init(void *level) {
    if (s_level_init) { hk_level_init(level); return; }
    VoidFn f = (VoidFn)api_sym("_ZN15IsaacRepentance5Level4InitEv");
    if (f) f(level);
}

/* Level::ChangeRoom(idx, dim) (GOT, from Game::ChangeRoom). With LeaveDoor (Level+0x2156c) set, the game
 * sends the player through that door of the current room (RoomDescriptor +0x28: neighbour index per door slot)
 * instead of to idx, and Level::get_target_slot then reads the neighbour's RoomConfig::Room (+0x10). Mod
 * teleports that leave a stale LeaveDoor can hit a neighbour without room data: a null read, the game crashes
 * (HW 0.63, Andromeda's Gravity Shift: get_target_slot+0x98, room 127). Then LeaveDoor is cleared, as PC mods
 * do before a teleport; a target without room data keeps the player in the current room. */
/* Low memory before a room loads. Every heap allocation (the game's, Lua's) comes from the same malloc, and the
 * game keeps every .anm2 it read for the rest of the run: AnmCache::FreeUnreferencedEntries runs only from the
 * console's clearcache, the mods' reset and the language switch, FreeUnreferencedImages from Level::Init. With
 * big mods a room's spawns then fail an operator new in AnmCache::Preload2 and the game aborts (HW 0.54, 0.67:
 * Room::Init -> Entity_NPC::Init -> AnmCache::Preload2). When a trial allocation of kLowMem fails, the entries
 * and images no sprite uses are freed (what clearcache does) and Lua's garbage is collected. */
enum { kLowMem = 64 << 20 };
#ifndef ISAACLUA_HOST_TEST
static void keep_drop_all(void);
#else
static void keep_drop_all(void) {}
#endif
static bool s_mem_low;  // set by a failed big allocation: the caches are freed at the next safe point
static void relieve_low_memory(void) {
    void *probe = s_mem_low ? NULL : malloc(kLowMem);
    if (probe) { free(probe); return; }
    s_mem_low = false;
    typedef void (*AnmFn)(void *);
    static AnmFn entries, images;
    static u8 *cache;
    if (!cache) {
        cache = (u8 *)api_sym("_ZN15IsaacRepentance10g_AnmCacheE");
        entries = (AnmFn)api_sym("_ZN15IsaacRepentance8AnmCache23FreeUnreferencedEntriesEv");
        images = (AnmFn)api_sym("_ZN15IsaacRepentance8AnmCache22FreeUnreferencedImagesEv");
    }
    if (L) lua_gc(L, LUA_GCCOLLECT, 0);
    keep_drop_all();
    if (cache && entries && images) entries(cache), images(cache);
    static bool told;
    if (!told) told = true, overlay_add("low memory: freed the animation cache and Lua garbage", OVERLAY_WARN);
}
/* The game's operator new / new[] (GOT): a big block that malloc cannot give right now first gets Lua's garbage
 * collected (safe at any point; the animation cache waits for the next frame, an entry may be half-built), then
 * the game's own operator new runs as before (it still aborts if there is no memory at all). Boss fights spawn
 * mid-room (HW 0.69: Entity_NPC::ai_hush -> Game::SpawnEx -> AnmCache::Preload2). */
void *nn_new(size_t) __asm__("_Znwm");
void *nn_new_array(size_t) __asm__("_Znam");
enum { kBigAlloc = 64 << 10 };
static void check_big_alloc(size_t n) {
    if (n < kBigAlloc) return;
    void *p = malloc(n);
    if (p) { free(p); return; }
    if (L) lua_gc(L, LUA_GCCOLLECT, 0);
    s_mem_low = true;
}
static void *hk_new(size_t n) { check_big_alloc(n); return nn_new(n); }
static void *hk_new_array(size_t n) { check_big_alloc(n); return nn_new_array(n); }
typedef void (*ChangeRoomFn)(void *, int, int);
static ChangeRoomFn s_level_change_room;
/* a room was entered: POST_NEW_ROOM on the next tick even when the room's enter frame is the same as the last one
 * (after a Glowing Hourglass rewind the frame count goes back, so the next room can be entered on the same frame
 * number: Andromeda's Gravity Shift, HW 0.71) */
static bool s_room_changed;
static void hk_level_change_room(void *level, int idx, int dim) {
    typedef u8 *(*ByIdxFn)(void *, int, int);
    static ByIdxFn by_idx;
    if (!by_idx) by_idx = (ByIdxFn)api_sym("_ZNK15IsaacRepentance5Level12GetRoomByIdxEii");
    u8 *lv = level;
    if (by_idx && lv) {
        int cur = *(int *)(lv + 0x21558), cur_dim = *(int *)(lv + 0x21560), *leave = (int *)(lv + 0x2156c);
        int to_dim = dim < 0 ? cur_dim : dim;
        char msg[96];
        u8 *t = by_idx(lv, idx, to_dim);
        if (t && !*(u8 **)(t + 0x10)) {
            snprintf(msg, sizeof msg, "ChangeRoom(%d): the room has no data, staying in room %d", idx, cur);
            overlay_add(msg, OVERLAY_WARN);
            idx = cur, dim = cur_dim, to_dim = cur_dim, *leave = -1;
        }
        /* a negative index is an off-grid room (devil/angel -1, secret exit -10, ...) entered through a door
         * of its own (deal door, secret exit door), not the grid neighbour behind that slot: the leave door
         * stays, it puts the way back on the wall the player came from (HW 0.99.87: with it cleared the
         * deal room's door went to another wall) */
        if (idx >= 0 && *leave >= 0 && *leave < 8) {
            u8 *d = by_idx(lv, cur, cur_dim), *data = d ? *(u8 **)(d + 0x10) : NULL;
            if (data && (*(u64 *)(data + 64) >> *leave & 1)) {
                int n = *(int *)(d + 0x28 + 4 * *leave);
                u8 *nd = n >= 0 ? by_idx(lv, n, to_dim) : NULL;
                if (n >= 0 && (!nd || !*(u8 **)(nd + 0x10))) {
                    snprintf(msg, sizeof msg, "ChangeRoom(%d): door %d leads to room %d without data, door cleared",
                             idx, *leave, n);
                    overlay_add(msg, OVERLAY_WARN);
                    *leave = -1;
                }
            }
        }
    }
    relieve_low_memory();
    s_level_change_room(level, idx, dim);
    s_room_changed = true;
}

static u32 s_last_frame, s_last_stage, s_last_enter;
static u8 *s_last_room;
/* PC fires POST_NEW_LEVEL, POST_NEW_ROOM and POST_GAME_STARTED inside Game::Start, before the run's first update:
 * a mod's PEFFECT_UPDATE finds what its GAME_STARTED set up (Fiend Folio's savedata.run, HW 0.99.30). With the
 * Game::Init hook the new run is known before that update, so hk_game_update fires them here first. */
static void game_start_events(void) {
    u8 *g = game_ptr(), *r = room_ptr();
    if (!g || !s_game_init || !s_start_pending) return;
    bool continued = s_start_continued;
    s_start_pending = s_start_continued = false;
    s_level_inited = false;
    run(MC_POST_NEW_LEVEL, false, 0, NULL, 0, 0);
    s_room_changed = false;
    run(MC_POST_NEW_ROOM, false, 0, NULL, 0, 0);
    run_args(MC_POST_GAME_STARTED, false, 0, "b", (int)continued);
    s_in_run = true, s_last_frame = *(u32 *)(g + kGame_FrameCount), s_last_stage = *(u32 *)(g + kGame_Stage);
    s_last_enter = r ? *(u32 *)(r + kRoom_EnterFrame) : 0, s_last_room = r;
}

static void game_tick(void) {
#define last_frame s_last_frame
#define last_stage s_last_stage
#define last_enter s_last_enter
#define last_room s_last_room
    perf_update();
    u8 *g = game_ptr(), *r = room_ptr();
    if (!g) return;
    u32 frame = *(u32 *)(g + kGame_FrameCount), stage = *(u32 *)(g + kGame_Stage);
    static u32 mem_frame;  // also mid-room: once a second, or after a failed new
    if (s_mem_low || frame - mem_frame >= 60) mem_frame = frame, relieve_low_memory();
    u32 enter = r ? *(u32 *)(r + kRoom_EnterFrame) : 0;
    bool new_run = !s_in_run || (s_game_init ? s_start_pending : frame < last_frame);
    bool continued = new_run && s_start_continued;
    s_start_pending = s_start_continued = false;
    bool new_level = s_level_init ? s_level_inited : stage != last_stage;
    s_level_inited = false;
    if (new_run || new_level) run(MC_POST_NEW_LEVEL, false, 0, NULL, 0, 0);
    /* a new level always enters its start room: PC fires POST_NEW_ROOM after every POST_NEW_LEVEL. A reseed on
     * frame 0 (a mod's POST_GAME_STARTED) builds the level again with the same enter frame and Room object, so
     * without new_level here mods kept the old level's room (MinimapAPI's player position: rooms next to a
     * wrong room shown on the map, HW 0.99.127) */
    bool new_room = new_run || new_level || enter != last_enter || r != last_room || s_room_changed;
    s_room_changed = false;
    if (new_room) run(MC_POST_NEW_ROOM, false, 0, NULL, 0, 0);
    if (new_run) run_args(MC_POST_GAME_STARTED, false, 0, "b", (int)continued);
    s_in_run = true, last_frame = frame, last_stage = stage, last_enter = enter, last_room = r;
    run(MC_POST_UPDATE, false, 0, NULL, 0, 0);
    kb_end_update();
#undef last_frame
#undef last_stage
#undef last_enter
#undef last_room
}

#ifdef ISAACLUA_DEBUG
_Bool nnifm_IsNetworkAvailable_real(void) __asm__("_ZN2nn4nifm18IsNetworkAvailableEv");
static _Bool game_network_available(void) { return 0; }
void remote_frame(lua_State *L);
static void remote_booted(void) {  // the first frame after the mods loaded: this launch did not die loading
    static bool done;
    if (done || !nnfs_IsMounted("sdmc")) return;
    done = true;
    isaaclua_save_store("remote_boot.txt", "ok\n", 3);
}
/* src/remote.c's per-frame step, on the game's thread and never inside Lua: from the HUD render in a run,
 * from the pad reads (several per frame, so at most every 16 ms) in the menus */
void isaaclua_remote_tick(bool render) {
    if (!L || nnosGetCurrentThread() != s_lua_thread) return;
    lua_Debug ar;
    if (lua_getstack(L, 0, &ar)) return;
    static s64 last;
    s64 f = nnosGetSystemTickFrequency(), now = f ? nnosGetSystemTick() / (f / 1000) : 0;
    if (!render && now - last < 16) return;
    last = now;
    if (s_remote) remote_booted();
    remote_frame(L);
}
#endif
static void hk_hud_render(void *hud) {
    if (s_defer_lua) lua_load_deferred();  // Manager::LoadConfigs never came: run the Lua now
    s_hud_render(hud);
    g_engine_ready = true;
    g_render_frames++;
    kb_frame();
    g_in_render = true;
    run(MC_POST_RENDER, false, 0, NULL, 0, 0);
    g_in_render = false;
    kb_end_render();
    kb_draw();
    overlay_draw();
#ifdef ISAACLUA_DEBUG
    isaaclua_remote_tick(true);
#endif
    s_perf_renders++;
}

/* Game::Exit(shouldSave): leaving the run (to the menu, or before a new one starts) */
typedef void (*ExitFn)(void *, bool);
static ExitFn s_game_exit;
static bool s_game_exiting;  // inside Game::Exit: the level's entity list is being torn down
static void hk_game_exit(void *g, bool save) {
    if (s_in_run) run_args(MC_PRE_GAME_EXIT, false, 0, "b", (int)save);
    s_in_run = false;
    s_game_exiting = true;
    s_game_exit(g, save);
    s_game_exiting = false;
}

/* Entity_Player::UseActiveItem(item, useFlags, slot, varData): PRE_USE_ITEM (true cancels), then USE_ITEM */
typedef u64 (*UseItemFn)(void *, int, u32, int, int);
static UseItemFn s_use_item;
static u64 hk_use_item(void *p, int item, u32 flags, int slot, int vardata) {
    if (run_args(MC_PRE_USE_ITEM, true, item, "ireiii", (lua_Integer)item, (u8 *)p, (lua_Integer)flags,
                 (lua_Integer)slot, (lua_Integer)vardata))
        return 0;
    u64 r = s_use_item(p, item, flags, slot, vardata);
    /* PC calls MC_USE_ITEM inside UseActiveItem, after the item's effect (0x5a6cb4), and uses what it returns:
     * true (or ShowAnim in a table) holds the item up ("UseItem", unless UseFlag.USE_NOANIM or a co-op baby),
     * Discharge / Remove set the result (here bit 0 discharge, bit 8 remove: control_active_item reads them) */
    int top = run_raw(MC_USE_ITEM, true, item, "ireiii", (lua_Integer)item, (u8 *)p, (lua_Integer)flags,
                      (lua_Integer)slot, (lua_Integer)vardata);
    if (top < 0) return r;
    bool anim = false;
    if (lua_type(L, -1) == LUA_TBOOLEAN) anim = lua_toboolean(L, -1);
    else if (lua_type(L, -1) == LUA_TTABLE) {
        if (lua_getfield(L, -1, "Discharge") != LUA_TNIL) r = (r & ~(u64)1) | (lua_toboolean(L, -1) ? 1 : 0);
        lua_pop(L, 1);
        if (lua_getfield(L, -1, "Remove") != LUA_TNIL) r = (r & ~(u64)0x100) | (lua_toboolean(L, -1) ? 0x100 : 0);
        lua_pop(L, 1);
        if (lua_getfield(L, -1, "ShowAnim") != LUA_TNIL) anim = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    lua_settop(L, top);
    if (anim && !(flags & 1) && ENT_I32(p, kEnt_Variant) == 0) {
        api_push_entity(L, p);
        if (lua_getfield(L, -1, "AnimateCollectible") == LUA_TFUNCTION) {
            lua_insert(L, -2);
            lua_pushinteger(L, item);
            lua_pushstring(L, "UseItem");
            lua_pushstring(L, "PlayerPickupSparkle");
            if (lua_pcall(L, 4, 0, 0) != LUA_OK) overlay_add(lua_tostring(L, -1), OVERLAY_ERROR);
        }
        lua_settop(L, top);
    }
    return r;
}
typedef u64 (*UseCardFn)(void *, int, u32);
static UseCardFn s_use_card;
static u64 hk_use_card(void *p, int card, u32 flags) {
    u64 r = s_use_card(p, card, flags);
    run_args(MC_USE_CARD, true, card, "iei", (lua_Integer)card, (u8 *)p, (lua_Integer)flags);
    return r;
}
typedef u64 (*UsePillFn)(void *, int, int, u32);
static UsePillFn s_use_pill;
static void rp_pre_use_pill(void *p, int effect, int color);  // repplus_hooks.inc
static float rp_player_dmg(void *p, float amount, u64 flags, const void *ref);  // repplus_hooks.inc
static int rp_spawn_extra(uintptr_t lr, void *spawner);  // repplus_hooks.inc
static u64 hk_use_pill(void *p, int effect, int color, u32 flags) {
    rp_pre_use_pill(p, effect, color);
    u64 r = s_use_pill(p, effect, color, flags);
    /* the pill colour as 4th argument, as in REP+'s API (PC 1.7.9b passes 3; extra arguments are ignored) */
    run_args(MC_USE_PILL, true, effect, "ieii", (lua_Integer)effect, (u8 *)p, (lua_Integer)flags, (lua_Integer)color);
    return r;
}

/* ItemPool::GetCollectible(pool, seed, flags, default): POST_GET_COLLECTIBLE(item, pool, decrease, seed), an
 * integer result replaces the item. Flag 1 is "don't decrease" (the item is not removed from the pool). The
 * game calls itself back for some pools: only the outermost call reaches mods, once per roll as on PC. */
typedef int (*GetCollectibleFn)(void *, int, u32, u32, int);
static GetCollectibleFn s_get_collectible;
static int s_get_collectible_depth;
static int hk_get_collectible(void *pool, int type, u32 seed, u32 flags, int def) {
    /* PRE_GET_COLLECTIBLE(pool, decrease, seed): an item id replaces the roll (the pool is not touched) */
    if (s_get_collectible_depth == 0 && s_cb_used[MC_PRE_GET_COLLECTIBLE]) {
        int item = 0;
        s_get_collectible_depth++;
        int top = run_raw(MC_PRE_GET_COLLECTIBLE, false, 0, "ibi", (lua_Integer)type, (int)!(flags & 1),
                          (lua_Integer)seed);
        s_get_collectible_depth--;
        if (top >= 0) {
            lua_Integer v;
            if (cb_integer(-1, "MC_PRE_GET_COLLECTIBLE", &v) && v > 0) item = (int)v;  // PC: any non-0
            lua_settop(L, top);
        }
        if (item > 0) return item;
    }
    s_get_collectible_depth++;
    int item = s_get_collectible(pool, type, seed, flags, def);
    if (--s_get_collectible_depth > 0) return item;
    int top = run_raw(MC_POST_GET_COLLECTIBLE, false, 0, "iibi", (lua_Integer)item, (lua_Integer)type,
                      (int)!(flags & 1), (lua_Integer)seed);
    if (top >= 0) {
        lua_Integer v;
        if (cb_integer(-1, "MC_POST_GET_COLLECTIBLE", &v) && v > 0) item = (int)v;  // PC 0x701865: non-0
        lua_settop(L, top);
    }
    return item;
}

/* ItemPool::GetTrinket(dontAdvance) (GOT): GET_TRINKET(trinket, rng); a trinket id replaces it */
typedef int (*GetTrinketFn)(void *, bool);
static GetTrinketFn s_get_trinket;
static int hk_get_trinket(void *pool, bool keep) {
    int t = s_get_trinket(pool, keep);
    int top = run_raw(MC_GET_TRINKET, false, 0, "ir", (lua_Integer)t);
    if (top >= 0) {
        lua_Integer v;
        if (cb_integer(-1, "MC_GET_TRINKET", &v) && v > 0) t = (int)v;
        lua_settop(L, top);
    }
    return t;
}

/* ItemPool::GetCard(seed, playing, runes, onlyRunes) (GOT): GET_CARD(rng, card, playing, runes, onlyRunes); a card id
 * replaces it */
typedef int (*GetCardFn)(void *, u32, bool, bool, bool);
static GetCardFn s_get_card;
static int hk_get_card(void *pool, u32 seed, bool playing, bool runes, bool only_runes) {
    int c = s_get_card(pool, seed, playing, runes, only_runes);
    int top = run_raw(MC_GET_CARD, false, 0, "ribbb", (lua_Integer)c, (int)playing, (int)runes, (int)only_runes);
    if (top >= 0) {
        lua_Integer v;  // PC 0x801c10: any number replaces the card, 0 (no card) included
        if (cb_integer(-1, "MC_GET_CARD", &v) && v >= 0) c = (int)v;
        lua_settop(L, top);
    }
    return c;
}

/* Entity_Pickup::SelectPickupType(seed, &variant, &subtype, bool, bool) (static, GOT; it calls itself): from
 * Pickup::Init, POST_PICKUP_SELECTION(pickup, variant, subtype) after the outermost call; {variant, subtype}
 * replaces the pick */
static u8 *s_init_pickup;  // the pickup Entity_Pickup::Init is initialising: POST_PICKUP_SELECTION's entity
typedef bool (*SelectPickupFn)(u32, u32 *, u32 *, bool, bool);
static SelectPickupFn s_select_pickup;
static int s_select_depth;
static bool hk_select_pickup(u32 seed, u32 *variant, u32 *subtype, bool a, bool b) {
    s_select_depth++;
    bool r = s_select_pickup(seed, variant, subtype, a, b);
    s_select_depth--;
    if (s_select_depth || !s_init_pickup) return r;
    int top = run_raw(MC_POST_PICKUP_SELECTION, false, 0, "eii", s_init_pickup, (lua_Integer)*variant,
                      (lua_Integer)*subtype);
    if (top >= 0) {
        if (lua_istable(L, -1)) {
            lua_rawgeti(L, -1, 1);
            lua_rawgeti(L, -2, 2);
            lua_Integer v;
            if (cb_integer(-2, "MC_POST_PICKUP_SELECTION", &v)) *variant = (u32)v;
            if (cb_integer(-1, "MC_POST_PICKUP_SELECTION", &v)) *subtype = (u32)v;
        }
        lua_settop(L, top);
    }
    return r;
}
/* ItemPool::GetPill(seed) and GetPillEffect(color, player) (GOT): GET_PILL_COLOR(seed) and
 * GET_PILL_EFFECT(effect, color); a returned id replaces the game's */
typedef int (*GetPillFn)(void *, u32);
typedef int (*GetPillEffectFn)(void *, int, void *);
static GetPillFn s_get_pill;
static GetPillEffectFn s_get_pill_effect;
static int int_result(int top, int v, lua_Integer min) {
    if (top < 0) return v;
    lua_Integer n;
    if (cb_integer(-1, "callback", &n) && n >= min) v = (int)n;
    lua_settop(L, top);
    return v;
}
static int hk_get_pill(void *pool, u32 seed) {
    int c = s_get_pill(pool, seed);
    return int_result(run_raw(MC_GET_PILL_COLOR, false, 0, "i", (lua_Integer)seed), c, 1);
}
static int hk_get_pill_effect(void *pool, int color, void *player) {
    int fx = s_get_pill_effect(pool, color, player);
    return int_result(run_raw(MC_GET_PILL_EFFECT, false, 0, "ii", (lua_Integer)fx, (lua_Integer)color), fx, 0);
}
/* Game::DevolveEnemy(entity) (GOT, D10 / tears): PRE_ENTITY_DEVOLVE(entity) returning true cancels it */
typedef bool (*DevolveFn)(void *, void *);
static DevolveFn s_devolve;
static bool hk_devolve(void *game, void *e) {
    if (run_args(MC_PRE_ENTITY_DEVOLVE, false, 0, "e", (u8 *)e)) return false;
    return s_devolve(game, e);
}

/* Room::spawn_entity(gridIndex, entry, seed, out, bool): the room layout's own entities on the room's first
 * visit. PRE_ROOM_ENTITY_SPAWN(type, variant, subtype, gridIndex, seed); a table {type, variant, subtype}
 * replaces the entry. RoomConfig::Entry = {u32 type, u16 variant, u16 subtype, float weight}. */
typedef struct { u32 type; uint16_t variant, subtype; float weight; } RoomEntry;
typedef u64 (*SpawnEntityFn)(void *, u32, const RoomEntry *, u32, void **, bool);
static SpawnEntityFn s_spawn_entity;
static u64 hk_spawn_entity(void *room, u32 grid, const RoomEntry *e, u32 seed, void **out, bool b) {
    RoomEntry copy = *e;
    int top = run_raw(MC_PRE_ROOM_ENTITY_SPAWN, true, (lua_Integer)e->type, "iiiii", (lua_Integer)e->type,
                      (lua_Integer)e->variant, (lua_Integer)e->subtype, (lua_Integer)grid, (lua_Integer)seed);
    if (top >= 0) {
        if (lua_istable(L, -1)) {
            lua_Integer v[3] = {e->type, e->variant, e->subtype};
            for (int i = 0; i < 3; i++) {
                lua_rawgeti(L, -1, i + 1);  // PC 0x810200: lua_gettable + luaL_checkinteger per field
                cb_integer(-1, "MC_PRE_ROOM_ENTITY_SPAWN", &v[i]);
                lua_pop(L, 1);
            }
            copy.type = (u32)v[0], copy.variant = (uint16_t)v[1], copy.subtype = (uint16_t)v[2];
            e = &copy;
        }
        lua_settop(L, top);
    }
    return s_spawn_entity(room, grid, e, seed, out, b);
}

/* Game::SpawnEx(type, variant, pos, vel, spawner, subtype, seed, flags) (GOT; also behind Game::Spawn):
 * PRE_ENTITY_SPAWN(type, variant, subtype, pos, vel, spawner, seed); a table {type, variant, subtype[, seed]}
 * replaces them. Not for spawns made from inside the callback. */
typedef void *(*SpawnExFn)(void *, u32, u32, const Vector2 *, const Vector2 *, void *, u32, u32, u32);
static SpawnExFn s_spawn_ex;
static int s_spawn_ex_depth;
static void *hk_spawn_ex(void *g, u32 type, u32 var, const Vector2 *pos, const Vector2 *vel, void *spawner, u32 sub,
                         u32 seed, u32 flags) {
    if (s_cb_used[MC_PRE_ENTITY_SPAWN] && s_spawn_ex_depth == 0 && pos && vel) {
        s_spawn_ex_depth++;
        int top = run_raw(MC_PRE_ENTITY_SPAWN, false, 0, "iiivvei", (lua_Integer)type, (lua_Integer)var,
                          (lua_Integer)sub, pos, vel, (u8 *)spawner, (lua_Integer)seed);
        s_spawn_ex_depth--;
        if (top >= 0) {
            if (lua_istable(L, -1)) {
                lua_Integer v[4] = {type, var, sub, seed};
                for (int i = 0; i < 4; i++) {
                    lua_rawgeti(L, -1, i + 1);
                    cb_integer(-1, "MC_PRE_ENTITY_SPAWN", &v[i]);
                    lua_pop(L, 1);
                }
                type = (u32)v[0], var = (u32)v[1], sub = (u32)v[2], seed = (u32)v[3];
            }
            lua_settop(L, top);
        }
    }
    void *r = s_spawn_ex(g, type, var, pos, vel, spawner, sub, seed, flags);
    for (int n = rp_spawn_extra((uintptr_t)__builtin_return_address(0), spawner); n > 0; n--) {
        seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;  // next seed for the copy
        s_spawn_ex(g, type, var, pos, vel, spawner, sub, seed ? seed : 1u, flags);
    }
    return r;
}

/* MC_INPUT_ACTION: the game asks KAGE::Input::ManagerBase for every action it reads (through the PLT);
 * a callback returning a boolean (pressed / triggered) or a number (value) replaces the answer. Queries
 * made while Lua runs (Input.IsActionPressed from a mod, or engine code a mod called) are answered
 * as they are, which also keeps a callback from calling itself. */
enum { INPUT_PRESSED = 0, INPUT_TRIGGERED = 1, INPUT_VALUE = 2 };
static void *s_input_ent;  // the entity of the Manager::IsAction* call running now
static bool input_override(int hook, u32 action, double *out) {
    if (!L || !s_cb_used[MC_INPUT_ACTION] || nnosGetCurrentThread() != s_lua_thread) return false;  // Lua is not thread-safe
    lua_Debug ar;
    if (lua_getstack(L, 0, &ar)) return false;  // Lua is running
    int top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, s_run_ref);
    lua_pushinteger(L, MC_INPUT_ACTION);
    lua_pushinteger(L, hook);
    /* the entity: what the game handed Manager::IsActionPressed / IsActionTriggered / GetActionValue (players
     * pass themselves), as on PC; the Switch Manager drops it before ManagerBase (x3 = 0) */
    if (s_input_ent) api_push_entity(L, (u8 *)s_input_ent);
    else lua_pushnil(L);
    lua_pushinteger(L, hook);
    lua_pushinteger(L, action);
    bool r = false;
    if (lua_pcall(L, 5, 1, 0) != LUA_OK) {
        overlay_add(lua_tostring(L, -1), OVERLAY_ERROR);
    } else if (!lua_isnil(L, -1)) {
        /* PC 0x8014c0 (pressed / triggered): lua_toboolean, so 0 is pressed too; 0x801620 (value):
         * luaL_checknumber, a boolean there is the PC's "number expected" error */
        if (hook != INPUT_VALUE) {
            *out = lua_toboolean(L, -1);
            r = true;
        } else {
            int ok = 0;
            double n = lua_tonumberx(L, -1, &ok);
            if (ok) *out = n, r = true;
            else overlay_add("MC_INPUT_ACTION: number expected", OVERLAY_ERROR);
        }
    }
    lua_settop(L, top);
    return r;
}
typedef bool (*ActionBoolFn)(void *, u32, u32, u32 *);
typedef float (*ActionValueFn)(void *, u32, u32, u32 *);
static ActionBoolFn s_action_pressed, s_action_triggered;
static ActionValueFn s_action_value;
static u32 s_block_actions;  // bit per action id: reported up while the Mods menu code calls the game's menus
static bool hk_action_pressed(void *im, u32 action, u32 ctrl, u32 *p) {
    double v;
    if (action < 32 && (s_block_actions >> action & 1)) return false;
    int je = rp_je_action(0, im, action, ctrl, p);
    if (je >= 0) return je;
    if (input_override(INPUT_PRESSED, action, &v)) return v != 0;
    return s_action_pressed(im, action, ctrl, p);
}
static bool hk_action_triggered(void *im, u32 action, u32 ctrl, u32 *p) {
    double v;
    if (action < 32 && (s_block_actions >> action & 1)) return false;
    int je = rp_je_action(1, im, action, ctrl, p);
    if (je >= 0) return je;
    if (input_override(INPUT_TRIGGERED, action, &v)) return v != 0;
    return s_action_triggered(im, action, ctrl, p);
}
static float hk_action_value(void *im, u32 action, u32 ctrl, u32 *p) {
    double v;
    if (input_override(INPUT_VALUE, action, &v)) return (float)v;
    return s_action_value(im, action, ctrl, p);
}
typedef bool (*MgrBoolFn)(u32, u32, void *);
typedef float (*MgrValueFn)(u32, u32, void *);
static MgrBoolFn s_mgr_pressed, s_mgr_triggered;
static MgrValueFn s_mgr_value;
static bool hk_mgr_pressed(u32 action, u32 ctrl, void *ent) {
    void *o = s_input_ent;
    s_input_ent = ent;
    bool r = s_mgr_pressed(action, ctrl, ent);
    s_input_ent = o;
    return r;
}
static bool hk_mgr_triggered(u32 action, u32 ctrl, void *ent) {
    void *o = s_input_ent;
    s_input_ent = ent;
    bool r = s_mgr_triggered(action, ctrl, ent);
    s_input_ent = o;
    return r;
}
static float hk_mgr_value(u32 action, u32 ctrl, void *ent) {
    void *o = s_input_ent;
    s_input_ent = ent;
    float r = s_mgr_value(action, ctrl, ent);
    s_input_ent = o;
    return r;
}
#ifdef ISAACLUA_HOST_TEST
bool test_action_pressed(u32 action) { return hk_action_pressed(NULL, action, 0, NULL); }
float test_action_value(u32 action) { return hk_action_value(NULL, action, 0, NULL); }
void test_set_action_originals(void *pressed, void *triggered, void *value) {
    s_action_pressed = (ActionBoolFn)pressed, s_action_triggered = (ActionBoolFn)triggered;
    s_action_value = (ActionValueFn)value;
}
#endif

/* ------------------------------------------------------------ the Mods menu
 * As on PC, the main menu gets MODS (under OPTIONS) and its screen (MainMenuType.MODS = 16). The Switch
 * build keeps Menu_Mods, its spot in MenuManager's camera table (the camera goes there in state 16) and
 * the "mods on" switch, but nothing creates or runs the menu: MenuManager::Update / Render run ours while
 * the state is 16, and Menu_Game draws one more entry with the menu font. A switch is kept in
 * sdmc:/isaaclua/mods_off.txt (save data; disable.it can't be written into romfs): the mod's callbacks
 * and resources/ stop or start at once, a mod off at the start runs its main.lua when switched on (0.99.47);
 * content/ the game reads itself follows at the next start. */
enum { kMenuStateGame = 3, kMenuStateMods = 16 };
enum { kActConfirm = 14, kActBack = 15, kActUp = 19, kActDown = 20, kActModsToggle = 23 };
#define kMM_State 56            /* MenuManager::eState */
#define kMM_Camera 60           /* Vector2 */
#define kMM_ModsSpot 0x1aff88   /* Vector2: camera table entry 16 */
#define kMenu_Pos 8             /* Vector2 in every Menu_* */
#define kMenuGame_Cursor 4      /* i32: 0 new run .. 4 options */
#define kMenuGame_Anm 16        /* ANM2: layer 6 is the cursor */
#define kMenuMods_Cursor 368    /* ModEntry** into ModManager's list */
#define kMenuMods_Dirty 360
#define kManager_MenuFont 0x117120  /* the font Menu_Mods writes with */
#define kModEntry_On 312        /* u8; the folder name is the std::string at +0 */
/* where MODS goes on the main menu's paper (480x270 units, from photos of the Switch screen) */
#define kModsTextX 231.0f       /* centre: the word starts under OPTIONS, as the list slants (HW photo) */
#define kModsTextY 192.0f       /* top of the text box (the letters start ~9 lower), under the closed-up rows */
#define kModsScale 1.0f
#define kModsRowDy0 (-4.0f)     /* CONTINUE, CHALLENGES, STATS, OPTIONS move up this much without REP+ */
#define kModsRowDy1 (-8.0f)
#define kModsRowDy2 (-14.0f)
#define kModsRowDy3 (-20.0f)
#define kModsSlant 0.05f        /* rise per unit to the right, as OPTIONS */
#define kModsArrowDx 2.0f      /* the cursor's move from OPTIONS */
#define kModsArrowDy 20.0f      /* from the moved OPTIONS row */
typedef void (*InitAtFn)(void *, const float *);
static VoidFn s_mm_update, s_cc_render, s_game_menu_update, s_game_menu_render;
static VoidFn s_mods_ctor, s_mods_update, s_mods_render;
static InitAtFn s_mods_init;
static void (*s_menu_set_state)(void *, int);
static u8 *(*s_anm_get_layer)(void *, int);
static void (*s_play_sound)(void *, int, float, int, bool, float);
static void **s_g_menu_manager;
static void *s_input_mgr;
static u8 *s_mods_menu;
static bool s_mods_item, s_mods_off_applied;

static const char *entry_name(const u8 *s) { return (s[0] & 1) ? *(const char *const *)(s + 16) : (const char *)s + 1; }
static u8 **mod_entries(u8 ***end) {
    u8 *mm = mod_manager();
    u8 **b = mm ? *(u8 ***)mm : NULL;
    *end = mm ? *(u8 ***)(mm + 8) : NULL;
    return b;
}
static void lua_mod_off(const char *name, bool off) {
    if (!L || s_modoff_ref == LUA_NOREF) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, s_modoff_ref);
    lua_pushfstring(L, "rom:/mods/%s/", name);
    lua_pushboolean(L, off);
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) lua_pop(L, 1);
}
/* a switch in the Mods menu, without restarting: callbacks stop/start (lua_mod_off), resources/ follow for
 * what the game loads from now on, and a mod that was off at the start runs its main.lua now */
static bool s_mods_quiet;  // switching all mods with X: one warning, not one per mod
static void mod_switched(const char *name, bool off) {
    char dir[300];
    snprintf(dir, sizeof dir, "rom:/mods/%s/", name);
    lua_mod_off(name, off);
    modfs_set_mod_off(dir, off);
    if (off && s_mods_quiet) return;
    if (off) {
        /* HW 0.99.47: StageAPI switched off live, MinimapAPI kept drawing through StageAPI's callback -> no map */
        char msg[360];
        snprintf(msg, sizeof msg, "%s switched off: mods that use it (as with StageAPI or MCM) can stop working until the game restarts", name);
        overlay_add(msg, OVERLAY_WARN);
        return;
    }
    if (!L || s_enablemod_ref == LUA_NOREF) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, s_enablemod_ref);
    lua_pushstring(L, dir);
    lua_pushstring(L, name);
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) overlay_add(lua_tostring(L, -1), OVERLAY_ERROR), lua_pop(L, 1);
}
static void mods_off_save(void) {
    u8 **e, **b = mod_entries(&e);
    size_t cap = 1, n = 0;
    for (u8 **i = b; b && i < e; i++) cap += strlen(entry_name(*i)) + 1;
    char *out = (char *)malloc(cap);
    if (!out) return;
    for (u8 **i = b; b && i < e; i++)
        if (!(*i)[kModEntry_On]) n += (size_t)snprintf(out + n, cap - n, "%s\n", entry_name(*i));
    if (!isaaclua_save_store(kModsOffFile, out, n)) overlay_add("Mods: could not save the switches", OVERLAY_WARN);
    free(out);
}
static void mods_off_apply(void) {  // once the save data is mounted (a user is picked)
    if (s_mods_off_applied || !nnfs_IsMounted("sdmc")) return;
    s_mods_off_applied = true;
    char *off = isaaclua_save_load(kModsOffFile);
    u8 **e, **b = mod_entries(&e);
    for (u8 **i = b; off && b && i < e; i++)
        if (name_in_list(off, entry_name(*i))) (*i)[kModEntry_On] = 0, lua_mod_off(entry_name(*i), true);
    free(off);
}
static bool triggered(u32 action) { return s_action_triggered && s_action_triggered(s_input_mgr, action, (u32)-1, NULL); }
static void menu_sound(int id) {
    u8 *mgr = manager_ptr();
    if (mgr && s_play_sound) s_play_sound(mgr, id, 1.0f, 2, false, 1.0f);
}

/* The widget on the Mods paper, as in Repentance+ (ModsMenu.anm2 null WidgetButton at 145,-42 of "Main"):
 * the game's own button icon for the toggle action (X on Switch, TAB / X on PC) instead of the "TAB" tile, and
 * "press to disable mods" while any mod is on, "enable" when all are off. X switches every mod off, and the
 * mods that were on back on (PC / REP+: the global mods switch; here the per-mod switches, live). */
#define kModsAnm 16           /* ANM2 in Menu_Mods */
#define kModsLayerText 12     /* frame 0 "press .. to enable mods", 1 "disable" */
#define kModsLayerTab 13      /* the "TAB" tile */
#define kModsMainX 200.0f     /* Menu_Mods::Render plays "Main" at its position + (200, 0) */
#define kModsButtonX 152.0f   /* the centre of the "TAB" tile it replaces (REP+ null WidgetButton: 145) */
#define kModsButtonY (-42.0f)
#define kMM_Controller 0x34   /* i32: the controller the menus read (Menu_Game::Render passes it on) */
typedef struct { float x, y; } Vec2Arg;
static void (*s_anm_set_layer_frame)(void *, int, int);
static void (*s_render_button_icon)(void *, int, int, int, Vec2Arg, bool);
static char *s_mods_all_off;  // names switched off by X, to switch back on
static bool mods_any_on(u8 **b, u8 **e) {
    for (u8 **i = b; b && i < e; i++)
        if ((*i)[kModEntry_On]) return true;
    return false;
}
static void mods_toggle_all(u8 **b, u8 **e) {
    if (!b || b >= e) return;
    s_mods_quiet = true;
    if (mods_any_on(b, e)) {
        size_t cap = 1, n = 0;
        for (u8 **i = b; i < e; i++) cap += strlen(entry_name(*i)) + 1;
        free(s_mods_all_off);
        s_mods_all_off = (char *)malloc(cap);
        if (s_mods_all_off) s_mods_all_off[0] = 0;
        for (u8 **i = b; i < e; i++)
            if ((*i)[kModEntry_On]) {
                if (s_mods_all_off) n += (size_t)snprintf(s_mods_all_off + n, cap - n, "%s\n", entry_name(*i));
                (*i)[kModEntry_On] = 0;
                mod_switched(entry_name(*i), true);
            }
        overlay_add("All mods switched off: they start again cleanly after a game restart", OVERLAY_WARN);
    } else {
        for (u8 **i = b; i < e; i++)
            if (!s_mods_all_off || name_in_list(s_mods_all_off, entry_name(*i))) {
                (*i)[kModEntry_On] = 1;
                mod_switched(entry_name(*i), false);
            }
    }
    s_mods_quiet = false;
    s_mods_menu[kMenuMods_Dirty] = 1;
    mods_off_save();
    menu_sound(282);
}
static void mods_widget_text(u8 **b, u8 **e) {
    if (s_anm_set_layer_frame) s_anm_set_layer_frame(s_mods_menu + kModsAnm, kModsLayerText, mods_any_on(b, e) ? 1 : 0);
}
static void mods_render(void) {
    u8 *mm = s_g_menu_manager ? (u8 *)*s_g_menu_manager : NULL;
    u8 *tab = s_render_button_icon && s_anm_get_layer ? s_anm_get_layer(s_mods_menu + kModsAnm, kModsLayerTab) : NULL;
    u8 vis = tab ? tab[0x7c] : 0;
    if (tab) tab[0x7c] = 0;
    s_mods_render(s_mods_menu);
    if (!tab) return;
    tab[0x7c] = vis;
    u8 *mgr = manager_ptr();
    if (!mm || !mgr) return;
    const float *cam = (const float *)(mm + kMM_Camera), *pos = (const float *)(s_mods_menu + kMenu_Pos);
    Vec2Arg at = {cam[0] + pos[0] + kModsMainX + kModsButtonX, cam[1] + pos[1] + kModsButtonY};
    s_render_button_icon(mgr, *(int *)(mm + kMM_Controller), kActModsToggle, 0, at, true);
}

static void hk_mm_update(void *mm) {
    u8 **e, **b = mod_entries(&e);
    if (!s_mods_menu && b && b < e && s_mods_ctor && s_mods_init) {
        u8 *raw = (u8 *)calloc(1, 0x400 + 16);
        if (raw) {
            s_mods_menu = (u8 *)(((uintptr_t)raw + 15) & ~(uintptr_t)15);
            s_mods_ctor(s_mods_menu);
            const float *spot = (const float *)((u8 *)mm + kMM_ModsSpot);
            float pos[2] = {-spot[0], -spot[1]};  // as MenuManager::Init places the other menus
            s_mods_init(s_mods_menu, pos);
        }
    }
    if (s_mods_menu) mods_off_apply();
    s_mm_update(mm);
    static bool was_in_mods;
    bool just_opened = !was_in_mods;  // the X that opened it from the main menu is still "triggered" now
    was_in_mods = s_mods_menu && *(int *)((u8 *)mm + kMM_State) == kMenuStateMods;
    if (!was_in_mods) return;
    /* the switch is ours (the game's writes disable.it next to the mod), the global one stays on */
    u8 **cur = *(u8 ***)(s_mods_menu + kMenuMods_Cursor);
    if (triggered(kActConfirm) && cur && cur >= b && cur < e) {
        (*cur)[kModEntry_On] ^= 1;
        s_mods_menu[kMenuMods_Dirty] = 1;
        mod_switched(entry_name(*cur), !(*cur)[kModEntry_On]);
        mods_off_save();
        menu_sound(282);
    }
    if (!just_opened && triggered(kActModsToggle)) mods_toggle_all(b, e);  // HW 0.99.54 test: all went off on open
    s_block_actions = 1u << kActConfirm | 1u << kActModsToggle;
    s_mods_update(s_mods_menu);
    s_block_actions = 0;
    mods_widget_text(b, e);
}
static void hk_cc_render(void *menu) {  // the last menu MenuManager::Render draws: ours goes after it
    s_cc_render(menu);
    if (s_mods_menu && s_mods_render) mods_render();
}
/* Repentance+ main menu (while repplus.lua's _isaaclua_mods_widget draws the MODS paper, i.e. the mod is on):
 * as in REP+'s GameMenu.anm2 the daily run paper is gone (REP+ has it under ONLINE in the list) and X, the
 * toggle action, opens the Mods menu from the MODS paper; our extra list entry under OPTIONS becomes
 * DAILY RUN (REP+'s ONLINE cannot work here). Without the mod: the game's daily paper on X, MODS in the list. */
#define kMenuGame_LayerDaily 7  /* DailyPaper, DailyText, DailyTab: visible while the daily run can be played */
#define kDailyCursorDy (-6.0f)  /* the cursor on DAILY RUN: CHALLENGES' row (y 103) less 6 */
#define kDailyTextX 244.0f      /* DAILY RUN where CHALLENGES was (Item3 at 185,103): centre, top of the text */
#define kDailyTextY 123.0f      /* HW: the text sits ~20 below its box top, as MODS did */
#define kDailyScale 1.0f
#define kDailyItemX 186.0f      /* top left of the drawn DAILY RUN (128x32), in CHALLENGES' row (Item3 185,103 128x48) */
#define kDailyItemY 104.0f
enum { kMenuStateDaily = 4 };
static bool s_mods_widget, s_in_game_menu_render, s_daily_avail;
static bool (*s_render_icon)(void *, int, int, int, Vec2Arg, bool);
static bool hk_render_icon(void *mgr, int ctrl, int action, int idx, Vec2Arg at, bool any) {
    if (s_in_game_menu_render && s_mods_widget && action == kActModsToggle) return true;  // daily paper's "press X"
    return s_render_icon(mgr, ctrl, action, idx, at, any);
}
static void hk_game_menu_update(void *menu) {
    int *cursor = (int *)((u8 *)menu + kMenuGame_Cursor);
    if (!s_mods_menu) {
        s_game_menu_update(menu);
        return;
    }
    if (s_mods_widget && triggered(kActModsToggle)) {  // X: the MODS paper
        s_mods_item = false;
        menu_sound(283);
        if (s_menu_set_state && s_g_menu_manager) s_menu_set_state(*s_g_menu_manager, kMenuStateMods);
        s_block_actions = ~0u;
        s_game_menu_update(menu);
        s_block_actions = 0;
        return;
    }
    if (s_mods_widget) {  // REP+: DAILY RUN is the third entry, where REP+ has ONLINE
        /* up/down are ours here (rows 0 NEW RUN, 1 CONTINUE, 2 DAILY RUN, 3-5 the game's 2-4): the game's
         * own repeat on a held stick moved two rows when we took one step (HW 0.99.55 test builds) */
        static int held;
        bool down = s_action_pressed && s_action_pressed(s_input_mgr, kActDown, (u32)-1, NULL);
        bool up = s_action_pressed && s_action_pressed(s_input_mgr, kActUp, (u32)-1, NULL);
        int step = 0;
        if (up != down) {
            held++;
            if (held == 1 || (held > 30 && held % 8 == 0)) step = down ? 1 : -1;
        } else held = 0;
        int row = s_mods_item ? 2 : *cursor < 2 ? *cursor : *cursor + 1;
        if (step) {
            row = (row + step + 6) % 6;
            menu_sound(282);
        }
        s_mods_item = row == 2;
        *cursor = row < 2 ? row : row == 2 ? 2 : row - 1;
        if (s_mods_item) {
            if (triggered(kActConfirm) && s_daily_avail) {
                menu_sound(283);
                if (s_menu_set_state && s_g_menu_manager) s_menu_set_state(*s_g_menu_manager, kMenuStateDaily);
            }
            /* back still works; confirm, X and the moves are ours */
            s_block_actions = 1u << kActConfirm | 1u << kActModsToggle | 1u << kActUp | 1u << kActDown;
        } else
            s_block_actions = 1u << kActModsToggle | 1u << kActUp | 1u << kActDown;
        s_game_menu_update(menu);
        s_block_actions = 0;
        if (s_mods_item) *cursor = 2;
        return;
    }
    /* without REP+: MODS is a sixth row under OPTIONS. Up/down are ours here too, as in the REP+ branch: the
     * game wrapped OPTIONS -> NEW RUN first and we set the cursor back, so the arrow jumped (HW 2026-11-08) */
    static int held;
    bool down = s_action_pressed && s_action_pressed(s_input_mgr, kActDown, (u32)-1, NULL);
    bool up = s_action_pressed && s_action_pressed(s_input_mgr, kActUp, (u32)-1, NULL);
    int step = 0;
    if (up != down) {
        held++;
        if (held == 1 || (held > 30 && held % 8 == 0)) step = down ? 1 : -1;
    } else held = 0;
    int row = s_mods_item ? 5 : *cursor;
    if (step) {
        row = (row + step + 6) % 6;
        menu_sound(282);
    }
    s_mods_item = row == 5;
    *cursor = row == 5 ? 4 : row;
    if (s_mods_item) {
        if (triggered(kActConfirm)) {
            menu_sound(283);
            if (s_menu_set_state && s_g_menu_manager) s_menu_set_state(*s_g_menu_manager, kMenuStateMods);
        }
        /* back still goes to the save files; confirm and the moves are ours */
        s_block_actions = 1u << kActConfirm | 1u << kActUp | 1u << kActDown;
    } else
        s_block_actions = 1u << kActUp | 1u << kActDown;
    s_game_menu_update(menu);
    s_block_actions = 0;
    if (s_mods_item) *cursor = 4;
}
static void mods_widget(void *menu, u8 *mm) {
    s_mods_widget = false;
    if (!L || !mm || !s_mods_menu) return;
    const float *cam = (const float *)(mm + kMM_Camera), *pos = (const float *)((u8 *)menu + kMenu_Pos);
    float x = cam[0] + pos[0], y = cam[1] + pos[1];
    g_engine_ready = true;  // the menus draw ANM2s: Lua sprites can be made now (before the first run too)
    int top = lua_gettop(L);
    lua_getglobal(L, "_isaaclua_mods_widget");
    if (lua_isfunction(L, -1)) {
        lua_pushnumber(L, x), lua_pushnumber(L, y);
        if (lua_pcall(L, 2, 3, 0) == LUA_OK && lua_toboolean(L, -3)) {
            s_mods_widget = true;
            u8 *mgr = manager_ptr();
            Vec2Arg at = {x + (float)lua_tonumber(L, -2), y + (float)lua_tonumber(L, -1)};
            if (mgr && s_render_icon) s_render_icon(mgr, *(int *)(mm + kMM_Controller), kActModsToggle, 0, at, true);
        }
    }
    lua_settop(L, top);
}
static void hk_game_menu_render(void *menu) {
    u8 *mm = s_g_menu_manager ? (u8 *)*s_g_menu_manager : NULL;
    /* the game's cursor (layer 6, on OPTIONS) is moved down to MODS while that is picked; +72/+76 is the
     * layer's offset, which Menu_Game itself shifts for narrow screens */
    bool rep = s_mods_widget;
    int cur_row = *(int *)((u8 *)menu + kMenuGame_Cursor);
    /* without REP+: CONTINUE..OPTIONS (layers 2-5) close up so MODS gets a whole row on the paper under OPTIONS
     * (HW 2026-11-08: a small MODS under the untouched rows overlapped OPTIONS); the cursor goes with its row */
    static const float kNRowDy[4] = {kModsRowDy0, kModsRowDy1, kModsRowDy2, kModsRowDy3};
    u8 *cur = !rep && s_anm_get_layer ? s_anm_get_layer((u8 *)menu + kMenuGame_Anm, 6) : NULL;
    float *off = cur ? (float *)(cur + 72) : NULL, ox = 0, oy = 0;
    if (off) {
        ox = off[0], oy = off[1];
        if (s_mods_item) off[0] += kModsArrowDx, off[1] += kNRowDy[3] + kModsArrowDy;
        else if (cur_row >= 1 && cur_row <= 4) off[1] += kNRowDy[cur_row - 1];
    }
    float *ny[4] = {NULL, NULL, NULL, NULL};
    for (int i = 0; !rep && s_anm_get_layer && i < 4; i++) {
        u8 *l = s_anm_get_layer((u8 *)menu + kMenuGame_Anm, 2 + i);
        if (l) ny[i] = (float *)(l + 76), ny[i][0] += kNRowDy[i];
    }
    /* REP+: the rows below CONTINUE close up to make room for DAILY RUN and keep OPTIONS on the paper
     * (HW photo: a whole row down put OPTIONS under it); the cursor goes with its row */
    static const float kRowDy[3] = {24.0f, 18.0f, 13.0f};  // CHALLENGES, STATS, OPTIONS (layers 3-5)
    static const float kRowDx[3] = {5.0f, 5.0f, 3.0f};     // keep the list's slant below DAILY RUN (HW photo)
    float *iy[4] = {NULL, NULL, NULL, NULL}, idy[4] = {0, 0, 0, 0}, idx[4] = {0, 0, 0, 0};
    int cur_idx = cur_row;
    for (int i = 0; rep && s_anm_get_layer && i < 4; i++) {
        float dy = i < 3 ? kRowDy[i] : s_mods_item ? kDailyCursorDy : cur_idx >= 2 && cur_idx <= 4 ? kRowDy[cur_idx - 2] : 0;
        u8 *l = dy != 0 ? s_anm_get_layer((u8 *)menu + kMenuGame_Anm, 3 + i) : NULL;
        float dx = i < 3 ? kRowDx[i] : s_mods_item ? 0 : cur_idx >= 2 && cur_idx <= 4 ? kRowDx[cur_idx - 2] : 0;
        if (l) iy[i] = (float *)(l + 76), idy[i] = dy, idx[i] = dx, iy[i][0] += dy, iy[i][-1] += dx;
    }
    u8 *dl[3] = {NULL, NULL, NULL}, dv[3] = {0, 0, 0};
    if (s_anm_get_layer) {
        u8 *l7 = s_anm_get_layer((u8 *)menu + kMenuGame_Anm, kMenuGame_LayerDaily);
        s_daily_avail = l7 && l7[0x7c];
        for (int i = 0; s_mods_widget && i < 3; i++)
            if ((dl[i] = s_anm_get_layer((u8 *)menu + kMenuGame_Anm, kMenuGame_LayerDaily + i)) != NULL)
                dv[i] = dl[i][0x7c], dl[i][0x7c] = 0;
    }
    s_in_game_menu_render = true;
    s_game_menu_render(menu);
    s_in_game_menu_render = false;
    for (int i = 0; i < 3; i++)
        if (dl[i]) dl[i][0x7c] = dv[i];
    for (int i = 0; i < 4; i++)
        if (iy[i]) iy[i][0] -= idy[i], iy[i][-1] -= idx[i];
    if (off) off[0] = ox, off[1] = oy;
    for (int i = 0; i < 4; i++)
        if (ny[i]) ny[i][0] -= kNRowDy[i];
    mods_widget(menu, mm);
    u8 *mgr = manager_ptr();
    if (!s_mods_menu || !mm || !mgr || !G.draw_utf8 || !G.color_ctor || !G.string_width_utf8) return;
    const float *cam = (const float *)(mm + kMM_Camera), *pos = (const float *)((u8 *)menu + kMenu_Pos);
    const void *font = mgr + kManager_MenuFont;
    ColorStorage c;
    G.color_ctor(&c, 0.11f, 0.08f, 0.07f, 1.0f);  // the ink of the other entries
    /* letter by letter, each a little higher: the entries above are written on a slant */
    if (rep && L) {  // the game's drawn "DAILY RUN" (repplus.lua), the font only if that fails
        int top = lua_gettop(L);
        lua_getglobal(L, "_isaaclua_daily_item");
        bool drawn = false;
        if (lua_isfunction(L, -1)) {
            lua_pushnumber(L, cam[0] + pos[0] + kDailyItemX), lua_pushnumber(L, cam[1] + pos[1] + kDailyItemY);
            lua_pushboolean(L, s_daily_avail);
            drawn = lua_pcall(L, 3, 1, 0) == LUA_OK && lua_toboolean(L, -1);
        }
        lua_settop(L, top);
        if (drawn) return;
    }
    const char *word = rep ? "DAILY RUN" : "MODS";
    const float scale = rep ? kDailyScale : kModsScale;
    if (rep && !s_daily_avail) G.color_ctor(&c, 0.11f, 0.08f, 0.07f, 0.4f);  // greyed like an entry not open yet
    float w = scale * (float)G.string_width_utf8(font, word);
    float x = cam[0] + pos[0] + (rep ? kDailyTextX : kModsTextX) - w / 2, y = cam[1] + pos[1] + (rep ? kDailyTextY : kModsTextY);
    for (int i = 0; word[i]; i++) {
        char ch[2] = {word[i], 0};
        G.draw_utf8(font, ch, x, y - kModsSlant * (x - (cam[0] + pos[0] + (rep ? kDailyTextX : kModsTextX) - w / 2)), scale, scale, &c, 0, false);
        x += scale * (float)G.string_width_utf8(font, ch);
    }
}
static void install_mods_menu(const ModView *v) {
    s_mods_ctor = (VoidFn)api_sym("_ZN15IsaacRepentance9Menu_ModsC1Ev");
    s_mods_init = (InitAtFn)api_sym("_ZN15IsaacRepentance9Menu_Mods4InitERKN4KAGE4Math7Vector2E");
    s_mods_update = (VoidFn)api_sym("_ZN15IsaacRepentance9Menu_Mods6UpdateEv");
    s_mods_render = (VoidFn)api_sym("_ZN15IsaacRepentance9Menu_Mods6RenderEv");
    s_menu_set_state = (void (*)(void *, int))api_sym("_ZN15IsaacRepentance11MenuManager8SetStateENS0_6eStateE");
    s_anm_get_layer = (u8 * (*)(void *, int)) api_sym("_ZN15IsaacRepentance4ANM28GetLayerEi");
    s_play_sound = (void (*)(void *, int, float, int, bool, float))api_sym(
        "_ZN15IsaacRepentance7Manager9PlaySoundENS_12eSoundEffectEfibf");
    s_g_menu_manager = (void **)api_sym("_ZN15IsaacRepentance13g_MenuManagerE");
    s_input_mgr = api_sym("_ZN4KAGE5Input9g_ManagerE");
    s_anm_set_layer_frame = (void (*)(void *, int, int))api_sym("_ZN15IsaacRepentance4ANM213SetLayerFrameEii");
    s_render_button_icon = (void (*)(void *, int, int, int, Vec2Arg, bool))api_sym(
        "_ZN15IsaacRepentance7Manager16RenderButtonIconEiiiN4KAGE4Math7Vector2Eb");
    if (!s_mods_ctor || !s_mods_init || !s_mods_update || !s_mods_render || !s_menu_set_state || !s_g_menu_manager ||
        !s_input_mgr || !s_action_triggered || !s_action_pressed)
        return;
    s_mm_update = (VoidFn)hook_got(v, "_ZN15IsaacRepentance11MenuManager6UpdateEv", (void *)hk_mm_update);
    s_cc_render = (VoidFn)hook_got(v, "_ZN15IsaacRepentance21Menu_Custom_Challenge6RenderEv", (void *)hk_cc_render);
    s_game_menu_update = (VoidFn)hook_got(v, "_ZN15IsaacRepentance9Menu_Game6UpdateEv", (void *)hk_game_menu_update);
    s_game_menu_render = (VoidFn)hook_got(v, "_ZN15IsaacRepentance9Menu_Game6RenderEv", (void *)hk_game_menu_render);
    s_render_icon = (bool (*)(void *, int, int, int, Vec2Arg, bool))hook_got(
        v, "_ZN15IsaacRepentance7Manager16RenderButtonIconEiiiN4KAGE4Math7Vector2Eb", (void *)hk_render_icon);
}

static void install_late_hooks(void) {
    static bool done = false;
    if (done || !s_rep_for_late) return;
    done = true;
    s_hud_render = (VoidFn)hook_got(s_rep_for_late, "_ZN15IsaacRepentance3HUD6RenderEv", (void *)hk_hud_render);
    if (!s_hud_render) overlay_add("HUD::Render hook missing: no POST_RENDER", OVERLAY_WARN);
}

static void hk_game_update(void *game) {
    if (s_defer_lua) lua_load_deferred();
    game_start_events();
    s_game_update(game);
    g_engine_ready = true;
    install_late_hooks();
    game_tick();
}

/* Manager::GetChallengeParams(id) = params vector (Manager+0x36740, 0xf0 each) + id*0xf0, unchecked. Continuing a
 * saved run of a mod's challenge with that mod switched off asked for 46 of 46 entries and crashed in Game::Start
 * (HW 0.99.18): past the end, hand out challenge 0's (no challenge) */
static u8 *hk_challenge_params(u8 *mgr, u32 id) {
    u8 *b = *(u8 **)(mgr + 0x36740), *e = *(u8 **)(mgr + 0x36748);
    if (!b || (u64)id >= (u64)(e - b) / 0xf0) id = 0;
    return b + (u64)id * 0xf0;
}
static void hk_evaluate_items(void *p) {
    u32 flags = *(u32 *)((u8 *)p + kPlayer_CacheFlags);
    float rb_saved[6];  // Rock Bottom's best stats (see below), as they were before this evaluation
    memcpy(rb_saved, (u8 *)p + kPlayer_RockBottom, sizeof rb_saved);
    s_evaluate_items(p);
    if (!s_cb_used[MC_EVALUATE_CACHE]) return;
    u8 *pl = p, fly = pl[0x1954];  // m_CanFly
    float ss0 = ENT_F32(pl, kPlayer_ShotSpeed), acc0 = ENT_F32(pl, kPlayer_TearFallingAccel);
    float libra0[4] = {ENT_F32(pl, kPlayer_Damage), ENT_F32(pl, kPlayer_MaxFireDelay), ENT_F32(pl, kPlayer_MoveSpeed),
                       ENT_F32(pl, kPlayer_TearRange)};
    for (int b = 0; b < 32; b++)
        if (flags & (1u << b)) run(MC_EVALUATE_CACHE, true, (lua_Integer)(1u << b), (u8 *)p, (lua_Integer)(1u << b), 1);
    /* PC EvaluateItems (0x72ed20) calls the callback for each flag inside the flag's block, then finishes the
     * block with what the callback left; here the callbacks run after the whole function, so the same last
     * steps are applied again to the mods' values: */
    if (flags & 2 && ENT_F32(pl, kPlayer_MaxFireDelay) < -0.75f) ENT_F32(pl, kPlayer_MaxFireDelay) = -0.75f;
    if (flags & 4 && ENT_F32(pl, kPlayer_ShotSpeed) < 0.6f) ENT_F32(pl, kPlayer_ShotSpeed) = 0.6f;
    if (flags & 8) {
        if (ENT_F32(pl, kPlayer_TearRange) < 40.0f) ENT_F32(pl, kPlayer_TearRange) = 40.0f;
        // TearFallingAcceleration *= ShotSpeed^2: done with the shot speed before the callbacks
        float ss1 = ENT_F32(pl, kPlayer_ShotSpeed);
        if (ss1 != ss0 && ss0 > 0 && acc0 > 0 && ENT_F32(pl, kPlayer_TearFallingAccel) == acc0)
            ENT_F32(pl, kPlayer_TearFallingAccel) = acc0 / (ss0 * ss0) * ss1 * ss1;
    }
    /* PC runs MC_EVALUATE_CACHE inside EvaluateItems, before the game applies CanFly; here it runs after, so
     * apply a CanFly the callback changed as EvaluateItems does: grid collision WALLS (3) when flying,
     * GROUND (5) when not, and the costume map rebuilt (wings). HW 0.73: Andromeda's Spode did not fly */
    if ((flags & 128) && pl[0x1954] != fly) {
        *(u32 *)(pl + 0x1e0) = pl[0x1954] ? 3 : 5;
        static VoidFn rebuild;
        if (!rebuild) rebuild = (VoidFn)api_sym("_ZN15IsaacRepentance13Entity_Player19rebuild_costume_mapEv");
        if (rebuild) rebuild(p);
    }
    /* After all the flags PC applies Libra (304, or the Zodiac effect 304), then Rock Bottom (562), to the stats
     * the callbacks left; the Switch build did both before our callbacks: do them again (EvaluateItems 0x28ba50) */
    typedef bool (*HasFn)(void *, int, bool);
    typedef int (*ZodiacFn)(void *);
    typedef float (*RateFn)(float);
    static HasFn has;
    static ZodiacFn zodiac;
    static RateFn speed_rating, range_rating, speed_from, delay_from, damage_from, range_from;
    if (!has) {
        has = (HasFn)api_sym("_ZNK15IsaacRepentance13Entity_Player14HasCollectibleENS_16eCollectibleTypeEb");
        zodiac = (ZodiacFn)api_sym("_ZNK15IsaacRepentance13Entity_Player15GetZodiacEffectEv");
        speed_rating = (RateFn)api_sym("_ZN15IsaacRepentance11PlayerUtils14GetSpeedRatingEf");
        range_rating = (RateFn)api_sym("_ZN15IsaacRepentance11PlayerUtils14GetRangeRatingEf");
        speed_from = (RateFn)api_sym("_ZN15IsaacRepentance11PlayerUtils18GetSpeedFromRatingEf");
        delay_from = (RateFn)api_sym("_ZN15IsaacRepentance11PlayerUtils22GetTearDelayFromRatingEf");
        damage_from = (RateFn)api_sym("_ZN15IsaacRepentance11PlayerUtils19GetDamageFromRatingEf");
        range_from = (RateFn)api_sym("_ZN15IsaacRepentance11PlayerUtils18GetRangeFromRatingEf");
    }
    if (!has) return;
    bool libra_changed = libra0[0] != ENT_F32(pl, kPlayer_Damage) || libra0[1] != ENT_F32(pl, kPlayer_MaxFireDelay) ||
                         libra0[2] != ENT_F32(pl, kPlayer_MoveSpeed) || libra0[3] != ENT_F32(pl, kPlayer_TearRange);
    // only when a callback changed one of Libra's stats: the game's own result is already balanced
    if ((flags & 0x1b) && libra_changed && speed_rating && range_rating && speed_from && delay_from && damage_from && range_from &&
        (has(p, 304, false) || (zodiac && zodiac(p) == 304))) {
        float dps = ENT_F32(pl, kPlayer_Damage) * 30.0f / (ENT_F32(pl, kPlayer_MaxFireDelay) + 1.0f);
        float r = sqrtf(dps);
        if (r == r) {
            union { u32 u; float f; } e = {0x3f266666}, k = {0x400a57eb};  // 0.65, 2.1616
            float dmg_r = powf(r, e.f) * k.f - 2.0f;
            float rating = (dmg_r + dmg_r + speed_rating(ENT_F32(pl, kPlayer_MoveSpeed)) +
                            range_rating(ENT_F32(pl, kPlayer_TearRange))) * 0.25f;
            ENT_F32(pl, kPlayer_MoveSpeed) = speed_from(rating);
            ENT_F32(pl, kPlayer_MaxFireDelay) = delay_from(rating);
            ENT_F32(pl, kPlayer_Damage) = damage_from(rating);
            ENT_F32(pl, kPlayer_TearRange) = range_from(rating);
            flags |= 0x1b;
        }
    }
    if (!pl[0x18f9] && has(p, 562, false)) {
        float *best = (float *)(pl + kPlayer_RockBottom);  // speed, tears (30 / (delay + 1)), damage, range,
        memcpy(best, rb_saved, sizeof rb_saved);            // shot speed, luck
        float tears;
#define RB(bit, i, off) if (flags & (bit)) { if (best[i] < ENT_F32(pl, off)) best[i] = ENT_F32(pl, off); ENT_F32(pl, off) = best[i]; }
        RB(1, 2, kPlayer_Damage)
        if (flags & 2) {
            tears = 30.0f / (ENT_F32(pl, kPlayer_MaxFireDelay) + 1.0f);
            if (best[1] < tears) best[1] = tears;
            ENT_F32(pl, kPlayer_MaxFireDelay) = 30.0f / best[1] - 1.0f;
        }
        RB(8, 3, kPlayer_TearRange)
        RB(16, 0, kPlayer_MoveSpeed)
        RB(1024, 5, kPlayer_Luck)
        RB(4, 4, kPlayer_ShotSpeed)
#undef RB
    }
}

/* vtable Update hooks: orig = the class's own Update, patched in every vtable that holds it */
typedef struct { const char *sym; VoidFn orig; void *hook; } VtHook;
enum { VT_PLAYER, VT_NPC, VT_PICKUP, VT_TEAR, VT_FAMILIAR, VT_EFFECT, VT_PLAYER_INIT, VT_PICKUP_INIT, VT_EFFECT_INIT,
       VT_PLAYER_RENDER, VT_PICKUP_RENDER, VT_EFFECT_RENDER, VT_PICKUP_COLLISION, VT_TEAR_INIT, VT_TEAR_RENDER,
       VT_PROJ_INIT, VT_PROJ_UPDATE, VT_PROJ_RENDER, VT_BOMB_INIT, VT_BOMB_UPDATE, VT_BOMB_RENDER,
       VT_DMG_ENTITY, VT_DMG_PLAYER, VT_DMG_NPC, VT_DMG_FAMILIAR, VT_DMG_PICKUP, VT_DMG_SLOT, VT_NPC_INIT, VT_NPC_RENDER,
       VT_LASER_INIT, VT_LASER_UPDATE, VT_LASER_RENDER, VT_KNIFE_INIT, VT_KNIFE_UPDATE, VT_KNIFE_RENDER,
       VT_ENTITY_KILL, VT_FAMILIAR_INIT, VT_COLL_FAMILIAR, VT_COLL_NPC, VT_COLL_PLAYER, VT_COLL_TEAR, VT_COLL_PROJ,
       VT_COLL_KNIFE, VT_COLL_BOMB, VT_CLEAR_REFS, VT_CLEAR_REFS_PICKUP, VT_FAMILIAR_RENDER, VT_COUNT };
static VtHook s_vt[VT_COUNT];

static void hk_player_update(void *e) {
    /* input the player's own update asks for is that player's (PC: Entity_Player code passes itself to
     * Manager::IsAction*, which reaches MC_INPUT_ACTION as the entity) */
    void *prev_ent = s_input_ent;
    s_input_ent = e;
    s_vt[VT_PLAYER].orig(e);
    s_input_ent = prev_ent;
    if (!s_have_game_update && e == api_player(0)) {  // no Game::Update hook: tick from player 1
        install_late_hooks();
        game_tick();
    }
    /* a player update outside a started run gets no callbacks: on a restart Switch updates the players once
     * from Game::InterpolatePositions after Game::Init / Game::Exit, before the Game::Update that starts the run
     * (game_start_events). PC fires GAME_STARTED in Game::Start, before any update; Fiend Folio's PEFFECT_UPDATE
     * found no savedata.run there (main.lua:10236, discs.lua:172, HW 0.99.43). */
    if (s_have_game_update && (!s_in_run || s_start_pending)) return;
    run(MC_POST_PEFFECT_UPDATE, true, ENT_I32(e, kPlayer_Type), (u8 *)e, 0, 0);
    run(MC_POST_PLAYER_UPDATE, true, ENT_I32(e, kEnt_Variant), (u8 *)e, 0, 0);
}
static bool pre_npc_update(void *e);
static void hk_npc_update(void *e) {
    if (pre_npc_update(e)) return;  // PRE_NPC_UPDATE returned true: the game's update is skipped
    s_vt[VT_NPC].orig(e);
    run(MC_NPC_UPDATE, true, ENT_I32(e, kEnt_Type), (u8 *)e, 0, 0);
}
static void hk_pickup_update(void *e) {
    s_vt[VT_PICKUP].orig(e);
    run(MC_POST_PICKUP_UPDATE, true, ENT_I32(e, kEnt_Variant), (u8 *)e, 0, 0);
}
static void hk_tear_update(void *e) {
    s_vt[VT_TEAR].orig(e);
    run(MC_POST_TEAR_UPDATE, true, ENT_I32(e, kEnt_Variant), (u8 *)e, 0, 0);
}
static void hk_familiar_update(void *e) {
    s_vt[VT_FAMILIAR].orig(e);
    run(MC_FAMILIAR_UPDATE, true, ENT_I32(e, kEnt_Variant), (u8 *)e, 0, 0);
}
static void hk_effect_update(void *e) {
    s_vt[VT_EFFECT].orig(e);
    run(MC_POST_EFFECT_UPDATE, true, ENT_I32(e, kEnt_Variant), (u8 *)e, 0, 0);
}

/* POST_*_INIT: the entity is not in the room's list yet during Init, but mods use it there all the same
 * (Remove, Visible): api_entity_alive counts it as alive meanwhile */
static void run_init(int cb, int param, u8 *e) {
    api_init_push(e);
    run(cb, true, param, e, 0, 0);
    api_init_pop();
}
/* Entity::Init(type, variant, subtype, seed) of players and pickups: POST_*_INIT after it */
typedef void (*InitFn)(void *, int, u32, u32, u32);
static void hk_player_init(void *e, int type, u32 variant, u32 subtype, u32 seed) {
    ((InitFn)s_vt[VT_PLAYER_INIT].orig)(e, type, variant, subtype, seed);
    run_init(MC_POST_PLAYER_INIT, ENT_I32(e, kEnt_Variant), (u8 *)e);
}
static void hk_pickup_init(void *e, int type, u32 variant, u32 subtype, u32 seed) {
    u8 *outer = s_init_pickup;
    s_init_pickup = e;
    ((InitFn)s_vt[VT_PICKUP_INIT].orig)(e, type, variant, subtype, seed);
    s_init_pickup = outer;
    run_init(MC_POST_PICKUP_INIT, ENT_I32(e, kEnt_Variant), (u8 *)e);
}

static void hk_effect_init(void *e, int type, u32 variant, u32 subtype, u32 seed) {
    ((InitFn)s_vt[VT_EFFECT_INIT].orig)(e, type, variant, subtype, seed);
    run_init(MC_POST_EFFECT_INIT, ENT_I32(e, kEnt_Variant), (u8 *)e);
}

/* Entity::Render(const Vector2 &offset): POST_*_RENDER(entity, offset) after the entity is drawn */
typedef void (*RenderFn)(void *, const Vector2 *);
static void hk_player_render(void *e, const Vector2 *off) {
    ((RenderFn)s_vt[VT_PLAYER_RENDER].orig)(e, off);
    g_in_entity_render = true;
    run_args(MC_POST_PLAYER_RENDER, true, ENT_I32(e, kEnt_Variant), "ev", (u8 *)e, off);
    g_in_entity_render = false;
}
static void hk_pickup_render(void *e, const Vector2 *off) {
    ((RenderFn)s_vt[VT_PICKUP_RENDER].orig)(e, off);
    g_in_entity_render = true;
    run_args(MC_POST_PICKUP_RENDER, true, ENT_I32(e, kEnt_Variant), "ev", (u8 *)e, off);
    g_in_entity_render = false;
}
/* NPCs: the optional argument of NPC callbacks is the entity type. Classes with their own Init/Render
 * (some bosses) are not covered: only vtables holding Entity_NPC's are patched. */
static void hk_npc_init(void *e, int type, u32 variant, u32 subtype, u32 seed) {
    ((InitFn)s_vt[VT_NPC_INIT].orig)(e, type, variant, subtype, seed);
    run_init(MC_POST_NPC_INIT, ENT_I32(e, kEnt_Type), (u8 *)e);
}
static void hk_npc_render(void *e, const Vector2 *off) {
    ((RenderFn)s_vt[VT_NPC_RENDER].orig)(e, off);
    g_in_entity_render = true;
    run_args(MC_POST_NPC_RENDER, true, ENT_I32(e, kEnt_Type), "ev", (u8 *)e, off);
    g_in_entity_render = false;
}
static void hk_familiar_render(void *e, const Vector2 *off) {
    ((RenderFn)s_vt[VT_FAMILIAR_RENDER].orig)(e, off);
    g_in_entity_render = true;
    run_args(MC_POST_FAMILIAR_RENDER, true, ENT_I32(e, kEnt_Variant), "ev", (u8 *)e, off);
    g_in_entity_render = false;
}
static void hk_effect_render(void *e, const Vector2 *off) {
    ((RenderFn)s_vt[VT_EFFECT_RENDER].orig)(e, off);
    g_in_entity_render = true;
    run_args(MC_POST_EFFECT_RENDER, true, ENT_I32(e, kEnt_Variant), "ev", (u8 *)e, off);
    g_in_entity_render = false;
}

/* tears, projectiles, bombs: POST_*_INIT / UPDATE / RENDER after the game's own */
static void hk_tear_init(void *e, int type, u32 variant, u32 subtype, u32 seed) {
    ((InitFn)s_vt[VT_TEAR_INIT].orig)(e, type, variant, subtype, seed);
    run_init(MC_POST_TEAR_INIT, ENT_I32(e, kEnt_Variant), (u8 *)e);
}
static void hk_tear_render(void *e, const Vector2 *off) {
    ((RenderFn)s_vt[VT_TEAR_RENDER].orig)(e, off);
    g_in_entity_render = true;
    run_args(MC_POST_TEAR_RENDER, true, ENT_I32(e, kEnt_Variant), "ev", (u8 *)e, off);
    g_in_entity_render = false;
}
static void hk_proj_init(void *e, int type, u32 variant, u32 subtype, u32 seed) {
    ((InitFn)s_vt[VT_PROJ_INIT].orig)(e, type, variant, subtype, seed);
    run_init(MC_POST_PROJECTILE_INIT, ENT_I32(e, kEnt_Variant), (u8 *)e);
}
static void hk_proj_update(void *e) {
    s_vt[VT_PROJ_UPDATE].orig(e);
    run(MC_POST_PROJECTILE_UPDATE, true, ENT_I32(e, kEnt_Variant), (u8 *)e, 0, 0);
}
static void hk_proj_render(void *e, const Vector2 *off) {
    ((RenderFn)s_vt[VT_PROJ_RENDER].orig)(e, off);
    g_in_entity_render = true;
    run_args(MC_POST_PROJECTILE_RENDER, true, ENT_I32(e, kEnt_Variant), "ev", (u8 *)e, off);
    g_in_entity_render = false;
}
static void hk_bomb_init(void *e, int type, u32 variant, u32 subtype, u32 seed) {
    ((InitFn)s_vt[VT_BOMB_INIT].orig)(e, type, variant, subtype, seed);
    run_init(MC_POST_BOMB_INIT, ENT_I32(e, kEnt_Variant), (u8 *)e);
}
static void hk_bomb_update(void *e) {
    s_vt[VT_BOMB_UPDATE].orig(e);
    run(MC_POST_BOMB_UPDATE, true, ENT_I32(e, kEnt_Variant), (u8 *)e, 0, 0);
}
static void hk_bomb_render(void *e, const Vector2 *off) {
    ((RenderFn)s_vt[VT_BOMB_RENDER].orig)(e, off);
    g_in_entity_render = true;
    run_args(MC_POST_BOMB_RENDER, true, ENT_I32(e, kEnt_Variant), "ev", (u8 *)e, off);
    g_in_entity_render = false;
}
/* Entity::Kill: in the vtables of classes that keep it, and through the GOT for the overrides
 * (Entity_NPC::Kill, Entity_Familiar::Kill call it); POST_ENTITY_KILL once, when it marks the entity
 * dead (u8 +0x1c3) */
static VoidFn s_entity_kill_got;
static void hk_entity_kill(void *e) {
    bool first = ((u8 *)e)[0x1c3] == 0;
    VoidFn orig = s_entity_kill_got ? s_entity_kill_got : s_vt[VT_ENTITY_KILL].orig;
    if (orig) orig(e);
    if (first && ((u8 *)e)[0x1c3]) run(MC_POST_ENTITY_KILL, true, ENT_I32(e, kEnt_Type), (u8 *)e, 0, 0);
}
/* Entity::ClearReferences: EntityList::Update calls it (vtable +136) on each entity it drops, and on room
 * exit; the subclasses call the base one through the GOT. POST_ENTITY_REMOVE once per entity, before. */
static VoidFn s_clear_refs_got;
/* Entities whose ClearReferences is running, innermost last. A mod's POST_ENTITY_REMOVE that spawns
 * (Fiend Folio's Briar thistle splits into 3 stingers) makes the game drop entities again from inside the
 * callback; with one "last" slot the nested drop cleared it and the thistle's ClearReferences ran its
 * callback again, without end (HW 0.99.40 China hang, floor 3). PC: once per entity. */
static void *s_removing[64];
static int s_nremoving;
static bool removing(void *e) {
    for (int i = 0; i < s_nremoving; i++)
        if (s_removing[i] == e) return true;
    return false;
}
static void clear_refs(void *e, VoidFn orig) {
    if (removing(e) || s_nremoving >= (int)(sizeof s_removing / sizeof *s_removing)) {  // the base one a subclass calls
        if (orig) orig(e);
        return;
    }
    s_removing[s_nremoving++] = e;
    /* Game::Exit (restart, back to the menu) frees the level's entities: EntityList::~EntityList calls each
     * one's ClearReferences while the list and its cell space are half gone. PC runs POST_ENTITY_REMOVE there
     * too, but a mod reacting with the world (Andromeda's meteor tears explode: BombExplosionEffects ->
     * EntityList::QueryRadius) crashed the Switch game on restart. The run is over: no callbacks then. */
    if (!s_game_exiting) run(MC_POST_ENTITY_REMOVE, true, ENT_I32(e, kEnt_Type), (u8 *)e, 0, 0);
    if (orig) orig(e);
    s_nremoving--;
}
static void hk_clear_refs(void *e) {
    clear_refs(e, s_clear_refs_got ? s_clear_refs_got : s_vt[VT_CLEAR_REFS].orig);
}
static void hk_clear_refs_pickup(void *e) {
    clear_refs(e, s_vt[VT_CLEAR_REFS_PICKUP].orig);
}
static void hk_laser_init(void *e, int type, u32 variant, u32 subtype, u32 seed) {
    ((InitFn)s_vt[VT_LASER_INIT].orig)(e, type, variant, subtype, seed);
    run_init(MC_POST_LASER_INIT, ENT_I32(e, kEnt_Variant), (u8 *)e);
}
static void hk_laser_update(void *e) {
    s_vt[VT_LASER_UPDATE].orig(e);
    run(MC_POST_LASER_UPDATE, true, ENT_I32(e, kEnt_Variant), (u8 *)e, 0, 0);
}
static void hk_laser_render(void *e, const Vector2 *off) {
    ((RenderFn)s_vt[VT_LASER_RENDER].orig)(e, off);
    g_in_entity_render = true;
    run_args(MC_POST_LASER_RENDER, true, ENT_I32(e, kEnt_Variant), "ev", (u8 *)e, off);
    g_in_entity_render = false;
}
/* POST_KNIFE_INIT / UPDATE / RENDER take the knife's SubType as their key on PC (isaac-ng.exe 0x7fdf30,
 * 0x7fe030, 0x7fe110: [entity + 0x30]), not its Variant */
static void hk_knife_init(void *e, int type, u32 variant, u32 subtype, u32 seed) {
    ((InitFn)s_vt[VT_KNIFE_INIT].orig)(e, type, variant, subtype, seed);
    run_init(MC_POST_KNIFE_INIT, ENT_I32(e, kEnt_SubType), (u8 *)e);
}
static void hk_knife_update(void *e) {
    s_vt[VT_KNIFE_UPDATE].orig(e);
    run(MC_POST_KNIFE_UPDATE, true, ENT_I32(e, kEnt_SubType), (u8 *)e, 0, 0);
}
static void hk_knife_render(void *e, const Vector2 *off) {
    ((RenderFn)s_vt[VT_KNIFE_RENDER].orig)(e, off);
    g_in_entity_render = true;
    run_args(MC_POST_KNIFE_RENDER, true, ENT_I32(e, kEnt_SubType), "ev", (u8 *)e, off);
    g_in_entity_render = false;
}

/* Entity::TakeDamage(amount, flags, const EntityRef &source, countdown) and its overrides: ENTITY_TAKE_DMG(entity,
 * amount, flags, source, countdown) first, keyed by the entity's type; a callback returning false cancels it */
typedef bool (*TakeDmgFn)(void *, float, u64, const void *, int);
static bool take_dmg(int vt, void *e, float amount, u64 flags, const void *ref, int countdown) {
    /* a player in damage cooldown (i-frames), or hit for 0, is turned away before ENTITY_TAKE_DMG on PC
     * (isaac-ng.exe 0x73d963: [player + 0x1308] > 0 or amount <= 0, unless DAMAGE_FAKE; Switch Entity_Player::
     * TakeDamage +0x1438 checks the same, cooldown at +0x1734). Running the callbacks anyway gave Fiend Folio's
     * China a broken heart on every frame an enemy touched her (HW 0.99.43: 12 from one Gaper). */
    if (vt == VT_DMG_PLAYER && !(flags & (1ull << 21)) && (ENT_I32(e, 0x1734) > 0 || !(amount > 0.0f)))
        return ((TakeDmgFn)s_vt[vt].orig)(e, amount, flags, ref, countdown);
    if (vt == VT_DMG_PLAYER) amount = rp_player_dmg(e, amount, flags, ref);
    int top = run_raw(MC_ENTITY_TAKE_DMG, true, ENT_I32(e, kEnt_Type), "efiRi", (u8 *)e, (double)amount,
                      (lua_Integer)flags, ref, (lua_Integer)countdown);
    if (top >= 0) {
        bool cancel = cb_bool(-1) == 0;  // PC 0x800e30: only false cancels
        lua_settop(L, top);
        if (cancel) return false;
    }
    return ((TakeDmgFn)s_vt[vt].orig)(e, amount, flags, ref, countdown);
}
#define DMG_HOOK(name, vt) \
    static bool name(void *e, float a, u64 f, const void *r, int c) { return take_dmg(vt, e, a, f, r, c); }
DMG_HOOK(hk_dmg_entity, VT_DMG_ENTITY)
DMG_HOOK(hk_dmg_player, VT_DMG_PLAYER)
DMG_HOOK(hk_dmg_npc, VT_DMG_NPC)
DMG_HOOK(hk_dmg_familiar, VT_DMG_FAMILIAR)
DMG_HOOK(hk_dmg_pickup, VT_DMG_PICKUP)
DMG_HOOK(hk_dmg_slot, VT_DMG_SLOT)

/* Entity_Player::FireTear / Weapon::FireTear (GOT): POST_FIRE_TEAR(tear) for the tear they return; the outer
 * call only (the player's calls the weapon's) */
typedef void *(*PlayerFireTearFn)(void *, const Vector2 *, const Vector2 *, u32, void *, float);
typedef void *(*WeaponFireTearFn)(void *, const Vector2 *, const Vector2 *, u32, void *, float, float);
static PlayerFireTearFn s_player_fire_tear;
static WeaponFireTearFn s_weapon_fire_tear;
static int s_fire_depth;
static void *fired(void *tear) {
    /* returned in x0 by both (from the disassembly); checked against the room's entities before use */
    if (tear && s_fire_depth == 0 && s_cb_used[MC_POST_FIRE_TEAR] && api_entity_alive((u8 *)tear, 2))
        run(MC_POST_FIRE_TEAR, false, 0, (u8 *)tear, 0, 0);
    return tear;
}
static void *hk_player_fire_tear(void *p, const Vector2 *pos, const Vector2 *vel, u32 f, void *src, float m) {
    s_fire_depth++;
    void *t = s_player_fire_tear(p, pos, vel, f, src, m);
    s_fire_depth--;
    return fired(t);
}
static void *hk_weapon_fire_tear(void *w, const Vector2 *pos, const Vector2 *vel, u32 f, void *src, float m, float n) {
    s_fire_depth++;
    void *t = s_weapon_fire_tear(w, pos, vel, f, src, m, n);
    s_fire_depth--;
    return fired(t);
}

/* Entity_Pickup::handle_collision(collider, low): PRE_PICKUP_COLLISION(pickup, collider, low). true or false skip
 * the game's own handling and go back to Entity::handle_collisions, where true means no physical collision. */
typedef bool (*CollisionFn)(void *, void *, bool);
static bool hk_pickup_collision(void *e, void *other, bool low) {
    int top = run_raw(MC_PRE_PICKUP_COLLISION, true, ENT_I32(e, kEnt_Variant), "eeb", (u8 *)e, (u8 *)other, (int)low);
    if (top >= 0) {
        int r = cb_bool(-1);  // PC 0x7fd2f0: any non-nil answer, read with lua_toboolean
        lua_settop(L, top);
        if (r >= 0) return r;
    }
    return ((CollisionFn)s_vt[VT_PICKUP_COLLISION].orig)(e, other, low);
}

/* the same for the other classes: PRE_<class>_COLLISION, keyed by type for NPCs, by variant otherwise */
static bool collision(int vt, int cb, void *e, void *other, bool low) {
    /* the key as on PC (isaac-ng.exe 0x7fdf30..0x7fe300): the knife callbacks are keyed by SubType, not Variant */
    int key = cb == MC_PRE_NPC_COLLISION ? ENT_I32(e, kEnt_Type)
              : ENT_I32(e, cb == MC_PRE_KNIFE_COLLISION ? kEnt_SubType : kEnt_Variant);
    int top = run_raw(cb, true, key, "eeb", (u8 *)e, (u8 *)other, (int)low);
    if (top >= 0) {
        int r = cb_bool(-1);  // PC 0x7fd2f0: any non-nil answer, read with lua_toboolean
        lua_settop(L, top);
        if (r >= 0) return r;
    }
    return ((CollisionFn)s_vt[vt].orig)(e, other, low);
}
static bool pre_npc_update(void *e) {
    if (!s_cb_used[MC_PRE_NPC_UPDATE]) return false;
    int top = run_raw(MC_PRE_NPC_UPDATE, true, ENT_I32(e, kEnt_Type), "e", (u8 *)e);
    if (top < 0) return false;
    bool r = cb_bool(-1) == 1;  // PC 0x7fcec0
    lua_settop(L, top);
    return r;
}
#define COLLISION_HOOK(name, vt, cb) \
    static bool name(void *e, void *o, bool low) { return collision(vt, cb, e, o, low); }
COLLISION_HOOK(hk_coll_familiar, VT_COLL_FAMILIAR, MC_PRE_FAMILIAR_COLLISION)
COLLISION_HOOK(hk_coll_npc, VT_COLL_NPC, MC_PRE_NPC_COLLISION)
COLLISION_HOOK(hk_coll_player, VT_COLL_PLAYER, MC_PRE_PLAYER_COLLISION)
COLLISION_HOOK(hk_coll_tear, VT_COLL_TEAR, MC_PRE_TEAR_COLLISION)
COLLISION_HOOK(hk_coll_proj, VT_COLL_PROJ, MC_PRE_PROJECTILE_COLLISION)
COLLISION_HOOK(hk_coll_knife, VT_COLL_KNIFE, MC_PRE_KNIFE_COLLISION)
COLLISION_HOOK(hk_coll_bomb, VT_COLL_BOMB, MC_PRE_BOMB_COLLISION)
static void hk_familiar_init(void *e, int type, u32 variant, u32 subtype, u32 seed) {
    ((InitFn)s_vt[VT_FAMILIAR_INIT].orig)(e, type, variant, subtype, seed);
    u8 *f = e, follower = f[0xd88], delayed = f[0xd89];  // IsFollower, IsDelayed
    run_init(MC_FAMILIAR_INIT, ENT_I32(e, kEnt_Variant), (u8 *)e);
    /* PC calls FAMILIAR_INIT early in Entity_Familiar::Init (0x5f06f6); right after it the game adds the familiar
     * to the followers when IsFollower is set, else to the delayed ones when IsDelayed is set (0x5f06fb). Here the
     * game's Init already ran with the values before the callback: do it for what the callback switched on */
    static VoidFn add_followers, add_delayed;
    if (!add_followers) {
        add_followers = (VoidFn)api_sym("_ZN15IsaacRepentance15Entity_Familiar16add_to_followersEv");
        add_delayed = (VoidFn)api_sym("_ZN15IsaacRepentance15Entity_Familiar14add_to_delayedEv");
    }
    if (f[0xd88] && !follower) { if (add_followers) add_followers(e); }
    else if (!f[0xd88] && f[0xd89] && !delayed && add_delayed) add_delayed(e);
}

/* Mod sprites as .png, as on PC: Manager::LoadImage turns every "x.png" into "x.pcx" (the game ships
 * .pcx only) and ImageManager::LoadImage then picks ImagePcx / ImagePng by that extension. A ".pcx"
 * that a mod has only as ".png" goes back to ".png" here, so the game's own PNG loader reads it
 * (libpng, alpha premultiplied like its .pcx, the loader its fonts use). */
const char *isaaclua_png_redirect(const char *path) {
    static char ring[4][512];
    static unsigned next;
    size_t n = path ? strlen(path) : 0;
    if (n < 4 || n >= sizeof ring[0] || path[n - 4] != '.' || (path[n - 3] | 32) != 'p' || (path[n - 2] | 32) != 'c' ||
        (path[n - 1] | 32) != 'x')
        return path;
    char tmp[1024];
    if (modfs_resolve(path, tmp, sizeof tmp)) return path;  // the mod has a .pcx itself
    char *png = ring[next++ % 4];
    memcpy(png, path, n - 3);
    memcpy(png + n - 3, "png", 4);
    return modfs_resolve(png, tmp, sizeof tmp) ? png : path;
}
void *isaaclua_load_image_orig;
#ifndef ISAACLUA_HOST_TEST
/* Mod images stay loaded for a while. The game frees an image the moment its last sprite goes
 * (SmartPointer<ImageBase> release -> ImageManager::s_image_reference_freed -> remove_image), so a mod's big
 * sheet is read and decoded again for every first sprite: Lego Studs ships 1860x1340 coin sheets, each new coin
 * in a room without coins stalled the game (HW 0.99.124). The loader keeps one more handle to the most recently
 * loaded mod images (files a mod provides, 256x256 or bigger), up to kKeepBytes / kKeepCount, oldest dropped
 * first; all are dropped when memory runs low. The handle is a second ImageManager::LoadImage of the same path
 * (a cache hit), released with the game's own SmartPointer destructor. */
typedef struct { void *p, *rc; } ImgPtr;
enum { kKeepCount = 32, kKeepBytes = 72 << 20, kKeepMinTexels = 256 * 256 };
static struct { ImgPtr h; u32 bytes, tick; } s_keep[kKeepCount];
static u32 s_keep_bytes, s_keep_tick;
static bool s_keep_busy, s_keep_off;
static void (*s_img_release)(ImgPtr *);
__attribute__((naked)) static void img_load_x8(void *orig, void *mgr, const char *path, void *vad, u64 flag, ImgPtr *out) {
    __asm__ volatile("mov x16, x0\nmov x0, x1\nmov x1, x2\nmov x2, x3\nmov x3, x4\nmov x8, x5\nbr x16\n");
}
static void keep_drop(int i) {
    if (!s_keep[i].h.p) return;
    s_keep_bytes -= s_keep[i].bytes;
    s_keep_busy = true;
    s_img_release(&s_keep[i].h);
    s_keep_busy = false;
    s_keep[i].h.p = s_keep[i].h.rc = NULL, s_keep[i].bytes = 0;
}
static void keep_drop_all(void) {
    if (!s_img_release) return;
    for (int i = 0; i < kKeepCount; i++) keep_drop(i);
}
__attribute__((used)) void isaaclua_image_keep(ImgPtr *res, const char *path, void *mgr, void *vad, u64 flag) {
    if (s_keep_busy || s_keep_off || !res || !res->p || !path) return;
    int slot = -1, oldest = -1;
    for (int i = 0; i < kKeepCount; i++) {
        if (s_keep[i].h.p == res->p) { s_keep[i].tick = ++s_keep_tick; return; }
        if (!s_keep[i].h.p) { if (slot < 0) slot = i; }
        else if (oldest < 0 || s_keep[i].tick < s_keep[oldest].tick) oldest = i;
    }
    char tmp[1024];
    if (!modfs_resolve(path, tmp, sizeof tmp)) return;  // the game's own image
    if (!s_img_release) {
        s_img_release = (void (*)(ImgPtr *))api_sym("_ZN12SmartPointerIN4KAGE8Graphics9ImageBaseEED2Ev");
        if (!s_img_release) { s_keep_off = true; return; }
    }
    typedef u32 (*DimFn)(void *);  // ImageBase vtable: width +0x50, height +0x58 (SourceQuad::convert_to_coordinate_space)
    void **vt = *(void ***)res->p;
    u64 texels = (u64)((DimFn)vt[10])(res->p) * ((DimFn)vt[11])(res->p);
    if (texels < kKeepMinTexels || texels * 4 > kKeepBytes) return;
    u32 bytes = (u32)(texels * 4);
    while (s_keep_bytes + bytes > kKeepBytes || slot < 0) {
        oldest = -1;
        for (int i = 0; i < kKeepCount; i++)
            if (s_keep[i].h.p && (oldest < 0 || s_keep[i].tick < s_keep[oldest].tick)) oldest = i;
        if (oldest < 0) break;
        keep_drop(oldest);
        slot = oldest;
    }
    if (slot < 0) return;
    ImgPtr h = {NULL, NULL};
    s_keep_busy = true;
    img_load_x8(isaaclua_load_image_orig, mgr, path, vad, flag, &h);
    s_keep_busy = false;
    if (h.p != res->p) {  // not the cached image (should not happen): give the handle back
        if (h.p || h.rc) { s_keep_busy = true; s_img_release(&h); s_keep_busy = false; }
        return;
    }
    s_keep[slot].h = h, s_keep[slot].bytes = bytes, s_keep[slot].tick = ++s_keep_tick;
    s_keep_bytes += bytes;
}
/* ImageManager::LoadImage(this, path, vad, bool) returns its image handle through x8: a trampoline that
 * swaps x1 (the .png redirect), calls the game's function and then shows the handle to isaaclua_image_keep */
__attribute__((naked)) static void hk_load_image(void) {
    __asm__ volatile("stp x29, x30, [sp, #-64]!\n"
                     "mov x29, sp\n"
                     "stp x0, x2, [sp, #16]\n"
                     "stp x3, x8, [sp, #32]\n"
                     "mov x0, x1\n"
                     "bl isaaclua_png_redirect\n"
                     "str x0, [sp, #48]\n"
                     "mov x1, x0\n"
                     "ldp x0, x2, [sp, #16]\n"
                     "ldp x3, x8, [sp, #32]\n"
                     "adrp x16, isaaclua_load_image_orig\n"
                     "ldr x16, [x16, :lo12:isaaclua_load_image_orig]\n"
                     "blr x16\n"
                     "ldr x0, [sp, #40]\n"
                     "ldr x1, [sp, #48]\n"
                     "ldp x2, x3, [sp, #16]\n"
                     "ldr x4, [sp, #32]\n"
                     "bl isaaclua_image_keep\n"
                     "ldp x29, x30, [sp], #64\n"
                     "ret\n");
}
#else
static void hk_load_image(void) {}
#endif

static bool lookup(const struct nnro_Module *m, const char *name, void *out) {
    uintptr_t a = 0;
    if (nnro_LookupModuleSymbol(&a, m, name) != 0 || a == 0) return false;
    *(uintptr_t *)out = a;
    return true;
}

static const struct nnro_Module *s_rep_module;  // Repentance.nro, for api_sym
#ifdef ISAACLUA_HOST_TEST
void *host_sym(const char *name);
void *api_sym(const char *name) { return host_sym(name); }
#else
void *api_sym(const char *name) {
    void *p = NULL;
    return s_rep_module && lookup(s_rep_module, name, &p) ? p : NULL;
}
#endif

/* Patch every vtable ("_ZTV..." symbol, bounded by its size) entry equal to one of our targets. */
static int patch_vtables(const ModView *v) {
    int patched = 0;
    for (u32 i = 0; i < v->nsyms; i++) {
        const Elf64_Sym *s = &v->symtab[i];
        if (!s->value || s->size < 16 || strncmp(v->strtab + s->name, "_ZTV", 4) != 0) continue;
        u64 *slots = (u64 *)(v->base + s->value);
        for (u64 k = 2; k < s->size / 8; k++)
            for (int h = 0; h < VT_COUNT; h++)
                if (s_vt[h].orig && slots[k] == (u64)(uintptr_t)s_vt[h].orig) {
                    if (!writable(&slots[k])) return patched;
                    slots[k] = (u64)(uintptr_t)s_vt[h].hook;
                    patched++;
                }
    }
    return patched;
}

/* KAGE::Memory::Manager::Initialize (0x4fedc8) gives its heap 1 (game objects, a pool it carves itself) 3 GB minus
 * MemoryConfigOptions::s_MainHeapSize (240 MB); the rest of the process memory stays with malloc, which Lua and
 * the game's own new share. With Fiend Folio malloc is down to 20-80 MB free while heap 1 keeps ~790 MB free
 * (HW 0.99.34, GetFreeMemoryAmount(1), constant over 3 floors). Manager::Initialize runs in run_repentance, after
 * nn::ro loaded the module, so the value is raised here: heap 1 still keeps ~520 MB free. */
enum { kMainHeapDefault = 0x0F000000, kGot_MainHeapSize = 0xaad7b8 };
static u32 s_main_heap_size = 512 << 20;
static void grow_malloc_budget(const struct nnro_Module *m) {
    /* the value sits in read-only data (HW 0.99.35: left at 240 MB); Manager::Initialize reads it through its
     * GOT slot, which is writable: point that slot at our own value */
    uintptr_t p = 0;
    if (!lookup(m, "_ZN4KAGE6Memory19MemoryConfigOptions14s_MainHeapSizeE", &p) || !p) return;
    u64 *slot = (u64 *)(s_rep.base + kGot_MainHeapSize);
    if (*slot == (u64)p && *(const u32 *)p == kMainHeapDefault && writable(slot))
        *slot = (u64)(uintptr_t)&s_main_heap_size;
}

/* Exact build we analysed: Repentance.nro from update v524288 (1.7.9b). */
static const u8 kRepBuildId[8] = {0x91, 0xc7, 0x3f, 0xdd, 0x57, 0x50, 0x61, 0x31};
#define kOff_GetPlanetariumChance 0x3d58bc

static bool s_ready = false;

static void on_module_loaded(const struct nnro_Module *m) {
    uintptr_t gpc = 0;
    if (!lookup(m, "_ZN15IsaacRepentance5Level20GetPlanetariumChanceEv", &gpc)) return;  // not Repentance.nro
    MemoryInfo mi;
    u32 pi;
    if (qol_svcQueryMemory(&mi, &pi, gpc) != 0) return;
    uintptr_t base = (uintptr_t)mi.addr;
    const u8 *hdr = (const u8 *)base;
    if (gpc - base != kOff_GetPlanetariumChance || memcmp(hdr + 0x10, "NRO0", 4) != 0 ||
        memcmp(hdr + 0x40, kRepBuildId, 8) != 0) {
        plugin_log("[isaaclua] unsupported Repentance.nro build, Lua disabled");
        return;
    }
    if (!mod_open(&s_rep, base)) return;
    s_ready = true;
    s_rep_module = m;
    grow_malloc_budget(m);

    lookup(m, "_ZN15IsaacRepentance6g_GameE", &G.g_Game);
    lookup(m, "_ZN15IsaacRepentance9g_ManagerE", &G.g_Manager);
    lookup(m, "_ZN15IsaacRepentance5WIDTHE", &G.width);
    lookup(m, "_ZN15IsaacRepentance6HEIGHTE", &G.height);
    lookup(m, "_ZN4KAGE8Graphics5ColorC1Effff", &G.color_ctor);
    lookup(m, "_ZNK4KAGE8Graphics4Font20DrawStringScaledUTF8EPKcffffNS0_5ColorEjb", &G.draw_utf8);
    lookup(m, "_ZNK4KAGE8Graphics4Font18GetStringWidthUTF8EPKc", &G.string_width_utf8);
    lookup(m, "_ZN15IsaacRepentance7Manager15IsActionPressedEjjPNS_6EntityE", &G.is_action_pressed);
    lookup(m, "_ZN15IsaacRepentance7Manager17IsActionTriggeredEjjPNS_6EntityE", &G.is_action_triggered);
    lookup(m, "_ZNK15IsaacRepentance6Entity13GetFrameCountEv", &G.ent_frame_count);
    lookup(m, "_ZN15IsaacRepentance6Entity4KillEv", &G.ent_kill);
    lookup(m, "_ZN15IsaacRepentance6Entity6RemoveEv", &G.ent_remove);
    lookup(m, "_ZNK15IsaacRepentance4Game11IsGreedModeEv", &G.is_greed);
    lookup(m, "_ZN15IsaacRepentance13Entity_Player8AddCoinsEi", &G.add_coins);
    lookup(m, "_ZN15IsaacRepentance13Entity_Player8AddBombsEi", &G.add_bombs);
    lookup(m, "_ZN15IsaacRepentance13Entity_Player7AddKeysEi", &G.add_keys);
    lookup(m, "_ZN15IsaacRepentance13Entity_Player9AddHeartsEib", &G.add_hearts);
    lookup(m, "_ZN15IsaacRepentance13Entity_Player12AddMaxHeartsEib", &G.add_max_hearts);
    lookup(m, "_ZN15IsaacRepentance13Entity_Player13AddSoulHeartsEi", &G.add_soul_hearts);
    lookup(m, "_ZN15IsaacRepentance13Entity_Player14AddBlackHeartsEi", &G.add_black_hearts);
    lookup(m, "_ZN15IsaacRepentance13Entity_Player16AddEternalHeartsEi", &G.add_eternal_hearts);
    lookup(m, "_ZN15IsaacRepentance13Entity_Player13AddBoneHeartsEi", &G.add_bone_hearts);
    lookup(m, "_ZN15IsaacRepentance13Entity_Player17RemoveCollectibleENS_16eCollectibleTypeEbib", &G.remove_collectible);
    lookup(m, "_ZN15IsaacRepentance13Entity_Player7AddCardENS_5eCardE", &G.add_card);
    lookup(m, "_ZN15IsaacRepentance13Entity_Player7AddPillENS_10ePillColorE", &G.add_pill);
    lookup(m, "_ZN15IsaacRepentance13Entity_Player12AnimateHappyEv", &G.animate_happy);
    lookup(m, "_ZN15IsaacRepentance13Entity_Player10AnimateSadEv", &G.animate_sad);
    lookup(m, "_ZN15IsaacRepentance13Entity_Player16ChangePlayerTypeENS_11ePlayerTypeEb", &G.change_player_type);
    lookup(m, "_ZN15IsaacRepentance13Entity_Player14AddCollectibleENS_16eCollectibleTypeEibii", &G.add_collectible);
    lookup(m, "_ZNK15IsaacRepentance13Entity_Player14HasCollectibleENS_16eCollectibleTypeEb", &G.has_collectible);
    lookup(m, "_ZNK15IsaacRepentance13Entity_Player10HasTrinketENS_12eTrinketTypeEb", &G.has_trinket);
    lookup(m, "_ZN15IsaacRepentance13Entity_Player10AddTrinketENS_12eTrinketTypeEb", &G.add_trinket);
    lookup(m, "_ZN15IsaacRepentance13Entity_Player13UseActiveItemENS_16eCollectibleTypeEjii", &G.use_active_item);
    lookup(m, "_ZN15IsaacRepentance13Entity_Player13EvaluateItemsEv", &G.evaluate_items);
    lookup(m, "_ZNK15IsaacRepentance4Room21WorldToScreenPositionEN4KAGE4Math7Vector2E", &G.world_to_screen);
    lookup(m, "_ZNK15IsaacRepentance10ItemConfig14GetCollectibleEi", &G.get_collectible);
    lookup(m, "_ZNK15IsaacRepentance10ItemConfig10GetTrinketEi", &G.get_trinket);
    lookup(m, "_ZN15IsaacRepentance4ANM2C1Ev", &G.anm_ctor);
    lookup(m, "_ZN15IsaacRepentance4ANM2D1Ev", &G.anm_dtor);
    lookup(m, "_ZN15IsaacRepentance4ANM25ResetEv", &G.anm_reset);
    lookup(m, "_ZN15IsaacRepentance4ANM24LoadERKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEEb", &G.anm_load);
    lookup(m, "_ZN15IsaacRepentance4ANM218ReplaceSpritesheetEiRKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE", &G.anm_replace_sheet);
    lookup(m, "_ZN15IsaacRepentance4ANM212LoadGraphicsEv", &G.anm_load_graphics);
    lookup(m, "_ZN15IsaacRepentance4ANM28SetFrameEPKci", &G.anm_set_frame);
    lookup(m, "_ZN15IsaacRepentance4ANM28SetFrameEi", &G.anm_set_frame_i);
    lookup(m, "_ZN15IsaacRepentance4ANM212SetAnimationEPKcb", &G.anm_set_animation);
    lookup(m, "_ZN15IsaacRepentance4ANM24PlayEPKcb", &G.anm_play);
    lookup(m, "_ZN15IsaacRepentance4ANM24StopEv", &G.anm_stop);
    lookup(m, "_ZN15IsaacRepentance4ANM26UpdateEv", &G.anm_update);
    lookup(m, "_ZN15IsaacRepentance4ANM26RenderERKN4KAGE4Math7Vector2ES5_S5_", &G.anm_render);
    lookup(m, "_ZN15IsaacRepentance4ANM211RenderLayerEiRKN4KAGE4Math7Vector2ES5_S5_", &G.anm_render_layer);
    lookup(m, "_ZNK15IsaacRepentance4ANM28GetFrameEv", &G.anm_get_frame);
    lookup(m, "_ZNK15IsaacRepentance4ANM210IsFinishedEPKc", &G.anm_is_finished);
    lookup(m, "_ZNK15IsaacRepentance4ANM29IsPlayingEPKc", &G.anm_is_playing);
    lookup(m, "_ZNK15IsaacRepentance4ANM216IsEventTriggeredEPKc", &G.anm_is_event_triggered);
    lookup(m, "_ZN15IsaacRepentance10EntityList11QueryRadiusERKN4KAGE4Math7Vector2Efj", &G.query_radius);

    kb_load_config();  // isaaclua.ini: also read by mods (language)
#ifdef ISAACLUA_DEBUG
    extern bool remote_config(void);
    if (remote_config()) g_autotest = 0, s_remote = true;  // rom:/isaaclua_remote.ini: remote control, no autotest
#endif
    lookup(m, "_ZN15IsaacRepentance10ModManager8ListModsEv", &s_mm_list);
    lookup(m, "_ZN15IsaacRepentance10ModManager11LoadConfigsEv", &s_mm_load);
    lookup(m, "_ZN15IsaacRepentance10ModManager15UpdateItemPoolsEv", &s_mm_item_pools);
    lookup(m, "_ZN15IsaacRepentance10ModManager15UpdateBossPoolsEv", &s_mm_boss_pools);
    lookup(m, "_ZN15IsaacRepentance10ModManager12UpdateCursesEv", &s_mm_curses);
    if (g_native_content && s_mm_list && s_mm_load) {
        hook_got(&s_rep, "_ZN15IsaacRepentance8ModEntry13WriteMetadataEv", (void *)hk_write_metadata);
        s_manager_load_configs = (VoidFn)hook_got(&s_rep, "_ZN15IsaacRepentance7Manager11LoadConfigsEv",
                                                  (void *)hk_manager_load_configs);
        s_defer_lua = s_manager_load_configs != NULL;
        s_load_item_pools = (LoadItemPoolsFn)hook_got(&s_rep, "_ZN15IsaacRepentance8ItemPool10load_poolsEPKcb",
                                                      (void *)hk_load_item_pools);
        lookup(m, "_ZN15IsaacRepentance10ModManager11UpdateRoomsENS_10RoomConfig6eStageENS1_5eModeE", &s_mm_rooms);
        s_load_stage_binary = (LoadStageFn)hook_got(
            &s_rep, "_ZN15IsaacRepentance10RoomConfig15LoadStageBinaryENS0_6eStageENS0_5eModeE", (void *)hk_load_stage_binary);
        s_load_boss_pools = (LoadBossPoolsFn)hook_got(&s_rep, "_ZN15IsaacRepentance8BossPool9LoadPoolsEPKcPNS_8ModEntryE",
                                                      (void *)hk_load_boss_pools);
    }
    if (!lua_start()) return;

    /* hooks nobody else uses: now */
    s_game_update = (VoidFn)hook_got(&s_rep, "_ZN15IsaacRepentance4Game6UpdateEv", (void *)hk_game_update);
    s_have_game_update = s_game_update != NULL;
    s_evaluate_items = (VoidFn)hook_got(&s_rep, "_ZN15IsaacRepentance13Entity_Player13EvaluateItemsEv",
                                        (void *)hk_evaluate_items);
    repplus_install(&s_rep, m);  // Repentance+ call-site hooks (src/repplus_hooks.inc), on while the isaac-repplus mod is on
    /* Lua's player:EvaluateItems() must run MC_EVALUATE_CACHE like PC: route it through the hook (HW 0.99.7:
     * AddCacheFlags(CACHE_ALL)+EvaluateItems() from Lua ran no callbacks; Andromeda's stats never applied) */
    if (s_evaluate_items) G.evaluate_items = hk_evaluate_items;
    hook_got(&s_rep, "_ZN15IsaacRepentance7Manager18GetChallengeParamsENS_10eChallengeE", (void *)hk_challenge_params);
    s_game_exit = (ExitFn)hook_got(&s_rep, "_ZN15IsaacRepentance4Game4ExitEb", (void *)hk_game_exit);
    isaaclua_load_image_orig = hook_got(&s_rep, "_ZN4KAGE8Graphics12ImageManager9LoadImageEPKcPNS0_25VertexAttributeDescriptorEb",
                                        (void *)hk_load_image);
    s_use_item = (UseItemFn)hook_got(&s_rep, "_ZN15IsaacRepentance13Entity_Player13UseActiveItemENS_16eCollectibleTypeEjii",
                                     (void *)hk_use_item);
    s_use_card = (UseCardFn)hook_got(&s_rep, "_ZN15IsaacRepentance13Entity_Player7UseCardENS_5eCardEj", (void *)hk_use_card);
    s_use_pill = (UsePillFn)hook_got(&s_rep, "_ZN15IsaacRepentance13Entity_Player7UsePillENS_11ePillEffectENS_10ePillColorEj",
                                     (void *)hk_use_pill);
    /* Lua's UseActiveItem / UseCard / UsePill run their callbacks as on PC: through the hooks */
    if (s_use_item) G.use_active_item = (void (*)(void *, int, u32, int, int))hk_use_item;
    if (s_use_card) isaaclua_sym_hooked[0] = (void *)hk_use_card;
    if (s_use_pill) isaaclua_sym_hooked[1] = (void *)hk_use_pill;
    s_action_pressed = (ActionBoolFn)hook_got(&s_rep, "_ZN4KAGE5Input11ManagerBase15IsActionPressedEjjPj",
                                              (void *)hk_action_pressed);
    s_action_triggered = (ActionBoolFn)hook_got(&s_rep, "_ZN4KAGE5Input11ManagerBase17IsActionTriggeredEjjPj",
                                                (void *)hk_action_triggered);
    s_action_value = (ActionValueFn)hook_got(&s_rep, "_ZN4KAGE5Input11ManagerBase14GetActionValueEjjPj",
                                             (void *)hk_action_value);
    /* 0.99.25 hooked Manager::IsActionPressed / IsActionTriggered / GetActionValue here for the MC_INPUT_ACTION
     * entity and crashed on the console: the slot hook_got found is the one the game's code calls for
     * KAGE::Input::ManagerBase (Cutscene::Update, bl PLT 0x671b90 with x0 = the input manager, x2 = ctrl -1),
     * so hk_mgr_* got (manager, action, ctrl) and took ctrl 0xffffffff for the entity. Manager::IsAction* itself
     * is reached by direct bl, not via the GOT: no entity until a code hook exists */
    (void)hk_mgr_pressed, (void)hk_mgr_triggered, (void)hk_mgr_value;
    install_mods_menu(&s_rep);
    daily_install(&s_rep);
    s_get_collectible = (GetCollectibleFn)hook_got(
        &s_rep, "_ZN15IsaacRepentance8ItemPool14GetCollectibleENS0_13eItemPoolTypeEjjNS_16eCollectibleTypeE",
        (void *)hk_get_collectible);
    s_level_init = (VoidFn)hook_got(&s_rep, "_ZN15IsaacRepentance5Level4InitEv", (void *)hk_level_init);
    s_game_init = (VoidFn)hook_got(&s_rep, "_ZN15IsaacRepentance4Game4InitEv", (void *)hk_game_init);
    s_start_saved = (StartSavedFn)hook_got(&s_rep, "_ZN15IsaacRepentance4Game19StartFromSavedStateERKNS_9GameStateE",
                                           (void *)hk_start_saved);
    s_level_change_room = (ChangeRoomFn)hook_got(&s_rep, "_ZN15IsaacRepentance5Level10ChangeRoomEii",
                                                 (void *)hk_level_change_room);
    hook_got(&s_rep, "_Znwm", (void *)hk_new);
    hook_got(&s_rep, "_Znam", (void *)hk_new_array);
    s_game_end = (GameEndFn)hook_got(&s_rep, "_ZN15IsaacRepentance4Game3EndENS_7eEndingE", (void *)hk_game_end);
    s_spawn_ex = (SpawnExFn)hook_got(
        &s_rep, "_ZN15IsaacRepentance4Game7SpawnExENS_11eEntityTypeEjRKN4KAGE4Math7Vector2ES6_PNS_6EntityEjjj", (void *)hk_spawn_ex);
    s_get_trinket = (GetTrinketFn)hook_got(&s_rep, "_ZN15IsaacRepentance8ItemPool10GetTrinketEb", (void *)hk_get_trinket);
    s_select_pickup = (SelectPickupFn)hook_got(&s_rep, "_ZN15IsaacRepentance13Entity_Pickup16SelectPickupTypeEjRjS1_bb",
                                               (void *)hk_select_pickup);
    s_get_pill = (GetPillFn)hook_got(&s_rep, "_ZNK15IsaacRepentance8ItemPool7GetPillEj", (void *)hk_get_pill);
    s_get_pill_effect = (GetPillEffectFn)hook_got(
        &s_rep, "_ZN15IsaacRepentance8ItemPool13GetPillEffectENS_10ePillColorEPNS_13Entity_PlayerE", (void *)hk_get_pill_effect);
    s_devolve = (DevolveFn)hook_got(&s_rep, "_ZN15IsaacRepentance4Game12DevolveEnemyEPNS_6EntityE", (void *)hk_devolve);
    s_get_card = (GetCardFn)hook_got(&s_rep, "_ZNK15IsaacRepentance8ItemPool7GetCardEjbbb", (void *)hk_get_card);
    s_clear_refs_got = (VoidFn)hook_got(&s_rep, "_ZN15IsaacRepentance6Entity15ClearReferencesEv", (void *)hk_clear_refs);
    s_entity_kill_got = (VoidFn)hook_got(&s_rep, "_ZN15IsaacRepentance6Entity4KillEv", (void *)hk_entity_kill);
    s_npc_death = (VoidFn)hook_got(&s_rep, "_ZN15IsaacRepentance10Entity_NPC20trigger_death_removeEv", (void *)hk_npc_death);
    s_clear_award = (RoomFn)hook_got(&s_rep, "_ZN15IsaacRepentance4Room15SpawnClearAwardEv", (void *)hk_clear_award);
    s_player_fire_tear = (PlayerFireTearFn)hook_got(
        &s_rep, "_ZN15IsaacRepentance13Entity_Player8FireTearERKN4KAGE4Math7Vector2ES3_jPNS_6EntityEf", (void *)hk_player_fire_tear);
    s_weapon_fire_tear = (WeaponFireTearFn)hook_got(
        &s_rep, "_ZN15IsaacRepentance6Weapon8FireTearERKN4KAGE4Math7Vector2ES5_jPNS_6EntityEff", (void *)hk_weapon_fire_tear);
    s_spawn_entity = (SpawnEntityFn)hook_got(
        &s_rep, "_ZN15IsaacRepentance4Room12spawn_entityEjRKNS_10RoomConfig5EntryEjPPNS_6EntityEb",
        (void *)hk_spawn_entity);
    static const struct { int i; const char *sym; void *hook; } kVt[] = {
        {VT_PLAYER, "_ZN15IsaacRepentance13Entity_Player6UpdateEv", (void *)hk_player_update},
        {VT_NPC, "_ZN15IsaacRepentance10Entity_NPC6UpdateEv", (void *)hk_npc_update},
        {VT_PICKUP, "_ZN15IsaacRepentance13Entity_Pickup6UpdateEv", (void *)hk_pickup_update},
        {VT_TEAR, "_ZN15IsaacRepentance11Entity_Tear6UpdateEv", (void *)hk_tear_update},
        {VT_FAMILIAR, "_ZN15IsaacRepentance15Entity_Familiar6UpdateEv", (void *)hk_familiar_update},
        {VT_EFFECT, "_ZN15IsaacRepentance13Entity_Effect6UpdateEv", (void *)hk_effect_update},
        {VT_PLAYER_INIT, "_ZN15IsaacRepentance13Entity_Player4InitENS_11eEntityTypeEjjj", (void *)hk_player_init},
        {VT_PICKUP_INIT, "_ZN15IsaacRepentance13Entity_Pickup4InitENS_11eEntityTypeEjjj", (void *)hk_pickup_init},
        {VT_EFFECT_INIT, "_ZN15IsaacRepentance13Entity_Effect4InitENS_11eEntityTypeEjjj", (void *)hk_effect_init},
        {VT_PLAYER_RENDER, "_ZN15IsaacRepentance13Entity_Player6RenderERKN4KAGE4Math7Vector2E", (void *)hk_player_render},
        {VT_PICKUP_RENDER, "_ZN15IsaacRepentance13Entity_Pickup6RenderERKN4KAGE4Math7Vector2E", (void *)hk_pickup_render},
        {VT_EFFECT_RENDER, "_ZN15IsaacRepentance13Entity_Effect6RenderERKN4KAGE4Math7Vector2E", (void *)hk_effect_render},
        {VT_PICKUP_COLLISION, "_ZN15IsaacRepentance13Entity_Pickup16handle_collisionEPNS_6EntityEb", (void *)hk_pickup_collision},
        {VT_TEAR_INIT, "_ZN15IsaacRepentance11Entity_Tear4InitENS_11eEntityTypeEjjj", (void *)hk_tear_init},
        {VT_TEAR_RENDER, "_ZN15IsaacRepentance11Entity_Tear6RenderERKN4KAGE4Math7Vector2E", (void *)hk_tear_render},
        {VT_PROJ_INIT, "_ZN15IsaacRepentance17Entity_Projectile4InitENS_11eEntityTypeEjjj", (void *)hk_proj_init},
        {VT_PROJ_UPDATE, "_ZN15IsaacRepentance17Entity_Projectile6UpdateEv", (void *)hk_proj_update},
        {VT_PROJ_RENDER, "_ZN15IsaacRepentance17Entity_Projectile6RenderERKN4KAGE4Math7Vector2E", (void *)hk_proj_render},
        {VT_BOMB_INIT, "_ZN15IsaacRepentance11Entity_Bomb4InitENS_11eEntityTypeEjjj", (void *)hk_bomb_init},
        {VT_BOMB_UPDATE, "_ZN15IsaacRepentance11Entity_Bomb6UpdateEv", (void *)hk_bomb_update},
        {VT_BOMB_RENDER, "_ZN15IsaacRepentance11Entity_Bomb6RenderERKN4KAGE4Math7Vector2E", (void *)hk_bomb_render},
        {VT_DMG_ENTITY, "_ZN15IsaacRepentance6Entity10TakeDamageEfmRKNS0_9EntityRefEi", (void *)hk_dmg_entity},
        {VT_DMG_PLAYER, "_ZN15IsaacRepentance13Entity_Player10TakeDamageEfmRKNS_6Entity9EntityRefEi", (void *)hk_dmg_player},
        {VT_DMG_NPC, "_ZN15IsaacRepentance10Entity_NPC10TakeDamageEfmRKNS_6Entity9EntityRefEi", (void *)hk_dmg_npc},
        {VT_DMG_FAMILIAR, "_ZN15IsaacRepentance15Entity_Familiar10TakeDamageEfmRKNS_6Entity9EntityRefEi", (void *)hk_dmg_familiar},
        {VT_DMG_PICKUP, "_ZN15IsaacRepentance13Entity_Pickup10TakeDamageEfmRKNS_6Entity9EntityRefEi", (void *)hk_dmg_pickup},
        {VT_DMG_SLOT, "_ZN15IsaacRepentance11Entity_Slot10TakeDamageEfmRKNS_6Entity9EntityRefEi", (void *)hk_dmg_slot},
        {VT_NPC_INIT, "_ZN15IsaacRepentance10Entity_NPC4InitENS_11eEntityTypeEjjj", (void *)hk_npc_init},
        {VT_NPC_RENDER, "_ZN15IsaacRepentance10Entity_NPC6RenderERKN4KAGE4Math7Vector2E", (void *)hk_npc_render},
        {VT_ENTITY_KILL, "_ZN15IsaacRepentance6Entity4KillEv", (void *)hk_entity_kill},
        {VT_CLEAR_REFS, "_ZN15IsaacRepentance6Entity15ClearReferencesEv", (void *)hk_clear_refs},
        {VT_CLEAR_REFS_PICKUP, "_ZN15IsaacRepentance13Entity_Pickup15ClearReferencesEv", (void *)hk_clear_refs_pickup},
        {VT_FAMILIAR_INIT, "_ZN15IsaacRepentance15Entity_Familiar4InitENS_11eEntityTypeEjjj", (void *)hk_familiar_init},
        {VT_COLL_FAMILIAR, "_ZN15IsaacRepentance15Entity_Familiar16handle_collisionEPNS_6EntityEb", (void *)hk_coll_familiar},
        {VT_COLL_NPC, "_ZN15IsaacRepentance10Entity_NPC16handle_collisionEPNS_6EntityEb", (void *)hk_coll_npc},
        {VT_COLL_PLAYER, "_ZN15IsaacRepentance13Entity_Player16handle_collisionEPNS_6EntityEb", (void *)hk_coll_player},
        {VT_COLL_TEAR, "_ZN15IsaacRepentance11Entity_Tear16handle_collisionEPNS_6EntityEb", (void *)hk_coll_tear},
        {VT_COLL_PROJ, "_ZN15IsaacRepentance17Entity_Projectile16handle_collisionEPNS_6EntityEb", (void *)hk_coll_proj},
        {VT_COLL_KNIFE, "_ZN15IsaacRepentance12Entity_Knife16handle_collisionEPNS_6EntityEb", (void *)hk_coll_knife},
        {VT_COLL_BOMB, "_ZN15IsaacRepentance11Entity_Bomb16handle_collisionEPNS_6EntityEb", (void *)hk_coll_bomb},
        {VT_LASER_INIT, "_ZN15IsaacRepentance12Entity_Laser4InitENS_11eEntityTypeEjjj", (void *)hk_laser_init},
        {VT_LASER_UPDATE, "_ZN15IsaacRepentance12Entity_Laser6UpdateEv", (void *)hk_laser_update},
        {VT_LASER_RENDER, "_ZN15IsaacRepentance12Entity_Laser6RenderERKN4KAGE4Math7Vector2E", (void *)hk_laser_render},
        {VT_KNIFE_INIT, "_ZN15IsaacRepentance12Entity_Knife4InitENS_11eEntityTypeEjjj", (void *)hk_knife_init},
        {VT_KNIFE_UPDATE, "_ZN15IsaacRepentance12Entity_Knife6UpdateEv", (void *)hk_knife_update},
        {VT_KNIFE_RENDER, "_ZN15IsaacRepentance12Entity_Knife6RenderERKN4KAGE4Math7Vector2E", (void *)hk_knife_render},
        {VT_FAMILIAR_RENDER, "_ZN15IsaacRepentance15Entity_Familiar6RenderERKN4KAGE4Math7Vector2E", (void *)hk_familiar_render},
    };
    for (size_t i = 0; i < sizeof kVt / sizeof *kVt; i++) {
        s_vt[kVt[i].i].sym = kVt[i].sym;
        s_vt[kVt[i].i].hook = kVt[i].hook;
        lookup(m, kVt[i].sym, &s_vt[kVt[i].i].orig);
    }
    int nvt = patch_vtables(&s_rep);
    /* Lua Entity:TakeDamage goes through the ENTITY_TAKE_DMG hook as on PC (its vtable call reaches the callback);
     * the symbols in native.c isaaclua_sym_hooked_name[2..7] */
    {
        static const int kDmg[6] = {VT_DMG_ENTITY, VT_DMG_PLAYER, VT_DMG_NPC, VT_DMG_FAMILIAR, VT_DMG_PICKUP, VT_DMG_SLOT};
        for (int i = 0; i < 6; i++)
            if (nvt > 0 && s_vt[kDmg[i]].orig) isaaclua_sym_hooked[2 + i] = s_vt[kDmg[i]].hook;
    }
    const char *kMounted = "_ZN4KAGE7Filesys15IContentManager18GetMountedFilePathEPKc";
    u64 *mounted = mod_got_slot(&s_rep, kMounted);
    void *mounted_orig = mounted ? got_target(&s_rep, mounted, kMounted, NULL) : NULL;
    if (mounted_orig) *mounted = (u64)(uintptr_t)modfs_hook(mounted_orig);
    else mounted = NULL;
    int npad = install_npad_hooks(&s_rep);
#ifdef ISAACLUA_DEBUG
    /* remote control brings the network up (nifm request). The game's own KAGE::Network::Manager thread then
     * sees it (check_connections, 0x4ffc1c) and dispatches on a NEX scheduler the Switch build never created
     * (Data Abort in nn::nex::Scheduler::Dispatch, HW 2026-11-01): the game is told there is no network. */
    if (s_remote) hook_got_to(&s_rep, "_ZN2nn4nifm18IsNetworkAvailableEv", (void *)game_network_available,
                              (void *)nnifm_IsNetworkAvailable_real);
#endif
    /* shared with isaacqol: on the first update (or now, if there is no update hook at all) */
    s_rep_for_late = &s_rep;
    if (!s_have_game_update && !s_vt[VT_PLAYER].orig) install_late_hooks();

    char msg[320];
    snprintf(msg, sizeof msg,
             "hooks: update %s, evaluate %s, %d vtable entries, %d/5 pad reads, files %s (%d mod files), png %s, use %d/3, exit %s, input %d/3, rolls %d/3",
             s_have_game_update ? "ok" : "via player", s_evaluate_items ? "ok" : "missing", nvt, npad,
             mounted ? "ok" : "missing", (int)modfs_count(), isaaclua_load_image_orig ? "ok" : "missing", !!s_use_item + !!s_use_card + !!s_use_pill,
             s_game_exit ? "ok" : "missing", !!s_action_pressed + !!s_action_triggered + !!s_action_value,
             !!s_get_collectible + !!s_spawn_entity + !!s_level_init);
    plugin_log(msg);
    if (!s_evaluate_items && s_cb_used[MC_EVALUATE_CACHE]) overlay_add("MC_EVALUATE_CACHE unavailable", OVERLAY_WARN);
}

/* ------------------------------------------------------------ LoadModule chaining */

typedef u32 (*LoadModuleFn)(struct nnro_Module *, const void *, void *, size_t, int);
static LoadModuleFn s_prev_load;  // a hook installed before ours (isaacqol), if any
static int s_load_depth = 0;

static u32 hk_load_module(struct nnro_Module *m, const void *image, void *buf, size_t size, int flag) {
    if (s_load_depth > 0) return nnro_LoadModule(m, image, buf, size, flag);  // called back by that hook
    s_load_depth++;
    u32 rc = s_prev_load ? s_prev_load(m, image, buf, size, flag) : nnro_LoadModule(m, image, buf, size, flag);
    s_load_depth--;
    if (rc == 0 && !s_ready) on_module_loaded(m);
    return rc;
}

/* Walk the address space and call cb(base) for every static module (rtld, main, subsdk*, sdk). */
static void install_loadmodule_hooks(void) {
    const char *name = "_ZN2nn2ro10LoadModuleEPNS0_6ModuleEPKvPvmi";
    uintptr_t self = (uintptr_t)__module_start, real = (uintptr_t)&nnro_LoadModule;
    MemoryInfo mi;
    u32 pi;
    u64 addr = 0;
    int patched = 0;
    while (qol_svcQueryMemory(&mi, &pi, addr) == 0) {
        if ((mi.type & 0xff) == 3 && mi.perm == 5 && mi.addr != self) {  // CodeStatic, R-X
            ModView v;
            u64 *slot = mod_open(&v, (uintptr_t)mi.addr) ? mod_got_slot(&v, name) : NULL;
            if (slot) {
                uintptr_t cur = (uintptr_t)*slot;
                /* neither the real function nor the module's own lazy PLT stub: another plugin's hook */
                if (cur != real && !same_region((uintptr_t)mi.addr, cur) && !s_prev_load) s_prev_load = (LoadModuleFn)cur;
                *slot = (u64)(uintptr_t)&hk_load_module;
                patched++;
            }
        }
        u64 next = mi.addr + mi.size;
        if (next <= addr) break;
        addr = next;
    }
    plugin_log(patched ? "[isaaclua] LoadModule hooked" : "[isaaclua] LoadModule import not found");
}

/* Called by rtld after all static modules are relocated, before nnMain. */
HIDDEN void isaaclua_init(void) { install_loadmodule_hooks(); }

#ifdef ISAACLUA_HOST_TEST
/* entry points for test/host_test.c (a fake engine on the build machine) */
static void t_noop(void *p) { (void)p; }
bool test_lua_start(void) {
    kb_load_config();
    if (!getenv("ISAACLUA_TEST_DEFER")) return lua_start();
    s_defer_lua = true;  // as on the Switch: the Lua runs after Manager::LoadConfigs
    if (!lua_start()) return false;
    static u8 fake_mgr[0x40000];
    s_manager_load_configs = t_noop;
    hk_manager_load_configs(fake_mgr);
    return !s_defer_lua;
}
void test_set_originals(VoidFn update, VoidFn hud, VoidFn eval, VoidFn player_update) {
    s_game_update = update, s_have_game_update = true;
    s_hud_render = hud, s_evaluate_items = eval;
    s_vt[VT_PLAYER].orig = s_vt[VT_PICKUP].orig = player_update;
}
void test_game_update(void *g) {
    s_game_update(g);
    g_engine_ready = true;
    game_tick();
}
void test_hud_render(void *h) { hk_hud_render(h); }
void *test_modfs_hook(void *orig) { return modfs_hook(orig); }
void test_evaluate_items(void *p) { hk_evaluate_items(p); }
void test_player_update(void *p) { hk_player_update(p); }
void test_pickup_update(void *p) { hk_pickup_update(p); }
/* evaluates a Lua expression, result as a string (static buffer) */
const char *test_lua_eval(const char *expr) {
    static char out[256];
    char code[2048];
    snprintf(code, sizeof code, "return tostring(%s)", expr);
    if (luaL_loadstring(L, code) != LUA_OK || lua_pcall(L, 0, 1, 0) != LUA_OK) snprintf(out, sizeof out, "error: %s", lua_tostring(L, -1));
    else snprintf(out, sizeof out, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    return out;
}
int test_lua_kb(void) { if (L && getenv("FULLGC")) lua_gc(L, LUA_GCCOLLECT, 0); return L ? lua_gc(L, LUA_GCCOUNT, 0) : 0; }
static u64 t_use_item(void *p, int i, u32 f, int s, int v) { (void)p, (void)i, (void)f, (void)s, (void)v; return 1; }
static u64 t_use_card(void *p, int c, u32 f) { (void)p, (void)c, (void)f; return 1; }
static u64 t_use_pill(void *p, int e, int c, u32 f) { (void)p, (void)e, (void)c, (void)f; return 1; }
static void t_exit(void *g, bool s) { (void)g, (void)s; }
static void t_init(void *e, int t, u32 v, u32 st, u32 seed) { (void)e, (void)t, (void)v, (void)st, (void)seed; }
/* returns whether the game's UseActiveItem ran (PRE_USE_ITEM did not cancel) */
bool test_use_item(void *p, int item) {
    s_use_item = t_use_item;
    return hk_use_item(p, item, 0, 0, 0) != 0;
}
u64 test_use_item_result(void *p, int item) { s_use_item = t_use_item; return hk_use_item(p, item, 0, 0, 0); }
void test_use_card(void *p, int card) { s_use_card = t_use_card, hk_use_card(p, card, 0); }
void test_use_pill(void *p, int effect) { s_use_pill = t_use_pill, hk_use_pill(p, effect, 0, 0); }
void test_game_exit(void *g, bool save) { s_game_exit = t_exit, hk_game_exit(g, save); }
static void t_game_init(void *g) { (void)g; }
static void t_start_saved(void *g, const void *st) { (void)g, (void)st; }
/* a run made by Manager::execute_start_game: Game::Init, then StartFromSavedState when continued */
static void t_change_room(void *l, int i, int d) { (void)l, (void)i, (void)d; }
void test_change_room(void *level) { s_level_change_room = t_change_room, hk_level_change_room(level, 0, 0); }
void test_game_start(void *g, bool continued) {
    s_game_init = t_game_init, s_start_saved = t_start_saved;
    hk_game_init(g);
    if (continued) hk_start_saved(g, NULL);
}
static void t_render(void *e, const Vector2 *off) { (void)e, (void)off; }
void test_entity_render(void *e, Vector2 off) {
    s_vt[VT_PLAYER_RENDER].orig = s_vt[VT_PICKUP_RENDER].orig = s_vt[VT_EFFECT_RENDER].orig = (VoidFn)t_render;
    if (ENT_I32(e, kEnt_Type) == 1) hk_player_render(e, &off);
    else if (ENT_I32(e, kEnt_Type) == 5) hk_pickup_render(e, &off);
    else hk_effect_render(e, &off);
}
int test_errors(void) { return s_errors; }
static void t_level_init(void *l) { (void)l; }
/* hook Level::Init (as on Switch); then a call of it */
void test_hook_level_init(void) { s_level_init = t_level_init; }
static void t_game_end(void *g, int ending) { (void)g, (void)ending; }
static bool s_t_awarded;
static void t_clear_award(void *room) { (void)room, s_t_awarded = true; }
bool test_clear_award(void) {  // true when the reward was spawned
    s_t_awarded = false;
    s_clear_award = t_clear_award;
    hk_clear_award(NULL);
    return s_t_awarded;
}
void test_game_end(int ending) { s_game_end = t_game_end, hk_game_end(NULL, ending); }
static int t_get_card(void *pool, u32 seed, bool playing, bool runes, bool only_runes) {
    (void)pool, (void)seed, (void)playing, (void)runes;
    return only_runes ? 32 : 1;  // Rune of Hagalaz / The Fool
}
int test_get_card(bool only_runes) { s_get_card = t_get_card; return hk_get_card(NULL, 7, true, true, only_runes); }
static void t_load_stage(void *rc, u32 stage, int mode) { (void)rc, (void)stage, (void)mode; }
static char s_t_rooms[32];
static void t_mm_rooms(void *mm, u32 stage, int mode) { (void)mm, snprintf(s_t_rooms, sizeof s_t_rooms, "%u/%d", stage, mode); }
/* a stage load with n content mods: "stage/mode" UpdateRooms was called with, or "" */
const char *test_load_stage(int n, u32 stage, int mode) {
    int had = s_content_mods;
    s_content_mods = n, s_load_stage_binary = t_load_stage, s_mm_rooms = t_mm_rooms, s_t_rooms[0] = 0;
    hk_load_stage_binary(NULL, stage, mode);
    s_content_mods = had;
    return s_t_rooms;
}
static bool t_select_pickup(u32 seed, u32 *v, u32 *st, bool a, bool b) {
    (void)seed, (void)a, (void)b;
    *v = 20, *st = 1;
    return true;
}
static int t_get_pill(void *pool, u32 seed) { (void)pool, (void)seed; return 3; }
static int t_get_pill_effect(void *pool, int color, void *pl) { (void)pool, (void)color, (void)pl; return 7; }
static bool t_devolve(void *g, void *e) { (void)g, (void)e; return true; }
/* "variant.subtype pill color effect devolved" after the test mod's callbacks */
const char *test_selection(void *pickup) {
    static char out[64];
    u32 v = 0, st = 0;
    s_select_pickup = t_select_pickup, s_get_pill = t_get_pill, s_get_pill_effect = t_get_pill_effect, s_devolve = t_devolve;
    s_init_pickup = pickup;
    hk_select_pickup(1, &v, &st, true, false);
    s_init_pickup = NULL;
    snprintf(out, sizeof out, "%u.%u %d %d %d", v, st, hk_get_pill(NULL, 5), hk_get_pill_effect(NULL, 3, NULL),
             (int)hk_devolve(NULL, pickup));
    return out;
}
void test_familiar_render(void *e) {
    s_vt[VT_FAMILIAR_RENDER].orig = (VoidFn)t_render;
    Vector2 off = {0, 0};
    hk_familiar_render(e, &off);
}
static void t_npc_update(void *e) { (void)e, plugin_log("npc updated"); }
void test_npc_update(void *e) { s_vt[VT_NPC].orig = (VoidFn)t_npc_update, hk_npc_update(e); }
void test_npc_render(void *e) {
    s_vt[VT_NPC_RENDER].orig = (VoidFn)t_render;
    Vector2 off = {0, 0};
    hk_npc_render(e, &off);
}
static void t_npc_death(void *e) { (void)e; }
void test_npc_death(void *e) { s_npc_death = t_npc_death, hk_npc_death(e); }
static void t_entity_kill(void *e) { ((u8 *)e)[0x1c3] = 1; }
void test_entity_remove(void *e) { s_clear_refs_got = t_npc_death, hk_clear_refs(e); }
static void *t_spawn_ex(void *g, u32 t, u32 v, const Vector2 *p, const Vector2 *vel, void *sp, u32 st, u32 seed,
                       u32 f) {
    (void)g, (void)p, (void)vel, (void)sp, (void)seed, (void)f;
    static char line[64];
    snprintf(line, sizeof line, "spawn %u.%u.%u", t, v, st);
    plugin_log(line);
    return NULL;
}
void test_spawn_ex(void) {
    s_spawn_ex = t_spawn_ex;
    Vector2 z = {0, 0};
    hk_spawn_ex(NULL, 5, 20, &z, &z, NULL, 1, 7, 0);
}
void test_entity_kill(void *e) { s_entity_kill_got = t_entity_kill, hk_entity_kill(e), hk_entity_kill(e); }
void test_level_init(void) { hk_level_init(NULL); }
const char *test_perf_line(void) { return s_perf_line; }
static int t_roll_item;
static int t_get_collectible(void *pool, int type, u32 seed, u32 flags, int def) {
    (void)pool, (void)type, (void)seed, (void)flags, (void)def;
    return t_roll_item;
}
/* the game rolls `item` from `pool`: what the caller gets */
int test_get_collectible(int pool, int item) {
    s_get_collectible = t_get_collectible, t_roll_item = item;
    return hk_get_collectible(NULL, pool, 1234, 0, 25);
}
static RoomEntry t_spawned;
static u64 t_spawn_entity(void *room, u32 grid, const RoomEntry *e, u32 seed, void **out, bool b) {
    (void)room, (void)grid, (void)seed, (void)out, (void)b;
    t_spawned = *e;
    return 0;
}
/* a room layout entry spawns: what the game spawns (type, variant, subtype packed as t*1e8 + v*1e4 + s) */
long long test_spawn_entity(int type, int variant, int subtype, int grid) {
    s_spawn_entity = t_spawn_entity;
    RoomEntry e = {(u32)type, (uint16_t)variant, (uint16_t)subtype, 1.0f};
    hk_spawn_entity(NULL, (u32)grid, &e, 99, NULL, false);
    return (long long)t_spawned.type * 100000000LL + t_spawned.variant * 10000LL + t_spawned.subtype;
}
static int t_collisions;
static bool t_collision(void *e, void *o, bool low) { (void)e, (void)o, (void)low; t_collisions++; return false; }
/* returns the collision result, *game_ran whether the game's own handling ran */
bool test_pickup_collision(void *pickup, void *other, bool *game_ran) {
    s_vt[VT_PICKUP_COLLISION].orig = (VoidFn)t_collision;
    int before = t_collisions;
    bool r = hk_pickup_collision(pickup, other, true);
    *game_ran = t_collisions > before;
    return r;
}
static int t_damage_calls;
static bool t_take_dmg(void *e, float a, u64 f, const void *r, int c) { (void)e, (void)a, (void)f, (void)r, (void)c; t_damage_calls++; return true; }
/* ENTITY_TAKE_DMG around a player's TakeDamage; *game_ran: the game's own TakeDamage was called */
bool test_take_damage(void *e, float amount, bool *game_ran) {
    s_vt[VT_DMG_PLAYER].orig = (VoidFn)t_take_dmg;
    u8 ref[0x30] = {0};
    int before = t_damage_calls;
    bool r = hk_dmg_player(e, amount, 0, ref, 30);
    *game_ran = t_damage_calls > before;
    return r;
}
void test_entity_init(void *e) {
    s_vt[VT_PLAYER_INIT].orig = s_vt[VT_PICKUP_INIT].orig = (VoidFn)t_init;
    if (ENT_I32(e, kEnt_Type) == 1) hk_player_init(e, 1, 0, 0, 1);
    else hk_pickup_init(e, ENT_I32(e, kEnt_Type), (u32)ENT_I32(e, kEnt_Variant), 0, 1);
}
#endif

#include "daily_hooks.inc"
#include "repplus_hooks.inc"
