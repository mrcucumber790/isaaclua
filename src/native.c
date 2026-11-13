/* core.*: a small FFI for the Lua half of the API (src/api.lua). It reads and writes engine memory
 * and calls exported game functions by symbol, so most of the PC API is plain Lua over the offsets
 * in game.h / api.lua instead of one C binding per method.
 *
 * Calls: every argument goes in an integer or a float register, never on the stack (at most 8 of
 * each, as AAPCS64 and SysV both allow), so one cast covers every signature we use. A float travels
 * in the low 32 bits of a double register. Signature strings: "<ret><args>", e.g. "bpib" for
 * bool f(void *, int, bool).
 *   args: p pointer (lightuserdata, entity, buffer or nil), i int, u unsigned, b bool, f float,
 *         s const char *, v Vector2 by value (two float registers: an HFA on aarch64),
 *         V const Vector2 & (pointer to a copy)
 *   ret:  _ void, i int, u unsigned, b bool, f float, p pointer (nil if NULL), e entity,
 *         v Vector2 by value, S std::string returned by value (sret),
 *         R any struct returned through x8 (up to 256 bytes), pushed as a new buffer */
#include <stdlib.h>
#include <string.h>

#include "isaaclua.h"
#include "lauxlib.h"
#include "lua.h"
#include "nn.h"

#define BUF_MT "isaaclua.Buffer"

void *_Znam(size_t);
void _ZdlPv(void *);

typedef struct { u8 b[24]; } Ret24;  /* std::string: returned through x8 / a hidden pointer */
typedef u64 (*CallI)(u64, u64, u64, u64, u64, u64, u64, u64, double, double, double, double, double, double, double, double,
                     u64, u64, u64, u64, u64, u64);
typedef double (*CallF)(u64, u64, u64, u64, u64, u64, u64, u64, double, double, double, double, double, double, double, double,
                     u64, u64, u64, u64, u64, u64);
typedef Vector2 (*CallV)(u64, u64, u64, u64, u64, u64, u64, u64, double, double, double, double, double, double, double, double,
                     u64, u64, u64, u64, u64, u64);
typedef struct { u64 l, h; } Ret16;  /* bitset128_t (TearFlags): returned in x0:x1 */
typedef Ret16 (*CallB)(u64, u64, u64, u64, u64, u64, u64, u64, double, double, double, double, double, double, double, double,
                     u64, u64, u64, u64, u64, u64);
typedef struct { u8 b[256]; } Ret256;  /* big structs (TearParams...): the callee writes through x8 */
typedef Ret256 (*CallR)(u64, u64, u64, u64, u64, u64, u64, u64, double, double, double, double, double, double, double, double,
                     u64, u64, u64, u64, u64, u64);
typedef Ret24 (*CallS)(u64, u64, u64, u64, u64, u64, u64, u64, double, double, double, double, double, double, double, double,
                     u64, u64, u64, u64, u64, u64);

static double fbits(float f) {
    union { double d; u64 u; } r = {0};
    memcpy(&r.u, &f, 4);
    return r.d;
}
static float bitsf(double d) {
    float f;
    memcpy(&f, &d, 4);
    return f;
}

static void *buf_ptr(void *ud) { return (void *)(((uintptr_t)ud + 15) & ~(uintptr_t)15); }

/* any pointer-ish Lua value */
static void *to_ptr(lua_State *L, int i) {
    switch (lua_type(L, i)) {
    case LUA_TNIL: case LUA_TNONE: return NULL;
    case LUA_TLIGHTUSERDATA: return lua_touserdata(L, i);
    case LUA_TUSERDATA: {
        void *b = luaL_testudata(L, i, BUF_MT);
        if (b) return buf_ptr(b);
        return api_entity_ptr(L, i);
    }
    case LUA_TNUMBER: return (void *)(uintptr_t)lua_tointeger(L, i);
    default: luaL_argerror(L, i, "pointer expected"); return NULL;
    }
}

static void push_ptr(lua_State *L, void *p) {
    if (p) lua_pushlightuserdata(L, p);
    else lua_pushnil(L);
}

static Vector2 to_vec(lua_State *L, int i) {
    luaL_checktype(L, i, LUA_TTABLE);
    lua_getfield(L, i, "X");
    lua_getfield(L, i, "Y");
    Vector2 v = {(float)luaL_optnumber(L, -2, 0), (float)luaL_optnumber(L, -1, 0)};
    lua_pop(L, 2);
    return v;
}

/* core.sym(name) -> function pointer or nil. Game functions whose callbacks the loader fires from a GOT
 * hook (UseCard -> USE_CARD...) are called through that hook, as PC's Lua API reaches its callbacks too */
const char *const isaaclua_sym_hooked_name[ISAACLUA_SYM_HOOKED] = {
    "_ZN15IsaacRepentance13Entity_Player7UseCardENS_5eCardEj",
    "_ZN15IsaacRepentance13Entity_Player7UsePillENS_11ePillEffectENS_10ePillColorEj",
    "_ZN15IsaacRepentance6Entity10TakeDamageEfmRKNS0_9EntityRefEi",
    "_ZN15IsaacRepentance13Entity_Player10TakeDamageEfmRKNS_6Entity9EntityRefEi",
    "_ZN15IsaacRepentance10Entity_NPC10TakeDamageEfmRKNS_6Entity9EntityRefEi",
    "_ZN15IsaacRepentance15Entity_Familiar10TakeDamageEfmRKNS_6Entity9EntityRefEi",
    "_ZN15IsaacRepentance13Entity_Pickup10TakeDamageEfmRKNS_6Entity9EntityRefEi",
    "_ZN15IsaacRepentance11Entity_Slot10TakeDamageEfmRKNS_6Entity9EntityRefEi",
};
void *isaaclua_sym_hooked[ISAACLUA_SYM_HOOKED];
/* every game function the loader GOT-hooks (hook_got in main.c): Lua calls go through the hook too, so a mod
 * calling Room:SpawnClearAward() gets PRE_SPAWN_CLEAN_AWARD as on PC, where the callback is in the function */
#define ISAACLUA_GOT_HOOKS 256
static const char *s_got_name[ISAACLUA_GOT_HOOKS];
static void *s_got_hook[ISAACLUA_GOT_HOOKS];
static int s_got_n;
void isaaclua_sym_got_hooked(const char *name, void *hook) {
    for (int i = 0; i < s_got_n; i++)
        if (strcmp(s_got_name[i], name) == 0) { s_got_hook[i] = hook; return; }
    if (s_got_n < ISAACLUA_GOT_HOOKS) { s_got_name[s_got_n] = name; s_got_hook[s_got_n++] = hook; }
}
static int c_sym(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    for (int i = 0; i < s_got_n; i++)
        if (strcmp(name, s_got_name[i]) == 0) {
            push_ptr(L, s_got_hook[i]);
            return 1;
        }
    for (int i = 0; i < ISAACLUA_SYM_HOOKED; i++)
        if (isaaclua_sym_hooked[i] && strcmp(name, isaaclua_sym_hooked_name[i]) == 0) {
            push_ptr(L, isaaclua_sym_hooked[i]);
            return 1;
        }
    push_ptr(L, api_sym(name));
    return 1;
}

static void push_std_string(lua_State *L, const StdString *s) {
    const u8 *b = (const u8 *)s;
    if (b[0] & 1) lua_pushlstring(L, (const char *)s->w[2], (size_t)s->w[1]);
    else lua_pushlstring(L, (const char *)b + 1, b[0] >> 1);
}

/* core.call(fn, sig, ...) */
static int c_call(lua_State *L) {
    void *fn = lua_touserdata(L, 1);
    const char *sig = luaL_checkstring(L, 2);
    if (!fn) return luaL_error(L, "core.call: no function (%s)", sig);
    u64 x[14] = {0};  // x0-x7, then 6 on the stack (AAPCS64: 8-byte slots), e.g. Game::BombExplosionEffects
    double d[8] = {0};
    Vector2 vtmp[4];
    int nx = 0, nd = 0, nv = 0, arg = 3;
    for (const char *c = sig + 1; *c; c++, arg++) {
        if (nx >= 14 || nd >= 7 || nv >= 4) return luaL_error(L, "core.call: too many arguments (%s)", sig);
        switch (*c) {
        case 'p': x[nx++] = (u64)(uintptr_t)to_ptr(L, arg); break;
        case 'i': x[nx++] = (u64)(s64)luaL_checkinteger(L, arg); break;
        case 'u': x[nx++] = (u64)(u32)luaL_checkinteger(L, arg); break;
        case 'b': x[nx++] = lua_toboolean(L, arg) ? 1 : 0; break;
        case 'f': d[nd++] = fbits((float)luaL_checknumber(L, arg)); break;
        case 's': x[nx++] = (u64)(uintptr_t)luaL_checkstring(L, arg); break;
        case 'v': {
            Vector2 v = to_vec(L, arg);
            d[nd++] = fbits(v.x);
            d[nd++] = fbits(v.y);
            break;
        }
        case 'V':
            vtmp[nv] = to_vec(L, arg);
            x[nx++] = (u64)(uintptr_t)&vtmp[nv++];
            break;
        default: return luaL_error(L, "core.call: bad signature %s", sig);
        }
    }
#define ARGS x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7], d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], x[8], x[9], x[10], x[11], x[12], x[13]
    switch (sig[0]) {
    case 'f': lua_pushnumber(L, bitsf(((CallF)fn)(ARGS))); return 1;
    case 'v': {
        Vector2 v = ((CallV)fn)(ARGS);
        api_push_vector(L, v);
        return 1;
    }
    case 'R': {
        Ret256 r = ((CallR)fn)(ARGS);
        void *b = lua_newuserdata(L, sizeof r + 16);
        memset(b, 0, sizeof r + 16);
        memcpy(buf_ptr(b), &r, sizeof r);
        luaL_setmetatable(L, BUF_MT);
        return 1;
    }
    case 'B': {  /* two integers: the low and high 64 bits */
        Ret16 r = ((CallB)fn)(ARGS);
        lua_pushinteger(L, (lua_Integer)r.l);
        lua_pushinteger(L, (lua_Integer)r.h);
        return 2;
    }
    case 'S': {
        Ret24 r = ((CallS)fn)(ARGS);
        push_std_string(L, (const StdString *)&r);
        if (r.b[0] & 1) _ZdlPv((void *)((const StdString *)&r)->w[2]);
        return 1;
    }
    default: break;
    }
    u64 r = ((CallI)fn)(ARGS);
#undef ARGS
    switch (sig[0]) {
    case '_': return 0;
    case 'i': lua_pushinteger(L, (s32)r); return 1;
    case 'u': lua_pushinteger(L, (u32)r); return 1;
    case 'b': lua_pushboolean(L, (r & 0xff) != 0); return 1;
    case 'p': push_ptr(L, (void *)(uintptr_t)r); return 1;
    case 'e': api_push_entity(L, (u8 *)(uintptr_t)r); return 1;
    default: return luaL_error(L, "core.call: bad return type %s", sig);
    }
}

/* core.peek(ptr, off, type) -> value (nil if ptr is NULL) */
static int c_has_query_radius(lua_State *L) { lua_pushboolean(L, G.query_radius != NULL); return 1; }

/* core.mem_ok(ptr[, size]) -> true when the bytes are mapped (stale RoomDescriptor pointers at mod load) */
static int c_mem_ok(lua_State *L) {
    u8 *p = (u8 *)to_ptr(L, 1);
    lua_pushboolean(L, p && isaaclua_mem_ok(p, (size_t)luaL_optinteger(L, 2, 8), false));
    return 1;
}

static int c_peek(lua_State *L) {
    u8 *p = (u8 *)to_ptr(L, 1);
    if (!p) return 0;
    p += luaL_optinteger(L, 2, 0);
    const char *t = luaL_checkstring(L, 3);
    if (!isaaclua_mem_ok(p, 8, false)) return luaL_error(L, "core.peek: bad pointer %p (%s)", (void *)p, t);
    if (!strcmp(t, "i32")) lua_pushinteger(L, *(s32 *)p);
    else if (!strcmp(t, "u32")) lua_pushinteger(L, *(u32 *)p);
    else if (!strcmp(t, "f32")) lua_pushnumber(L, *(float *)p);
    else if (!strcmp(t, "u8")) lua_pushinteger(L, *p);
    else if (!strcmp(t, "i8")) lua_pushinteger(L, *(int8_t *)p);
    else if (!strcmp(t, "b")) lua_pushboolean(L, *p != 0);
    else if (!strcmp(t, "u16")) lua_pushinteger(L, *(uint16_t *)p);
    else if (!strcmp(t, "i16")) lua_pushinteger(L, *(int16_t *)p);
    else if (!strcmp(t, "i64")) lua_pushinteger(L, *(s64 *)p);
    else if (!strcmp(t, "p")) push_ptr(L, *(void **)p);
    else if (!strcmp(t, "e")) api_push_entity(L, *(u8 **)p);
    else if (!strcmp(t, "str")) push_std_string(L, (const StdString *)p);
    else if (!strcmp(t, "cstr")) lua_pushstring(L, *(const char **)p);
    else if (!strcmp(t, "vec")) api_push_vector(L, *(Vector2 *)p);
    else return luaL_error(L, "core.peek: bad type %s", t);
    return 1;
}

/* core.poke(ptr, off, type, value) */
static int c_poke(lua_State *L) {
    u8 *p = (u8 *)to_ptr(L, 1);
    if (!p) return 0;
    p += luaL_optinteger(L, 2, 0);
    const char *t = luaL_checkstring(L, 3);
    if (!isaaclua_mem_ok(p, 8, true)) return luaL_error(L, "core.poke: bad pointer %p (%s)", (void *)p, t);
    if (!strcmp(t, "i32") || !strcmp(t, "u32")) *(u32 *)p = (u32)luaL_checkinteger(L, 4);
    else if (!strcmp(t, "f32")) *(float *)p = (float)luaL_checknumber(L, 4);
    else if (!strcmp(t, "u8") || !strcmp(t, "i8")) *p = (u8)luaL_checkinteger(L, 4);
    else if (!strcmp(t, "b")) *p = lua_toboolean(L, 4) ? 1 : 0;
    else if (!strcmp(t, "u16") || !strcmp(t, "i16")) *(uint16_t *)p = (uint16_t)luaL_checkinteger(L, 4);
    else if (!strcmp(t, "vec")) *(Vector2 *)p = to_vec(L, 4);
    else if (!strcmp(t, "i64")) *(u64 *)p = (u64)luaL_checkinteger(L, 4);
    else if (!strcmp(t, "p")) *(void **)p = to_ptr(L, 4);
    else return luaL_error(L, "core.poke: bad type %s", t);
    return 0;
}

/* core.offset(ptr, n) -> ptr + n (nil stays nil) */
static int c_offset(lua_State *L) {
    u8 *p = (u8 *)to_ptr(L, 1);
    push_ptr(L, p ? p + luaL_checkinteger(L, 2) : NULL);
    return 1;
}

/* core.buf(size) -> zeroed 16-aligned scratch memory owned by Lua; core.ptr(x) -> its address */
static int c_buf(lua_State *L) {
    size_t n = (size_t)luaL_checkinteger(L, 1);
    void *b = lua_newuserdata(L, n + 16);
    memset(b, 0, n + 16);
    luaL_setmetatable(L, BUF_MT);
    return 1;
}
static int c_ptr(lua_State *L) {
    push_ptr(L, to_ptr(L, 1));
    return 1;
}

static int c_entity(lua_State *L) {  // core.entity(ptr) -> Entity or nil
    api_push_entity(L, (u8 *)to_ptr(L, 1));
    return 1;
}
static int c_game(lua_State *L) {
    push_ptr(L, game_ptr());
    return 1;
}
static int c_room(lua_State *L) {
    push_ptr(L, room_ptr());
    return 1;
}
/* core.grid_at(i) -> the room's GridEntity* at index i, nil when empty or outside the grid (Room::GetGridEntity) */
static int c_grid_at(lua_State *L) {
    u8 *r = room_ptr();
    lua_Integer i = luaL_checkinteger(L, 1);
    if (!r || i < 0 || i >= (lua_Integer)(*(s32 *)(r + 0x14)) * (*(s32 *)(r + 0x18))) return 0;
    push_ptr(L, *(u8 **)(r + 0x30 + 8 * i));
    return 1;
}
/* core.grid_desc(p) -> GridEntityDesc as a table (GridEntity +8: Type, Variant, State, SpawnCount, SpawnSeed,
 * VarData, Initialized, VariableSeed): StageAPI reads grid.Desc for every grid cell every update */
static int c_grid_desc(lua_State *L) {
    const u8 *p = (const u8 *)to_ptr(L, 1);
    if (!p) return 0;
    lua_createtable(L, 0, 8);
    static const char *const kI[] = {"Type", "Variant", "State", "SpawnCount"};
    for (int i = 0; i < 4; i++) {
        lua_pushinteger(L, *(const s32 *)(p + 8 + 4 * i));
        lua_setfield(L, -2, kI[i]);
    }
    lua_pushinteger(L, *(const u32 *)(p + 0x18));
    lua_setfield(L, -2, "SpawnSeed");
    lua_pushinteger(L, *(const s32 *)(p + 0x1c));
    lua_setfield(L, -2, "VarData");
    lua_pushboolean(L, p[0x20] != 0);
    lua_setfield(L, -2, "Initialized");
    lua_pushinteger(L, *(const u32 *)(p + 0x24));
    lua_setfield(L, -2, "VariableSeed");
    return 1;
}
static int c_manager(lua_State *L) {
    push_ptr(L, manager_ptr());
    return 1;
}
/* core.restart(): what holding R does in Game::Update: Manager::RestartGame(a copy of the game's Seeds,
 * false, false). Seeds is passed by value, so by pointer to a temporary that we construct and destroy. */
typedef void (*SeedsCopyFn)(void *, const void *);
typedef void (*SeedsDtorFn)(void *);
typedef void (*RestartFn)(void *, void *, bool, bool);
static int c_restart(lua_State *L) {
    SeedsCopyFn copy = (SeedsCopyFn)api_sym("_ZN15IsaacRepentance5SeedsC1ERKS0_");
    SeedsDtorFn dtor = (SeedsDtorFn)api_sym("_ZN15IsaacRepentance5SeedsD1Ev");
    RestartFn restart = (RestartFn)api_sym("_ZN15IsaacRepentance7Manager11RestartGameENS_5SeedsEbb");
    u8 *g = game_ptr(), *m = manager_ptr();
    if (!copy || !dtor || !restart || !g || !m) return lua_pushboolean(L, 0), 1;
    static u8 tmp[0x400] __attribute__((aligned(16)));  // Seeds is about 0x70 bytes
    copy(tmp, g + 0x25d40);
    restart(m, tmp, false, false);
    dtor(tmp);
    lua_pushboolean(L, 1);
    return 1;
}
/* core.level_init(): Level::Init on the game's level (Game+0), through main.c's hook */
static int c_level_init(lua_State *L) {
    u8 *g = game_ptr();
    if (g) isaaclua_level_init(g);
    lua_pushboolean(L, g != NULL);
    return 1;
}
/* core.spawn(type, variant, subtype, pos, vel, spawner, seed) -> entity: Game::SpawnEx(type, variant,
 * &pos, &vel, spawner, subtype, seed, 0). Its ninth argument goes on the stack, which core.call does not do. */
typedef u8 *(*SpawnExFn)(void *, u32, u32, const Vector2 *, const Vector2 *, void *, u32, u32, u32);
static int c_spawn(lua_State *L) {
    static SpawnExFn spawn;
    if (!spawn) spawn = (SpawnExFn)api_sym("_ZN15IsaacRepentance4Game7SpawnExENS_11eEntityTypeEjRKN4KAGE4Math7Vector2ES6_PNS_6EntityEjjj");
    u8 *g = game_ptr();
    if (!spawn || !g) return 0;
    Vector2 pos = to_vec(L, 4), vel = lua_istable(L, 5) ? to_vec(L, 5) : (Vector2){0, 0};
    u8 *e = spawn(g, (u32)luaL_checkinteger(L, 1), (u32)luaL_optinteger(L, 2, 0), &pos, &vel,
                  lua_isnoneornil(L, 6) ? NULL : api_entity_ptr(L, 6), (u32)luaL_optinteger(L, 3, 0),
                  (u32)luaL_optinteger(L, 7, 1), 0);
    api_push_entity(L, e);
    return 1;
}
extern bool g_perf;
static int c_tick(lua_State *L) {  // core.tick() -> raw system ticks (perf counters)
    lua_pushinteger(L, nnosGetSystemTick());
    return 1;
}
static int c_perf(lua_State *L) {  // core.perf() -> isaaclua.ini "perf = 1"; core.perf(on) switches it (debug console)
    if (lua_gettop(L) >= 1) g_perf = lua_toboolean(L, 1);
    lua_pushboolean(L, g_perf);
    return 1;
}
const char *perf_line(void);
static int c_perf_line(lua_State *L) {  // core.perf_line() -> the timings line the overlay shows
    lua_pushstring(L, perf_line());
    return 1;
}
extern int g_autotest;
void autotest_log(const char *line, bool flush);
int overlay_errors(void);
static int c_autotest(lua_State *L) {  // core.autotest() -> isaaclua.ini "autotest" minutes (0: off)
    lua_pushinteger(L, g_autotest);
    return 1;
}
static int c_autolog(lua_State *L) {  // core.autolog(line, flush)
    autotest_log(luaL_checkstring(L, 1), lua_toboolean(L, 2));
    return 0;
}
static int c_errors(lua_State *L) {  // core.errors() -> errors shown since launch
    lua_pushinteger(L, overlay_errors());
    return 1;
}
#ifdef ISAACLUA_DEBUG
extern bool g_autotest_shots;
static int c_autotest_shots(lua_State *L) {  // core.autotest_shots() -> isaaclua.ini "autotest_shots" (default on)
    lua_pushboolean(L, g_autotest_shots);
    return 1;
}
int kb_button_bit(const char *s);
void remote_press(u32 mask, int frames);
static int c_remote_press(lua_State *L) {  // core.remote_press("A" / "A+B", frames): pad buttons held down
    char buf[64];
    snprintf(buf, sizeof buf, "%s", luaL_checkstring(L, 1));
    u32 mask = 0;
    for (char *t = buf, *e; *t; t = e) {
        e = t + strcspn(t, "+ ");
        if (*e) *e++ = 0;
        if (!*t) continue;
        int b = kb_button_bit(t);
        if (b < 0) return luaL_error(L, "unknown button %s", t);
        mask |= 1u << b;
    }
    remote_press(mask, (int)luaL_optinteger(L, 2, 6));
    return 0;
}
static int c_screenshot(lua_State *L) {  // core.screenshot() -> 0 or the nn::Result: what is on screen to the album
    static bool inited;
    if (!inited) inited = true, nnalbum_Initialize();
    lua_pushinteger(L, nnalbum_SaveCurrentScreenshot(0));  // no "saved" notice on screen
    return 1;
}
#endif
#ifdef ISAACLUA_DEBUG
int autotest_batch(const char **names, int *count);
void autotest_next(const char *result);
static int c_autotest_batch(lua_State *L) {  // core.autotest_batch() -> batch (1..), batches, its mods (0: no batches)
    const char *names;
    int n, b = autotest_batch(&names, &n);
    lua_pushinteger(L, b);
    lua_pushinteger(L, n);
    lua_pushstring(L, names);
    return 3;
}
static int c_autotest_next(lua_State *L) {  // core.autotest_next(result): this batch is done, the next one
    autotest_next(luaL_checkstring(L, 1));
    return 0;
}
#endif
static int c_memfree(lua_State *L) {  // core.memfree() -> the largest block malloc gives now, in MB
    int lo = 0, hi = 4096;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        void *p = malloc((size_t)mid << 20);
        if (p) free(p), lo = mid;
        else hi = mid - 1;
    }
    lua_pushinteger(L, lo);
    return 1;
}
static u64 svc_info(u32 id) {  // svcGetInfo(id, current process, 0); 0 on error
#ifndef __aarch64__
    (void)id;
    return 0;  // host tests
#else
    register u64 x0 __asm__("x0") = 0, x1 __asm__("x1") = id, x2 __asm__("x2") = 0xffff8001, x3 __asm__("x3") = 0;
    __asm__ volatile("svc 0x29" : "+r"(x0), "+r"(x1) : "r"(x2), "r"(x3) : "memory");
    return x0 ? 0 : x1;
#endif
}
/* core.meminfo() -> MB the system gives this process in total, MB of it in use (svcGetInfo 6 and 7); the game
 * (nninitStartup) takes all of it at launch: malloc heap = all - 32 MB, nn::init allocator = 32 MB. */
static int c_meminfo(lua_State *L) {
    lua_pushinteger(L, (lua_Integer)(svc_info(6) >> 20));
    lua_pushinteger(L, (lua_Integer)(svc_info(7) >> 20));
    return 2;
}
/* core.memtotal() -> MB malloc gives in all now (1 MB blocks, then 64 KB ones, freed at once): the heap is
 * fragmented, so the largest block (memfree) says little about how much is left. */
static int c_memtotal(lua_State *L) {
    void **head = NULL;
    size_t kb = 0;
    for (size_t sz = 1 << 20; sz >= (64 << 10); sz >>= 4)
        for (void **p; kb < (4u << 20) && (p = malloc(sz)); kb += sz >> 10) *p = head, head = p;
    while (head) {
        void **n = *head;
        free(head);
        head = n;
    }
    lua_pushinteger(L, (lua_Integer)(kb >> 10));
    return 1;
}
static int c_missing(lua_State *L) {  // core.missing(what): one "not on Switch yet" line per name
    api_report_missing(L, luaL_checkstring(L, 1));
    return 0;
}
/* type() for mods: grid and mod entities are Lua tables here, userdata on PC (Da Rules tells its own wrappers by
 * type() == "table"). In C: Fiend Folio's CustomHealthAPI calls type() so often that the Lua version was 12%
 * of all Lua time (HW 0.99.42). Upvalue 1: the set of metatables that answer "userdata". */
static int c_type_impl(lua_State *L) {
    luaL_checkany(L, 1);
    int t = lua_type(L, 1);
    if (t == LUA_TTABLE && lua_getmetatable(L, 1)) {
        if (lua_rawget(L, lua_upvalueindex(1)) != LUA_TNIL) {
            lua_pushliteral(L, "userdata");
            return 1;
        }
        lua_pop(L, 1);
    }
    lua_pushstring(L, lua_typename(L, t));
    return 1;
}
static int c_make_type(lua_State *L) {  // core.make_type(set of metatables) -> type function
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushvalue(L, 1);
    lua_pushcclosure(L, c_type_impl, 1);
    return 1;
}
static int c_ms(lua_State *L) {  // core.ms() -> milliseconds since boot
    s64 f = nnosGetSystemTickFrequency();
    lua_pushinteger(L, f ? nnosGetSystemTick() / (f / 1000) : 0);
    return 1;
}

static int c_resolve(lua_State *L) {  // core.resolve(path) -> real path of a mod file, or nil
    char buf[1024];
    const char *r = modfs_resolve(luaL_checkstring(L, 1), buf, sizeof buf);
    if (r) lua_pushstring(L, r);
    else lua_pushnil(L);
    return 1;
}

/* core.language() -> Options.Language: isaaclua.ini, else the game's (Manager::GetLanguageCode) */
static int c_language(lua_State *L) {
    if (g_mod_language[0]) {
        lua_pushstring(L, g_mod_language);
        return 1;
    }
    const char *(*code)(void *) = (const char *(*)(void *))api_sym("_ZN15IsaacRepentance7Manager15GetLanguageCodeEv");
    const char *c = code && manager_ptr() ? code(manager_ptr()) : NULL;
    lua_pushstring(L, c && c[0] && strcmp(c, "XX") != 0 ? c : "en");
    return 1;
}

/* core.ui_scale(k): draw the running mod's sprites and text k times smaller around the screen centre */
/* core.ui_scale("pc"): the running mod sees PC's 480x270 screen (isaaclua.ini pc_screen = 1, sprite.c g_ui_pc) */
static int c_ui_scale(lua_State *L) {
    if (lua_type(L, 1) == LUA_TSTRING) {
        g_ui_pc = true, g_ui_scale = 1.0f;
        return 0;
    }
    g_ui_pc = false;
    g_ui_scale = (float)luaL_optnumber(L, 1, 1.0);
    return 0;
}
/* core.pc_screen() -> isaaclua.ini pc_screen = 1; core.ui_pc() -> the running mod sees 480x270 now */
static int c_pc_screen(lua_State *L) { lua_pushboolean(L, g_pc_screen); return 1; }
static int c_ui_pc(lua_State *L) { lua_pushboolean(L, g_ui_pc); return 1; }
/* core.ui_unscale(x, y): a real screen position or offset in the running mod's screen space */
static int c_ui_unscale(lua_State *L) {
    lua_pushnumber(L, ui_unscale_x((float)luaL_checknumber(L, 1)));
    lua_pushnumber(L, ui_unscale_y((float)luaL_checknumber(L, 2)));
    return 2;
}
/* core.menu_scale() -> isaaclua.ini "menu_scale", else what fits a PC 480x270 menu on this screen */
static int c_menu_scale(lua_State *L) {
    float w = G.width ? *G.width : 480.0f, h = G.height ? *G.height : 270.0f, k = 0.95f * (w / 480 < h / 270 ? w / 480 : h / 270);
    lua_pushnumber(L, g_menu_scale > 0 ? g_menu_scale : k < 1 ? k : 1);
    return 1;
}

/* ------------------------------------------------------------ mod save data
 * The game mounts its own save data as "sdmc" (KAGE::Filesys::SaveDataManager, once a user is picked;
 * before that, touching "sdmc:/" aborts the game). Mod:SaveData goes to sdmc:/isaaclua/<mod name>.dat
 * and is committed straight away, so it survives the game being closed. */
#define SAVE_MOUNT "sdmc"
/* A file name in the save data can't be long: nn::fs aborts the game on a longer one (2002-6003, path too long;
 * mods with long names crashed at boot reading their data). Names up to kSaveName characters stay as they are,
 * longer ones are cut and get a hash of the whole name so two such mods don't share a file. */
enum { kSaveName = 56 };
static bool save_path(char *out, size_t n, const char *name) {
    int k = snprintf(out, n, SAVE_MOUNT ":/isaaclua/"), start = k;
    if (n < (size_t)start + kSaveName + 5) return false;
    uint32_t hash = 2166136261u;
    for (const char *c = name; *c; c++) {
        hash = (hash ^ (unsigned char)*c) * 16777619u;
        bool ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-' ||
                  *c == '_' || *c == ' ' || *c == '.';
        if (k - start < kSaveName + 1) out[k++] = ok ? *c : '_';
    }
    if (k - start > kSaveName) k = start + kSaveName - 9 + snprintf(out + start + kSaveName - 9, 10, "_%08x", (unsigned)hash);
    memcpy(out + k, ".dat", 5);
    return true;
}
static int c_save_ready(lua_State *L) {
    lua_pushboolean(L, nnfs_IsMounted(SAVE_MOUNT));
    return 1;
}
/* Mod:SaveData is not written at once: writing and committing the save data takes about 0.1 s on the console,
 * and mods that save every few seconds (or every room) made the game stutter. The data waits here and is written
 * together, one commit, kSaveDelayMs after the first change (isaaclua_save_flush, every frame), so a mod saving
 * often costs one short pause every few seconds at most; reads see it at once. */
enum { kPendMax = 64, kSaveDelayMs = 5000 };
static struct { char path[160]; char *data; size_t len; bool del; } s_pend[kPendMax];
static int s_npend;
static s64 s_pend_since;
static s64 now_ms(void) {
    s64 f = nnosGetSystemTickFrequency();
    return f ? nnosGetSystemTick() / (f / 1000) : 0;
}
static int pend_find(const char *p) {
    for (int i = 0; i < s_npend; i++)
        if (strcmp(s_pend[i].path, p) == 0) return i;
    return -1;
}
static bool save_put(const char *p, const char *data, size_t len) {
    if (nnfs_CreateDirectory(SAVE_MOUNT ":/isaaclua") == 0) nnfs_CommitSaveData(SAVE_MOUNT);  // fails when it exists
    nnfs_DeleteFile(p);
    if (!data) return true;
    bool ok = false;
    nnfs_FileHandle h;
    if (nnfs_CreateFile(p, (int64_t)len) == 0 && nnfs_OpenFile(&h, p, NNFS_OPEN_WRITE) == 0) {
        nnfs_WriteOption opt = {1};
        ok = len == 0 || nnfs_WriteFile(h, 0, data, len, &opt) == 0;
        nnfs_CloseFile(h);
    }
    return ok;
}
/* writes what waits (force: now; else once it has waited kSaveDelayMs); false if a write failed */
bool isaaclua_save_flush(bool force) {
    if (!s_npend || (!force && now_ms() - s_pend_since < kSaveDelayMs) || !nnfs_IsMounted(SAVE_MOUNT)) return true;
    bool ok = true;
    for (int i = 0; i < s_npend; i++) {
        ok = save_put(s_pend[i].path, s_pend[i].del ? NULL : s_pend[i].data, s_pend[i].len) && ok;
        free(s_pend[i].data);
    }
    s_npend = 0;
    ok = nnfs_CommitSaveData(SAVE_MOUNT) == 0 && ok;
    if (!ok) overlay_add("could not save the mods' data (Mod:SaveData)", OVERLAY_WARN);
    return ok;
}
static int c_save_read(lua_State *L) {  // core.save_read(name) -> string, or nil
    char p[160];
    nnfs_FileHandle h;
    int64_t size = 0;
    if (!nnfs_IsMounted(SAVE_MOUNT) || !save_path(p, sizeof p, luaL_checkstring(L, 1))) return 0;
    int k = pend_find(p);
    if (k >= 0) {
        if (s_pend[k].del) return 0;
        lua_pushlstring(L, s_pend[k].data, s_pend[k].len);
        return 1;
    }
    if (nnfs_OpenFile(&h, p, NNFS_OPEN_READ) != 0) return 0;
    char *buf = NULL;
    size_t got = 0;
    if (nnfs_GetFileSize(&size, h) == 0 && size >= 0 && size < (16 << 20) && (buf = malloc((size_t)size + 1)) &&
        (size == 0 || nnfs_ReadFile(&got, h, 0, buf, (size_t)size) == 0))
        lua_pushlstring(L, buf, (size_t)size);
    else
        lua_pushnil(L);
    free(buf);
    nnfs_CloseFile(h);
    return 1;
}
/* A file of the isaaclua save folder (sdmc:/isaaclua/<file>), for the C side: NULL until the save data is
 * mounted or when the file is missing; the caller frees it */
static char *save_read(const char *mount, const char *file) {
    char p[160];
    nnfs_FileHandle h;
    int64_t size = 0;
    snprintf(p, sizeof p, "%s:/isaaclua/%s", mount, file);
    if (!nnfs_IsMounted(mount) || nnfs_OpenFile(&h, p, NNFS_OPEN_READ) != 0) return NULL;
    char *buf = NULL;
    size_t got = 0;
    if (nnfs_GetFileSize(&size, h) == 0 && size >= 0 && size < (1 << 20) && (buf = malloc((size_t)size + 1))) {
        if (size && nnfs_ReadFile(&got, h, 0, buf, (size_t)size) != 0) size = 0;
        buf[size] = 0;
    }
    nnfs_CloseFile(h);
    return buf;
}
char *isaaclua_save_load(const char *file) { return save_read(SAVE_MOUNT, file); }
/* The same file before the game mounts its save data (it reads the mods' content/ before that): the save of
 * the user the system picked at launch (KAGE::System::Manager::Initialize: OpenPreselectedUser, its Uid at
 * KAGE::System::g_Manager +120, as SaveDataManager::Initialize mounts it), mounted under another name and
 * unmounted again. NULL when there is no user yet or no save data. */
static const nnacc_Uid *early_uid(void);
bool isaaclua_save_ready_early(void) {  // the user is known (or the game mounted its save): early reads work
    return nnfs_IsMounted(SAVE_MOUNT) || early_uid() != NULL;
}
void nnacc_Initialize(void) __asm__("_ZN2nn7account10InitializeEv");
uint32_t nnacc_ListAllUsers(int *count, nnacc_Uid *out, int max) __asm__("_ZN2nn7account12ListAllUsersEPiPNS0_3UidEi");
/* the user whose save data the game uses: the one KAGE's manager picked, or, before it picks one (the mods'
 * resources are scanned that early), the console's only user; NULL when there are several and none is picked */
static const nnacc_Uid *early_uid(void) {
    u8 *km = (u8 *)api_sym("_ZN4KAGE6System9g_ManagerE");
    const nnacc_Uid *uid = km ? (const nnacc_Uid *)(km + 120) : NULL;
    if (uid && (uid->hi | uid->lo)) return uid;
    static nnacc_Uid only;
    static int tried;
    if (!tried) {
        tried = 1;
        nnacc_Uid all[8];
        int n = 0;
        nnacc_Initialize();
        if (nnacc_ListAllUsers(&n, all, 8) == 0 && n == 1) only = all[0];
    }
    return (only.hi | only.lo) ? &only : NULL;
}
char *isaaclua_save_load_early(const char *file) {
    if (nnfs_IsMounted(SAVE_MOUNT)) return save_read(SAVE_MOUNT, file);
    const nnacc_Uid *uid = early_uid();
    if (!uid || nnfs_IsMounted("ilboot")) return NULL;
    if (nnfs_MountSaveData("ilboot", uid) != 0) return NULL;
    char *r = save_read("ilboot", file);
    nnfs_Unmount("ilboot");
    return r;
}
static bool save_store_at(const char *mount, const char *file, const char *data, size_t len) {
    char p[160], dir[64];
    snprintf(p, sizeof p, "%s:/isaaclua/%s", mount, file);
    snprintf(dir, sizeof dir, "%s:/isaaclua", mount);
    if (!nnfs_IsMounted(mount)) return false;
    nnfs_CreateDirectory(dir);
    nnfs_DeleteFile(p);
    bool ok = false;
    nnfs_FileHandle h;
    if (nnfs_CreateFile(p, (int64_t)len) == 0 && nnfs_OpenFile(&h, p, NNFS_OPEN_WRITE) == 0) {
        nnfs_WriteOption opt = {1};
        ok = len == 0 || nnfs_WriteFile(h, 0, data, len, &opt) == 0;
        nnfs_CloseFile(h);
    }
    return nnfs_CommitSaveData(mount) == 0 && ok;
}
bool isaaclua_save_store(const char *file, const char *data, size_t len) {
    return save_store_at(SAVE_MOUNT, file, data, len);
}
/* writing before the game mounts its save data, as isaaclua_save_load_early reads (debug build: the autotest
 * notes which mods it is about to load, so a crash while loading them is known at the next start) */
bool isaaclua_save_store_early(const char *file, const char *data, size_t len) {
    if (nnfs_IsMounted(SAVE_MOUNT)) return isaaclua_save_store(file, data, len);
    const nnacc_Uid *uid = early_uid();
    if (!uid || nnfs_IsMounted("ilboot")) return false;
    if (nnfs_MountSaveData("ilboot", uid) != 0) return false;
    bool ok = save_store_at("ilboot", file, data, len);
    nnfs_Unmount("ilboot");
    return ok;
}
static int c_save_write(lua_State *L) {  // core.save_write(name, data or nil) -> ok (written a few seconds later)
    char p[160];
    size_t len = 0;
    const char *data = lua_isnil(L, 2) ? NULL : luaL_checklstring(L, 2, &len);
    if (!nnfs_IsMounted(SAVE_MOUNT) || !save_path(p, sizeof p, luaL_checkstring(L, 1))) return 0;
    int k = pend_find(p);
    if (k < 0 && s_npend == kPendMax) isaaclua_save_flush(true);
    if (k < 0) {
        k = s_npend++;
        snprintf(s_pend[k].path, sizeof s_pend[k].path, "%s", p);
        s_pend[k].data = NULL, s_pend[k].len = 0, s_pend[k].del = true;
        if (k == 0) s_pend_since = now_ms();
    }
    char *copy = data ? malloc(len ? len : 1) : NULL;
    if (data && !copy) return lua_pushboolean(L, 0), 1;
    if (copy && len) memcpy(copy, data, len);
    free(s_pend[k].data);
    s_pend[k].data = copy, s_pend[k].len = len, s_pend[k].del = data == NULL;
    lua_pushboolean(L, 1);
    return 1;
}

/* core.touch() -> x, y of the first finger on the touchscreen in screen units and the finger count, or nothing. The game
 * initialises the touchscreen itself (KAGE::Input::DeviceTouchscreen); mods get it as the PC mouse. */
static int c_touch(lua_State *L) {
    float mx, my;
    int mb;
    if (kb_mouse(&mx, &my, &mb)) {  // the cursor (stick) instead of the touchscreen; count = button
        lua_pushnumber(L, mx);
        lua_pushnumber(L, my);
        lua_pushinteger(L, mb);
        return 3;
    }
    nnhid_TouchScreenState16 st;
    memset(&st, 0, sizeof st);
    nnhid_GetTouchScreenState16(&st);
    if (st.count <= 0) return 0;
    float w = G.width ? *G.width : 480.0f, h = G.height ? *G.height : 270.0f;
    lua_pushnumber(L, st.touches[0].x * w / 1280.0f);
    lua_pushnumber(L, st.touches[0].y * h / 720.0f);
    lua_pushinteger(L, st.count);
    return 3;
}

extern char api_ext_key[3];
int api_ent_dispatch(lua_State *L);
/* core.entity_ext(methods, getters, setters): Entity keys implemented in Lua */
static int c_entity_ext(lua_State *L) {
    static const char *const kTabs[] = {"isaaclua.entmeth", "isaaclua.entget", "isaaclua.entset"};
    for (int i = 0; i < 3; i++) {
        luaL_checktype(L, i + 1, LUA_TTABLE);
        lua_pushvalue(L, i + 1);
        lua_setfield(L, LUA_REGISTRYINDEX, kTabs[i]);
        lua_pushvalue(L, i + 1);
        lua_rawsetp(L, LUA_REGISTRYINDEX, &api_ext_key[i]);  // api.c ext_lookup
    }
    return 0;
}

/* core.box(mt, fields) -> a userdata with metatable mt holding the table fields (PC API classes are
 * userdata, and IsaacScript checks that); core.unbox(u) -> fields */
static int c_box(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_newuserdata(L, 0);
    lua_pushvalue(L, 2);
    lua_setuservalue(L, -2);
    lua_pushvalue(L, 1);
    lua_setmetatable(L, -2);
    return 1;
}
static int c_unbox(lua_State *L) {
    if (lua_type(L, 1) != LUA_TUSERDATA) return lua_pushnil(L), 1;
    lua_getuservalue(L, 1);
    return 1;
}

/* core.seed_effect(seeds, effect, add): Seeds::AddSeedEffect / RemoveSeedEffect by number (the game's own take
 * a seed code). std::set<eSeedEffect> at Seeds+0x58 {begin node, root (end node's left), size}; nodes {left,
 * right, parent, black, value +0x1c}: the same steps as the game's AddSeedEffect, with the SDK's libc++ */
typedef struct TreeNode { struct TreeNode *left, *right, *parent; u8 black; s32 value; } TreeNode;
void tree_balance_after_insert(TreeNode *root, TreeNode *x) __asm__("_ZNSt3__127__tree_balance_after_insertIPNS_16__tree_node_baseIPvEEEEvT_S5_");
void tree_remove(TreeNode *root, TreeNode *z) __asm__("_ZNSt3__113__tree_removeIPNS_16__tree_node_baseIPvEEEEvT_S5_");
void *cxx_new(u64 n) __asm__("_Znwm");
void cxx_delete(void *p) __asm__("_ZdlPv");
static int c_seed_effect(lua_State *L) {
    u8 *s = (u8 *)to_ptr(L, 1);
    s32 v = (s32)luaL_checkinteger(L, 2);
    bool add = lua_toboolean(L, 3);
    if (!s) return lua_pushboolean(L, 0), 1;
    TreeNode **begin = (TreeNode **)(s + 0x58), *end = (TreeNode *)(s + 0x60);
    if (!*begin) return lua_pushboolean(L, 0), 1;  // not a constructed set (an empty one begins at its end node)
    u64 *size = (u64 *)(s + 0x68);
    TreeNode *parent = end, **child = &end->left, *n = end->left;
    while (n) {
        if (v < n->value) parent = n, child = &n->left, n = n->left;
        else if (n->value < v) parent = n, child = &n->right, n = n->right;
        else break;
    }
    if (add) {
        if (n) return lua_pushboolean(L, 0), 1;
        n = (TreeNode *)cxx_new(sizeof(TreeNode));
        n->left = n->right = NULL, n->parent = parent, n->value = v;
        *child = n;
        if ((*begin)->left) *begin = (*begin)->left;
        tree_balance_after_insert(end->left, n);
        ++*size;
    } else {
        if (!n) return lua_pushboolean(L, 0), 1;
        if (*begin == n) {  // the next node in order
            TreeNode *x = n;
            if (x->right) for (x = x->right; x->left; x = x->left) {}
            else { while (x->parent->left != x) x = x->parent; x = x->parent; }
            *begin = x;
        }
        --*size;
        tree_remove(end->left, n);
        cxx_delete(n);
    }
    lua_pushboolean(L, 1);
    return 1;
}

void native_open(lua_State *L) {  // adds the functions to the table on the stack top
    luaL_newmetatable(L, BUF_MT);
    lua_pop(L, 1);
    static const luaL_Reg f[] = {
        {"sym", c_sym},       {"call", c_call},       {"peek", c_peek}, {"mem_ok", c_mem_ok},       {"poke", c_poke},
        {"offset", c_offset}, {"buf", c_buf},         {"ptr", c_ptr},         {"entity", c_entity},
        {"gameptr", c_game}, {"managerptr", c_manager}, {"roomptr", c_room}, {"grid_at", c_grid_at}, {"grid_desc", c_grid_desc}, {"missing", c_missing}, {"ms", c_ms}, {"make_type", c_make_type}, {"ent_dispatch", api_ent_dispatch}, {"has_query_radius", c_has_query_radius}, {"tick", c_tick}, {"spawn", c_spawn}, {"perf", c_perf}, {"perf_line", c_perf_line},       {"entity_ext", c_entity_ext}, {"resolve", c_resolve}, {"language", c_language}, {"ui_scale", c_ui_scale}, {"pc_screen", c_pc_screen}, {"ui_pc", c_ui_pc}, {"ui_unscale", c_ui_unscale}, {"save_ready", c_save_ready}, {"touch", c_touch}, {"save_read", c_save_read}, {"save_write", c_save_write}, {"menu_scale", c_menu_scale},
        {"box", c_box}, {"unbox", c_unbox}, {"seed_effect", c_seed_effect}, {"restart", c_restart}, {"level_init", c_level_init},
        {"autotest", c_autotest}, {"autolog", c_autolog}, {"errors", c_errors}, {"memfree", c_memfree}, {"meminfo", c_meminfo}, {"memtotal", c_memtotal},
        {NULL, NULL},
    };
    luaL_setfuncs(L, f, 0);
#ifdef ISAACLUA_DEBUG
    lua_pushcfunction(L, c_screenshot);
    lua_setfield(L, -2, "screenshot");
    lua_pushcfunction(L, c_remote_press);
    lua_setfield(L, -2, "remote_press");
    lua_pushcfunction(L, c_autotest_shots);
    lua_setfield(L, -2, "autotest_shots");
    lua_pushcfunction(L, c_autotest_batch);
    lua_setfield(L, -2, "autotest_batch");
    lua_pushcfunction(L, c_autotest_next);
    lua_setfield(L, -2, "autotest_next");
#endif
}
