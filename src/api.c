/* Lua bindings: the native half of the PC modding API (Isaac.*, Input.*, Entity / EntityPlayer).
 * Entities are userdata holding the engine pointer; every access first checks that the pointer is
 * still in the current room's entity list, so a stale handle reads as nil instead of crashing. */
#include <string.h>

#include "isaaclua.h"
#include "lauxlib.h"
#include "lua.h"

#define ENTITY_MT "isaaclua.Entity"

typedef struct {
    u8 *p;
    s32 type;  // type at creation: a reused slot with another type is not the same entity
    /* last values seen while the entity existed: like on PC, a removed entity's handle still reads them */
    s32 variant, subtype;
    u32 seed;
    s32 frame;
    u8 blk[0x50];  // Entity +0x310..+0x360: Position .. CollisionDamage (Size, Mass, Velocity, DepthOffset ...)
    s32 grid_coll, ent_coll;
    float hp, max_hp;
} EntRef;

bool g_in_render = false;
bool g_in_entity_render = false;  // in a POST_*_RENDER entity callback
static int s_vector_ref = LUA_NOREF;  // the prelude's Vector constructor

/* ------------------------------------------------------------ helpers */

static const u8 *s_initing[16];  // entities a callback runs for (main.c run, run_init), innermost last
static int s_ninit;
void api_init_push(const u8 *e) {
    if (s_ninit < 16) s_initing[s_ninit] = e;
    s_ninit++;
}
void api_init_pop(void) {
    if (s_ninit > 0) s_ninit--;
}

/* where an entity sits in the room's entity list, or -1. Every field read of an entity checks it is still
 * there; a scan of the list each time made Fiend Folio rooms (100+ entities, thousands of reads per update)
 * quadratic (HW 0.99.43: 11 fps). A hash of the list's pointers is checked against the list itself: a hit
 * whose slot no longer holds the entity, or an entity found only by the scan, rebuilds it. */
#define ALIVE_SLOTS 2048
static const u8 *s_alive_key[ALIVE_SLOTS];
static u32 s_alive_at[ALIVE_SLOTS];
static u8 *const *s_alive_data;
static u32 s_alive_count;
static inline u32 alive_hash(const u8 *e) {
    u64 x = (u64)(uintptr_t)e;
    x ^= x >> 17;
    x *= 0x9E3779B97F4A7C15ull;
    return (u32)(x >> 40) & (ALIVE_SLOTS - 1);
}
static void alive_build(EntList L) {
    memset(s_alive_key, 0, sizeof s_alive_key);
    s_alive_data = NULL;
    if (L.count >= ALIVE_SLOTS / 2) return;
    for (u32 i = 0; i < L.count; i++) {
        u32 h = alive_hash(L.data[i]);
        while (s_alive_key[h]) h = (h + 1) & (ALIVE_SLOTS - 1);
        s_alive_key[h] = L.data[i], s_alive_at[h] = i;
    }
    s_alive_data = L.data, s_alive_count = L.count;
}
int api_room_index(const u8 *e) {
    EntList L = room_entities();
    if (!e || !L.count) return -1;
    if (L.data == s_alive_data && L.count == s_alive_count) {
        for (u32 h = alive_hash(e); s_alive_key[h]; h = (h + 1) & (ALIVE_SLOTS - 1))
            if (s_alive_key[h] == e) {
                u32 i = s_alive_at[h];
                if (i < L.count && L.data[i] == e) return (int)i;
                break;
            }
    }
    for (u32 i = 0; i < L.count; i++)
        if (L.data[i] == e) {
            alive_build(L);
            return (int)i;
        }
    if (L.data != s_alive_data || L.count != s_alive_count) alive_build(L);
    return -1;
}

bool api_entity_alive(const u8 *e, s32 type) {
    if (!e) return false;
    for (int i = 0; i < s_ninit && i < 16; i++)
        if (s_initing[i] == e) return ENT_I32(e, kEnt_Type) == type;
    /* players live in the PlayerManager: between a run's start (or a room change) and the room's entity list
     * taking them in, Isaac.GetPlayer already hands them out, as on PC */
    if (type == 1)
        for (int i = 0; i < 64; i++) {
            u8 *p = api_player(i);
            if (!p) break;
            if (p == e) return ENT_I32(e, kEnt_Type) == 1;
            if (*(u8 **)(p + kPlayer_SubPlayer) == e || *(u8 **)(p + kPlayer_BackupPlayer) == e)  // The Soul..., as on PC
                return ENT_I32(e, kEnt_Type) == 1;
        }
    int i = api_room_index(e);
    return i >= 0 && ENT_I32(e, kEnt_Type) == type;
}

/* luaL_testudata/checkudata look the metatable up by name in the registry on every call */
static const void *s_ent_mt;
static EntRef *test_ent(lua_State *L, int idx) {
    if (lua_type(L, idx) != LUA_TUSERDATA || !lua_getmetatable(L, idx)) return NULL;
    const void *mt = lua_topointer(L, -1);
    lua_pop(L, 1);
    return mt == s_ent_mt ? (EntRef *)lua_touserdata(L, idx) : NULL;
}
static EntRef *checku_ent(lua_State *L, int idx) {
    EntRef *r = test_ent(L, idx);
    return r ? r : (EntRef *)luaL_checkudata(L, idx, ENTITY_MT);
}

#define kSnapBase 0x310
/* fields a removed entity's handle keeps (PC reads the last values; HW 0.99.47 timemachine slot.Size) */
static const struct { const char *name; unsigned short off; char kind; } kSnapFields[] = {
    {"Position", 0x310, 'v'}, {"PositionOffset", 0x320, 'v'}, {"RenderZOffset", 0x328, 'i'},
    {"DepthOffset", 0x32c, 'f'}, {"SortingLayer", 0x330, 'i'}, {"Velocity", 0x334, 'v'}, {"Size", 0x344, 'f'},
    {"SizeMulti", 0x348, 'v'}, {"Mass", 0x350, 'f'}, {"CollisionDamage", 0x35c, 'f'},
};

static void ent_snapshot(EntRef *r) {
    r->variant = ENT_I32(r->p, kEnt_Variant);
    r->subtype = ENT_I32(r->p, kEnt_SubType);
    r->seed = (u32)ENT_I32(r->p, kEnt_InitSeed);
    memcpy(r->blk, r->p + kSnapBase, sizeof r->blk);
    r->grid_coll = ENT_I32(r->p, 0x1e0), r->ent_coll = ENT_I32(r->p, 0x1e4);
    r->frame = G.ent_frame_count ? G.ent_frame_count(r->p) : 0;
    r->hp = ENT_F32(r->p, kEnt_HitPoints), r->max_hp = ENT_F32(r->p, kEnt_MaxHitPoints);
}

/* EntityFactory::Create: types 1-9 have their own classes, 1000 is an effect, 9001 text; every other type,
 * mod types above 1000 included (Samael's scythe 6617), is made an Entity_NPC */
static bool npc_type(int t) { return t >= 10 && t != 1000 && t != 9001; }
static bool ent_alive(EntRef *r) {
    if (!r) return false;
    /* Entity_NPC::Morph to another type keeps the object (and its InitSeed): every handle follows it, as on PC */
    if (!api_entity_alive(r->p, r->type)) {
        if (!npc_type(r->type) || !r->p || api_room_index(r->p) < 0) return false;
        s32 t = ENT_I32(r->p, kEnt_Type);
        if (!npc_type(t) || (u32)ENT_I32(r->p, kEnt_InitSeed) != r->seed) return false;
        r->type = t;
    }
    ent_snapshot(r);
    return true;
}

void api_push_entity(lua_State *L, u8 *e) {
    if (!e) {
        lua_pushnil(L);
        return;
    }
    EntRef *r = (EntRef *)lua_newuserdata(L, sizeof(EntRef));
    r->p = e;
    r->type = ENT_I32(e, kEnt_Type);
    ent_snapshot(r);
    luaL_setmetatable(L, ENTITY_MT);
}

u8 *api_entity_ptr(lua_State *L, int idx) {
    EntRef *r = test_ent(L, idx);
    return ent_alive(r) ? r->p : NULL;
}

static EntRef *check_ent(lua_State *L, int idx) {
    EntRef *r = checku_ent(L, idx);
    if (!ent_alive(r)) luaL_error(L, "entity no longer exists");
    return r;
}

static u8 *check_player(lua_State *L, int idx) {
    EntRef *r = check_ent(L, idx);
    if (r->type != 1) luaL_error(L, "EntityPlayer expected");
    return r->p;
}

void api_push_vector(lua_State *L, Vector2 v) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, s_vector_ref);
    lua_pushnumber(L, v.x);
    lua_pushnumber(L, v.y);
    lua_call(L, 2, 1);
}

static Vector2 check_vector(lua_State *L, int idx) {
    luaL_checktype(L, idx, LUA_TTABLE);
    lua_getfield(L, idx, "X");
    lua_getfield(L, idx, "Y");
    Vector2 v = {(float)luaL_checknumber(L, -2), (float)luaL_checknumber(L, -1)};
    lua_pop(L, 2);
    return v;
}

#define NEED(fn) \
    do { \
        if (!G.fn) return luaL_error(L, "%s is not available in this game build", #fn); \
    } while (0)

/* ------------------------------------------------------------ Isaac.* */

static int l_debug_string(lua_State *L) {
    size_t n;
    const char *s = luaL_tolstring(L, 1, &n);
    overlay_add(s, OVERLAY_INFO);
    return 0;
}

static void render_text(lua_State *L, const char *s, float x, float y, float sx, float sy, int ci) {
    u8 *mgr = manager_ptr();
    if (!g_in_render || !mgr || !G.draw_utf8 || !G.color_ctor) return;  // only valid in POST_RENDER
    float c[4];
    for (int i = 0; i < 4; i++) c[i] = (float)luaL_optnumber(L, ci + i, 1.0);
    ColorStorage color;
    G.color_ctor(&color, c[0], c[1], c[2], c[3]);
    G.draw_utf8(mgr + kManager_HudFont, s, x, y, sx, sy, &color, 0, false);
}

static int l_render_text(lua_State *L) {  // Isaac.RenderText(str, x, y, r, g, b, a)
    const char *s = luaL_checkstring(L, 1);
    render_text(L, s, (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), 1.0f, 1.0f, 4);
    return 0;
}

static int l_render_scaled_text(lua_State *L) {  // Isaac.RenderScaledText(str, x, y, sx, sy, r, g, b, a)
    const char *s = luaL_checkstring(L, 1);
    render_text(L, s, (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5), 6);
    return 0;
}

static int l_get_text_width(lua_State *L) {
    u8 *mgr = manager_ptr();
    const char *s = luaL_checkstring(L, 1);
    lua_pushinteger(L, (mgr && G.string_width_utf8) ? G.string_width_utf8(mgr + kManager_HudFont, s) : 0);
    return 1;
}

static int l_frame_count(lua_State *L) {
    lua_pushinteger(L, g_render_frames);
    return 1;
}

static int l_game_frame_count(lua_State *L) {
    u8 *g = game_ptr();
    lua_pushinteger(L, g ? *(u32 *)(g + kGame_FrameCount) : 0);
    return 1;
}

static int l_room_frame_count(lua_State *L) {
    u8 *g = game_ptr(), *r = room_ptr();
    lua_pushinteger(L, (g && r) ? (s32)(*(u32 *)(g + kGame_FrameCount) - *(u32 *)(r + kRoom_EnterFrame)) : 0);
    return 1;
}

/* Isaac.GetPlayer(i) as on PC: the PlayerManager's list (std::vector<Entity_Player*> at +16), which has
 * every player and nothing else. Before a run (and in the host test) it is empty: players in the room. */
#define kGame_PlayerManager 0x25c40
u8 *api_player(int index) {
    u8 *g = game_ptr();
    if (g && index >= 0) {
        u8 **b = *(u8 ***)(g + kGame_PlayerManager + 16), **e = *(u8 ***)(g + kGame_PlayerManager + 24);
        if (b && e > b && e - b < 64) return index < e - b ? b[index] : NULL;
    }
    EntList L = room_entities();
    int n = 0;
    for (u32 i = 0; i < L.count; i++) {
        u8 *e = L.data[i];
        if (ENT_I32(e, kEnt_Type) == 1 && ENT_I32(e, kEnt_Variant) == 0 && n++ == index) return e;
    }
    return NULL;
}

static int l_get_player(lua_State *L) {
    u8 *p = api_player((int)luaL_optinteger(L, 1, 0));
    api_push_entity(L, p ? p : api_player(0));  // PC: an index past the last player gives player 0
    return 1;
}

static int l_num_players(lua_State *L) {
    int n = 0;
    while (api_player(n)) n++;
    lua_pushinteger(L, n);
    return 1;
}

/* Isaac.FindByType(type, variant = -1, subtype = -1) / Isaac.GetRoomEntities() */
static int find(lua_State *L, int type, int variant, int subtype) {
    EntList E = room_entities();
    lua_createtable(L, 0, 0);
    int n = 0;
    for (u32 i = 0; i < E.count; i++) {
        u8 *e = E.data[i];
        if (type >= 0 && ENT_I32(e, kEnt_Type) != type) continue;
        if (variant >= 0 && ENT_I32(e, kEnt_Variant) != variant) continue;
        if (subtype >= 0 && ENT_I32(e, kEnt_SubType) != subtype) continue;
        api_push_entity(L, e);
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}
static int l_find_by_type(lua_State *L) {
    return find(L, (int)luaL_checkinteger(L, 1), (int)luaL_optinteger(L, 2, -1), (int)luaL_optinteger(L, 3, -1));
}
static int l_room_entities(lua_State *L) { return find(L, -1, -1, -1); }
/* Isaac.FindInRadius(pos, radius, partitions = 0xffffffff) through the engine's query, as PC does: the
 * entity's size counts toward the radius, and the order is the engine's. nil when not available (host test). */
static int l_find_in_radius(lua_State *L) {
    u8 *r = room_ptr();
    Vector2 pos = check_vector(L, 1);
    float radius = (float)luaL_checknumber(L, 2);
    u32 mask = (u32)luaL_optinteger(L, 3, -1);
    lua_createtable(L, 0, 0);
    if (!r || !G.query_radius) return 1;
    struct QueryResult q = G.query_radius(r + kRoom_EntityList, &pos, radius, mask);
    for (u32 i = 0; q.data && i < q.count; i++) {
        api_push_entity(L, q.data[i]);
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    return 1;
}
static int l_count_entities(lua_State *L) {
    find(L, (int)luaL_checkinteger(L, 2), (int)luaL_optinteger(L, 3, -1), (int)luaL_optinteger(L, 4, -1));
    lua_pushinteger(L, (lua_Integer)lua_rawlen(L, -1));
    return 1;
}

static int l_screen_w(lua_State *L) {
    lua_pushnumber(L, G.width ? *G.width : 480.0f);
    return 1;
}
static int l_screen_h(lua_State *L) {
    lua_pushnumber(L, G.height ? *G.height : 270.0f);
    return 1;
}

static int l_is_greed(lua_State *L) {
    u8 *g = game_ptr();
    lua_pushboolean(L, g && G.is_greed && G.is_greed(g));
    return 1;
}

static int l_unsupported(lua_State *L) {
    return luaL_error(L, "%s is not supported on Switch yet", lua_tostring(L, lua_upvalueindex(1)));
}

static int l_ptr_hash(lua_State *L) {
    EntRef *r = test_ent(L, 1);
    if (!r && lua_istable(L, 1) && lua_getfield(L, 1, "_p") == LUA_TLIGHTUSERDATA) {  // grid entity proxy
        lua_pushinteger(L, (lua_Integer)(uintptr_t)lua_touserdata(L, -1));
        return 1;
    }
    lua_pushinteger(L, r ? (lua_Integer)(uintptr_t)r->p : (lua_Integer)(uintptr_t)lua_topointer(L, 1));
    return 1;
}

/* ------------------------------------------------------------ Input.* */

static int input(lua_State *L, bool triggered) {
    u8 *mgr = manager_ptr();
    bool (*fn)(void *, u32, u32, void *) = triggered ? G.is_action_triggered : G.is_action_pressed;
    u32 action = (u32)luaL_checkinteger(L, 1);
    /* PC passes a controller index (0 keyboard, 1+ gamepads; MCM tries 0-4). The Switch numbers its
     * controllers its own way, so an index no player has means "any controller" (-1), as the menus ask */
    int ctrl = (int)luaL_optinteger(L, 2, -1);
    bool known = false;
    for (int i = 0; ctrl != -1 && !known && api_player(i); i++)
        known = *(s32 *)(api_player(i) + kPlayer_ControllerIndex) == ctrl;
    if (!known) ctrl = -1;
    lua_pushboolean(L, mgr && fn && fn(mgr, action, (u32)ctrl, NULL));
    return 1;
}
static int l_action_pressed(lua_State *L) { return input(L, false); }
static int l_action_triggered(lua_State *L) { return input(L, true); }

/* Input.IsButtonPressed(key, controllerIndex): controller 0 is the keyboard on PC, here the on-screen
 * keyboard / pad bindings (keyboard.c); -1 counts as the keyboard too. Ids below 32 are gamepad buttons
 * (no key has such a code): the Switch pad answers them for any controller index. */
static int button(lua_State *L, bool triggered) {
    int key = (int)luaL_checkinteger(L, 1);
    lua_Integer ctrl = luaL_optinteger(L, 2, 0);
    if (key >= 0 && key < 32)
        lua_pushboolean(L, triggered ? kb_pad_triggered(key) : kb_pad_down(key));
    else
        lua_pushboolean(L, ctrl <= 0 && (triggered ? kb_key_triggered(key) : kb_key_down(key)));
    return 1;
}
static int l_button_pressed(lua_State *L) { return button(L, false); }
static int l_button_triggered(lua_State *L) { return button(L, true); }
static int l_false(lua_State *L) {
    lua_pushboolean(L, 0);
    return 1;
}
static int l_mouse_position(lua_State *L) {
    api_push_vector(L, (Vector2){0, 0});
    return 1;
}

/* ------------------------------------------------------------ Entity methods */

static int m_exists(lua_State *L) {
    lua_pushboolean(L, ent_alive(checku_ent(L, 1)));
    return 1;
}
static int m_is_dead(lua_State *L) {  // a vanished entity is dead (mods keep lists of entities across frames)
    EntRef *r = checku_ent(L, 1);
    lua_pushboolean(L, !ent_alive(r) || *(r->p + kEnt_Dead) != 0);
    return 1;
}
static int m_is_boss(lua_State *L) {
    EntRef *r = check_ent(L, 1);
    lua_pushboolean(L, npc_type(r->type) && *(r->p + kNpc_IsBoss) != 0);
    return 1;
}
/* Kill and Remove of an entity that is already gone do nothing, as on PC (the object stays in its pool
 * there): mods remove what they kept from an earlier room without checking Exists (Samael's hitboxes) */
static int m_kill(lua_State *L) {
    EntRef *r = checku_ent(L, 1);
    if (!ent_alive(r)) return 0;
    /* virtual, like PC's binding: Entity_NPC::Kill / Entity_Familiar::Kill / Entity_Player::Kill overrides run
     * (vtable slot 10, HW-checked on a Gaper), and through them the hooked base Entity::Kill, so
     * POST_ENTITY_KILL fires. Calling Entity::Kill directly skipped both (HW 0.99.139: e:Kill() gave no
     * POST_ENTITY_KILL). */
    void **vt = *(void ***)r->p;
    if (vt && vt[10]) { ((void (*)(void *))vt[10])(r->p); return 0; }
    NEED(ent_kill);
    G.ent_kill(r->p);
    return 0;
}
static int m_remove(lua_State *L) {
    EntRef *r = checku_ent(L, 1);
    if (!ent_alive(r)) return 0;
    NEED(ent_remove);
    G.ent_remove(r->p);
    return 0;
}

/* PC To<Class>() only checks the type, also on a removed entity (mods keep lists of them: Watch Out Laser read
 * ToNPC().State of a dead Gluttony); a vanished one reads its last basic fields, the rest as nil */
static int to_kind(lua_State *L, int lo, int hi) {
    EntRef *r = checku_ent(L, 1);
    if (r->type >= lo && r->type <= hi) lua_pushvalue(L, 1);
    else lua_pushnil(L);
    return 1;
}
static int m_to_player(lua_State *L) { return to_kind(L, 1, 1); }
static int m_to_tear(lua_State *L) { return to_kind(L, 2, 2); }
static int m_to_familiar(lua_State *L) { return to_kind(L, 3, 3); }
static int m_to_bomb(lua_State *L) { return to_kind(L, 4, 4); }
static int m_to_pickup(lua_State *L) { return to_kind(L, 5, 5); }
static int m_to_laser(lua_State *L) { return to_kind(L, 7, 7); }
static int m_to_knife(lua_State *L) { return to_kind(L, 8, 8); }
static int m_to_projectile(lua_State *L) { return to_kind(L, 9, 9); }
static int m_to_npc(lua_State *L) {
    EntRef *r = checku_ent(L, 1);
    if (npc_type(r->type)) lua_pushvalue(L, 1);
    else lua_pushnil(L);
    return 1;
}
static int m_to_effect(lua_State *L) { return to_kind(L, 1000, 1000); }

/* ------------------------------------------------------------ EntityPlayer methods */

static bool opt_bool(lua_State *L, int idx, bool def) { return lua_isnoneornil(L, idx) ? def : lua_toboolean(L, idx); }

static int m_player_type(lua_State *L) {
    lua_pushinteger(L, ENT_I32(check_player(L, 1), kPlayer_Type));
    return 1;
}

#define PLAYER_ADD(name, fn) \
    static int name(lua_State *L) { \
        u8 *p = check_player(L, 1); \
        NEED(fn); \
        G.fn(p, (int)luaL_checkinteger(L, 2)); \
        return 0; \
    }
PLAYER_ADD(m_add_coins, add_coins)
PLAYER_ADD(m_add_bombs, add_bombs)
PLAYER_ADD(m_add_keys, add_keys)
PLAYER_ADD(m_add_soul_hearts, add_soul_hearts)
PLAYER_ADD(m_add_black_hearts, add_black_hearts)
PLAYER_ADD(m_add_eternal_hearts, add_eternal_hearts)
PLAYER_ADD(m_add_bone_hearts, add_bone_hearts)
PLAYER_ADD(m_add_card, add_card)
PLAYER_ADD(m_add_pill, add_pill)

#define PLAYER_GET(name, off) \
    static int name(lua_State *L) { \
        lua_pushinteger(L, ENT_I32(check_player(L, 1), off)); \
        return 1; \
    }
PLAYER_GET(m_num_coins, kPlayer_Coins)
PLAYER_GET(m_num_bombs, kPlayer_Bombs)
PLAYER_GET(m_num_keys, kPlayer_Keys)
PLAYER_GET(m_max_hearts, kPlayer_MaxHearts)
PLAYER_GET(m_hearts, kPlayer_Hearts)
PLAYER_GET(m_eternal_hearts, kPlayer_EternalHearts)
PLAYER_GET(m_soul_hearts, kPlayer_SoulHearts)
PLAYER_GET(m_bone_hearts, kPlayer_BoneHearts)

#define PLAYER_VOID(name, fn) \
    static int name(lua_State *L) { \
        u8 *p = check_player(L, 1); \
        NEED(fn); \
        G.fn(p); \
        return 0; \
    }
PLAYER_VOID(m_animate_happy, animate_happy)
PLAYER_VOID(m_animate_sad, animate_sad)

/* RemoveCollectible(type, ignoreModifiers = false, activeSlot = 0, removeFromPlayerForm = true) */
static int m_remove_collectible(lua_State *L) {
    u8 *p = check_player(L, 1);
    NEED(remove_collectible);
    G.remove_collectible(p, (int)luaL_checkinteger(L, 2), lua_toboolean(L, 3), (int)luaL_optinteger(L, 4, 0),
                         opt_bool(L, 5, true));
    return 0;
}
static int m_change_player_type(lua_State *L) {
    u8 *p = check_player(L, 1);
    NEED(change_player_type);
    G.change_player_type(p, (int)luaL_checkinteger(L, 2), false);
    return 0;
}

static int m_add_hearts(lua_State *L) {
    u8 *p = check_player(L, 1);
    NEED(add_hearts);
    G.add_hearts(p, (int)luaL_checkinteger(L, 2), false);
    return 0;
}
static int m_add_max_hearts(lua_State *L) {
    u8 *p = check_player(L, 1);
    NEED(add_max_hearts);
    G.add_max_hearts(p, (int)luaL_checkinteger(L, 2), lua_toboolean(L, 3));
    return 0;
}


/* AddCollectible(type, charge = 0, addConsumables = true, activeSlot = 0, varData = 0) */
static int m_add_collectible(lua_State *L) {
    u8 *p = check_player(L, 1);
    NEED(add_collectible);
    G.add_collectible(p, (int)luaL_checkinteger(L, 2), (int)luaL_optinteger(L, 3, 0), opt_bool(L, 4, true),
                      (int)luaL_optinteger(L, 5, 0), (int)luaL_optinteger(L, 6, 0));
    return 0;
}
static int m_has_collectible(lua_State *L) {
    u8 *p = check_player(L, 1);
    NEED(has_collectible);
    int id = (int)luaL_checkinteger(L, 2);
    /* the game reads ItemConfig::GetCollectible(id) unchecked when the player's flag +0x284c is set (PC
     * 0x73af30 does the same): an id with no item (-1 from a failed GetItemIdByName) crashed HW 0.99.18 */
    u8 *mgr = manager_ptr();
    if (G.get_collectible && mgr && !G.get_collectible(mgr + kManager_ItemConfig, id)) return lua_pushboolean(L, 0), 1;
    lua_pushboolean(L, G.has_collectible(p, id, lua_toboolean(L, 3)));
    return 1;
}
static int m_collectible_num(lua_State *L) {
    u8 *p = check_player(L, 1);
    lua_Integer id = luaL_checkinteger(L, 2);
    const u32 *b = *(const u32 *const *)(p + kPlayer_CollectibleNum);
    const u32 *e = *(const u32 *const *)(p + kPlayer_CollectibleNum + 8);
    lua_pushinteger(L, (b && id >= 0 && id < (lua_Integer)(e - b)) ? b[id] : 0);
    return 1;
}
static int m_has_trinket(lua_State *L) {
    u8 *p = check_player(L, 1);
    NEED(has_trinket);
    lua_pushboolean(L, G.has_trinket(p, (int)luaL_checkinteger(L, 2), lua_toboolean(L, 3)));
    return 1;
}
static int m_add_trinket(lua_State *L) {
    u8 *p = check_player(L, 1);
    NEED(add_trinket);
    G.add_trinket(p, (int)luaL_checkinteger(L, 2), opt_bool(L, 3, true));
    return 0;
}
/* UseActiveItem(item, useFlags = 0, activeSlot = -1, customVarData = 0); a boolean 2nd argument
   (the old ShowAnim/KeepActiveItem signature) counts as no flags */
static int m_use_active_item(lua_State *L) {
    u8 *p = check_player(L, 1);
    NEED(use_active_item);
    u32 flags = lua_isinteger(L, 3) ? (u32)lua_tointeger(L, 3) : 0;
    int slot = -1, var = 0;
    if (lua_isinteger(L, 3)) {
        slot = (int)luaL_optinteger(L, 4, -1);
        var = (int)luaL_optinteger(L, 5, 0);
    }
    G.use_active_item(p, (int)luaL_checkinteger(L, 2), flags, slot, var);
    return 0;
}
static int m_get_sprite(lua_State *L) {
    EntRef *r = checku_ent(L, 1);
    if (!ent_alive(r)) {  // a vanished entity: an empty Sprite, as reading a stale one on PC does no harm either
        lua_getglobal(L, "Sprite");
        lua_call(L, 0, 1);
        return 1;
    }
    sprite_push_entity(L, r->p);
    return 1;
}

static int m_get_active_item(lua_State *L) {  // GetActiveItem(slot = ActiveSlot.SLOT_PRIMARY)
    u8 *p = check_player(L, 1);
    int slot = (int)luaL_optinteger(L, 2, 0);
    lua_pushinteger(L, slot >= 0 && slot < 4 ? ENT_I32(p, kPlayer_ActiveItems + 28 * slot) : 0);
    return 1;
}

static int m_add_cache_flags(lua_State *L) {
    u8 *p = check_player(L, 1);
    *(u32 *)(p + kPlayer_CacheFlags) |= (u32)luaL_checkinteger(L, 2);
    return 0;
}
static int m_evaluate_items(lua_State *L) {
    u8 *p = check_player(L, 1);
    NEED(evaluate_items);
    G.evaluate_items(p);
    return 0;
}

/* ------------------------------------------------------------ Entity fields */

typedef struct { const char *name; lua_CFunction fn; bool player_only; } Method;
static const Method kMethods[] = {
    {"Exists", m_exists, false},      {"IsDead", m_is_dead, false},       {"IsBoss", m_is_boss, false},
    {"Kill", m_kill, false},          {"Remove", m_remove, false},        {"ToPlayer", m_to_player, false},
    {"ToTear", m_to_tear, false},     {"ToFamiliar", m_to_familiar, false}, {"ToBomb", m_to_bomb, false},
    {"ToPickup", m_to_pickup, false}, {"ToLaser", m_to_laser, false},     {"ToKnife", m_to_knife, false},
    {"ToProjectile", m_to_projectile, false}, {"ToNPC", m_to_npc, false}, {"ToEffect", m_to_effect, false},
    {"GetSprite", m_get_sprite, false},       {"GetActiveItem", m_get_active_item, true},
    {"GetPlayerType", m_player_type, true},   {"AddCoins", m_add_coins, true},
    {"AddBombs", m_add_bombs, true},          {"AddKeys", m_add_keys, true},
    {"AddHearts", m_add_hearts, true},        {"AddMaxHearts", m_add_max_hearts, true},
    {"AddSoulHearts", m_add_soul_hearts, true}, {"AddBlackHearts", m_add_black_hearts, true},
    {"AddCollectible", m_add_collectible, true}, {"HasCollectible", m_has_collectible, true},
    {"GetCollectibleNum", m_collectible_num, true}, {"HasTrinket", m_has_trinket, true},
    {"AddTrinket", m_add_trinket, true},      {"UseActiveItem", m_use_active_item, true},
    {"AddCacheFlags", m_add_cache_flags, true}, {"EvaluateItems", m_evaluate_items, true},
    {"AddEternalHearts", m_add_eternal_hearts, true}, {"AddBoneHearts", m_add_bone_hearts, true},
    {"AddCard", m_add_card, true},            {"AddPill", m_add_pill, true},
    {"GetNumCoins", m_num_coins, true},       {"GetNumBombs", m_num_bombs, true},
    {"GetNumKeys", m_num_keys, true},         {"GetMaxHearts", m_max_hearts, true},
    {"GetHearts", m_hearts, true},            {"GetEternalHearts", m_eternal_hearts, true},
    {"GetSoulHearts", m_soul_hearts, true},   {"GetBoneHearts", m_bone_hearts, true},
    {"AnimateHappy", m_animate_happy, true},  {"AnimateSad", m_animate_sad, true},
    {"RemoveCollectible", m_remove_collectible, true}, {"ChangePlayerType", m_change_player_type, true},
    {NULL, NULL, false},
};

/* Unknown fields are reported once each, so a mod running into a missing part of the API says so. */
void api_report_missing(lua_State *L, const char *what) {
    lua_getfield(L, LUA_REGISTRYINDEX, "isaaclua.missing");
    if (lua_getfield(L, -1, what) == LUA_TNIL) {
        lua_pushboolean(L, 1);
        lua_setfield(L, -3, what);
        char buf[96];
        snprintf(buf, sizeof buf, "not on Switch yet: %s", what);
        overlay_add(buf, OVERLAY_WARN);
    }
    lua_pop(L, 2);
}

/* registry[tab][key at idx] -> pushed and true, or nothing pushed and false */
/* the Lua tables of api.lua (core.entity_ext), by light userdata key: no name lookup per access */
char api_ext_key[3];
enum { EXT_METH, EXT_GET, EXT_SET };
static bool ext_lookup(lua_State *L, int tab, int idx) {
    if (lua_rawgetp(L, LUA_REGISTRYINDEX, &api_ext_key[tab]) != LUA_TTABLE) {
        lua_pop(L, 1);
        return false;
    }
    lua_pushvalue(L, idx);
    if (lua_rawget(L, -2) == LUA_TNIL) {
        lua_pop(L, 2);
        return false;
    }
    lua_remove(L, -2);
    return true;
}

/* float stats of EntityPlayer, readable and writable (MC_EVALUATE_CACHE) */
static int player_stat(const char *k) {
    static const struct { const char *name; int off; } kStats[] = {
        {"Damage", kPlayer_Damage},       {"MaxFireDelay", kPlayer_MaxFireDelay}, {"ShotSpeed", kPlayer_ShotSpeed},
        {"TearRange", kPlayer_TearRange}, {"MoveSpeed", kPlayer_MoveSpeed},       {"Luck", kPlayer_Luck},
        {"TearHeight", kPlayer_TearHeight}, {"TearFallingSpeed", kPlayer_TearFallingSpeed},
        {"TearFallingAcceleration", kPlayer_TearFallingAccel},
    };
    for (size_t i = 0; i < sizeof kStats / sizeof *kStats; i++)
        if (strcmp(k, kStats[i].name) == 0) return kStats[i].off;
    return -1;
}

/* entity keys -> codes (api_open): one hash lookup instead of strcmp down the method list and the fields */
enum { K_TYPE = 1, K_VARIANT, K_SUBTYPE, K_HP, K_MAXHP, K_POS, K_SEED, K_FRAME, K_INDEX, K_STAT = 0x1000,
       K_METHOD = 0x100000 };
static int s_keys_ref = LUA_NOREF;
static int key_code(lua_State *L, int idx) {
    if (s_keys_ref == LUA_NOREF || lua_type(L, idx) != LUA_TSTRING) return 0;
    lua_rawgeti(L, LUA_REGISTRYINDEX, s_keys_ref);
    lua_pushvalue(L, idx);
    lua_rawget(L, -2);
    int c = (int)lua_tointeger(L, -1);
    lua_pop(L, 2);
    return c;
}
static void keys_build(lua_State *L) {
    static const char *const kFields[] = {NULL, "Type", "Variant", "SubType", "HitPoints", "MaxHitPoints",
                                          "Position", "InitSeed", "FrameCount", "Index"};
    static const char *const kStatNames[] = {"Damage", "MaxFireDelay", "ShotSpeed", "TearRange", "MoveSpeed",
                                             "Luck", "TearHeight", "TearFallingSpeed", "TearFallingAcceleration"};
    lua_newtable(L);
    for (int i = 1; i < (int)(sizeof kFields / sizeof *kFields); i++) {
        lua_pushinteger(L, i);
        lua_setfield(L, -2, kFields[i]);
    }
    for (size_t i = 0; i < sizeof kStatNames / sizeof *kStatNames; i++) {
        lua_pushinteger(L, K_STAT + player_stat(kStatNames[i]));
        lua_setfield(L, -2, kStatNames[i]);
    }
    for (int i = 0; kMethods[i].name; i++)  // the first of a name wins, as the scan did
        if (lua_getfield(L, -1, kMethods[i].name) == LUA_TNIL || lua_tointeger(L, -1) < K_METHOD) {
            lua_pop(L, 1);
            lua_pushinteger(L, K_METHOD + i);
            lua_setfield(L, -2, kMethods[i].name);
        } else
            lua_pop(L, 1);
    s_keys_ref = luaL_ref(L, LUA_REGISTRYINDEX);
}

static int ent_index(lua_State *L) {
    EntRef *r = checku_ent(L, 1);
    const char *k = luaL_checkstring(L, 2);
    int code = key_code(L, 2);
    if (code >= K_METHOD) {
        const Method *m = &kMethods[code - K_METHOD];
        if (!(m->player_only && r->type != 1)) {
            lua_pushcfunction(L, m->fn);
            return 1;
        }
    }
    if (ext_lookup(L, EXT_METH, 2)) return 1;  // methods written in Lua (src/api.lua)
    if (!ent_alive(r)) {  // a vanished entity keeps its last basic fields, the rest reads as nil
        if (strcmp(k, "Type") == 0) lua_pushinteger(L, r->type);
        else if (strcmp(k, "Variant") == 0) lua_pushinteger(L, r->variant);
        else if (strcmp(k, "SubType") == 0) lua_pushinteger(L, r->subtype);
        else if (strcmp(k, "InitSeed") == 0) lua_pushinteger(L, r->seed);
        else if (strcmp(k, "GridCollisionClass") == 0) lua_pushinteger(L, r->grid_coll);
        else if (strcmp(k, "EntityCollisionClass") == 0) lua_pushinteger(L, r->ent_coll);
        else if (strcmp(k, "FrameCount") == 0) lua_pushinteger(L, r->frame);
        else if (strcmp(k, "HitPoints") == 0) lua_pushnumber(L, r->hp);
        else if (strcmp(k, "MaxHitPoints") == 0) lua_pushnumber(L, r->max_hp);
        else {
            lua_pushnil(L);
            for (size_t i = 0; i < sizeof kSnapFields / sizeof *kSnapFields; i++) {
                if (strcmp(k, kSnapFields[i].name) != 0) continue;
                const u8 *v = r->blk + kSnapFields[i].off - kSnapBase;
                lua_pop(L, 1);
                if (kSnapFields[i].kind == 'v') api_push_vector(L, *(const Vector2 *)v);
                else if (kSnapFields[i].kind == 'f') lua_pushnumber(L, *(const float *)v);
                else lua_pushinteger(L, *(const s32 *)v);
                break;
            }
        }
        return 1;
    }
    u8 *e = r->p;
    switch (code) {
    case K_TYPE: lua_pushinteger(L, ENT_I32(e, kEnt_Type)); return 1;
    case K_VARIANT: lua_pushinteger(L, ENT_I32(e, kEnt_Variant)); return 1;
    case K_SUBTYPE: lua_pushinteger(L, ENT_I32(e, kEnt_SubType)); return 1;
    case K_HP: lua_pushnumber(L, ENT_F32(e, kEnt_HitPoints)); return 1;
    case K_MAXHP: lua_pushnumber(L, ENT_F32(e, kEnt_MaxHitPoints)); return 1;
    case K_POS: api_push_vector(L, *(Vector2 *)(e + kEnt_Position)); return 1;
    case K_SEED: lua_pushinteger(L, (u32)ENT_I32(e, kEnt_InitSeed)); return 1;
    case K_FRAME:
        if (G.ent_frame_count) {
            lua_pushinteger(L, G.ent_frame_count(e));
            return 1;
        }
        break;
    case K_INDEX: {
        int i = api_room_index(e);
        lua_pushinteger(L, i >= 0 ? i : (lua_Integer)room_entities().count);
        return 1;
    }
    default:
        if (code >= K_STAT && code < K_METHOD && r->type == 1) {
            lua_pushnumber(L, ENT_F32(e, code - K_STAT));
            return 1;
        }
    }
    if (ext_lookup(L, EXT_GET, 2)) {  // fields written in Lua: getter(entity)
        lua_pushvalue(L, 1);
        lua_call(L, 1, 1);
    } else {
        char what[80];
        snprintf(what, sizeof what, "%s.%s", r->type == 1 ? "EntityPlayer" : "Entity", k);
        api_report_missing(L, what);
        lua_pushnil(L);
    }
    return 1;
}

/* core.ent_dispatch(cls_of, resolve, Entity class) -> the entities' __index once a mod replaced a class's
 * __index (api.lua scan): the entity's class (cls_of[type], else resolve(type)) indexes through its __index,
 * else Entity's, else ent_index. Was a Lua function on every entity read (HW 0.99.43: 31% of Lua time). */
static int ent_dispatch(lua_State *L) {
    EntRef *r = checku_ent(L, 1);
    lua_settop(L, 2);
    lua_Integer t = ent_alive(r) ? ENT_I32(r->p, kEnt_Type) : r->type;
    if (lua_rawgeti(L, lua_upvalueindex(1), t) == LUA_TNIL) {
        lua_pop(L, 1);
        lua_pushvalue(L, lua_upvalueindex(2));
        lua_pushinteger(L, t);
        lua_call(L, 1, 1);
        lua_pushvalue(L, -1);
        lua_rawseti(L, lua_upvalueindex(1), t);
    }
    lua_pushliteral(L, "__index");  /* 3: class, 4: its __index */
    lua_rawget(L, 3);
    if (lua_tocfunction(L, 4) == ent_index && !lua_rawequal(L, 3, lua_upvalueindex(3))) {
        lua_settop(L, 2);
        lua_pushvalue(L, lua_upvalueindex(3));
        lua_pushliteral(L, "__index");
        lua_rawget(L, 3);
    }
    if (lua_tocfunction(L, 4) == ent_index) {
        lua_settop(L, 2);
        return ent_index(L);
    }
    if (lua_istable(L, 4)) {
        lua_pushvalue(L, 2);
        lua_gettable(L, 4);
        return 1;
    }
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_call(L, 2, 1);
    return 1;
}
int api_ent_dispatch(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    luaL_checktype(L, 3, LUA_TTABLE);
    lua_settop(L, 3);
    lua_pushcclosure(L, ent_dispatch, 3);
    return 1;
}

static int ent_newindex(lua_State *L) {
    /* a field written on an entity the game already freed: PC raises no error there (the write lands in the
     * old object), so the mod's callback goes on; here the write is dropped (Fiend Folio shotfly.lua:179) */
    EntRef *r = checku_ent(L, 1);
    if (!ent_alive(r)) return 0;
    const char *k = luaL_checkstring(L, 2);
    int code = key_code(L, 2);
    u8 *e = r->p;
    if (code == K_HP) ENT_F32(e, kEnt_HitPoints) = (float)luaL_checknumber(L, 3);
    else if (code == K_MAXHP) ENT_F32(e, kEnt_MaxHitPoints) = (float)luaL_checknumber(L, 3);
    else if (code == K_POS) *(Vector2 *)(e + kEnt_Position) = check_vector(L, 3);
    /* writable on PC too (IsaacDocs: int Variant / int SubType; Type is const) */
    else if (code == K_VARIANT) ENT_I32(e, kEnt_Variant) = (s32)luaL_checkinteger(L, 3), r->variant = ENT_I32(e, kEnt_Variant);
    else if (code == K_SUBTYPE) ENT_I32(e, kEnt_SubType) = (s32)luaL_checkinteger(L, 3), r->subtype = ENT_I32(e, kEnt_SubType);
    else if (r->type == 1 && code >= K_STAT && code < K_METHOD) ENT_F32(e, code - K_STAT) = (float)luaL_checknumber(L, 3);
    else if (ext_lookup(L, EXT_SET, 2)) {  // setter(entity, value)
        lua_pushvalue(L, 1);
        lua_pushvalue(L, 3);
        lua_call(L, 2, 0);
    } else {
        char what[80];
        snprintf(what, sizeof what, "%s.%s (write)", r->type == 1 ? "EntityPlayer" : "Entity", k);
        api_report_missing(L, what);
    }
    return 0;
}

static int ent_eq(lua_State *L) {
    EntRef *a = test_ent(L, 1), *b = test_ent(L, 2);
    lua_pushboolean(L, a && b && a->p == b->p);
    return 1;
}

static int ent_tostring(lua_State *L) {
    EntRef *r = checku_ent(L, 1);
    if (ent_alive(r))
        lua_pushfstring(L, "Entity %d.%d.%d", ENT_I32(r->p, kEnt_Type), ENT_I32(r->p, kEnt_Variant),
                        ENT_I32(r->p, kEnt_SubType));
    else
        lua_pushliteral(L, "Entity (gone)");
    return 1;
}

/* ------------------------------------------------------------ registration */

static void set_funcs(lua_State *L, const luaL_Reg *f) { luaL_setfuncs(L, f, 0); }

void api_open(lua_State *L) {
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "isaaclua.missing");

    luaL_newmetatable(L, ENTITY_MT);
    s_ent_mt = lua_topointer(L, -1);
    keys_build(L);  /* (pushes nothing) */
    static const luaL_Reg mt[] = {{"__index", ent_index}, {"__newindex", ent_newindex}, {"__eq", ent_eq},
                                  {"__tostring", ent_tostring}, {NULL, NULL}};
    set_funcs(L, mt);
    lua_pop(L, 1);

    static const luaL_Reg isaac[] = {
        {"DebugString", l_debug_string},     {"ConsoleOutput", l_debug_string},
        {"RenderText", l_render_text},       {"RenderScaledText", l_render_scaled_text},
        {"GetTextWidth", l_get_text_width},  {"GetFrameCount", l_frame_count},
        {"GetPlayer", l_get_player},         {"FindByType", l_find_by_type},
        {"GetRoomEntities", l_room_entities}, {"FindInRadius_engine", l_find_in_radius}, {"CountEntities", l_count_entities},
        {"GetScreenWidth", l_screen_w},      {"GetScreenHeight", l_screen_h},
        {NULL, NULL},
    };
    lua_newtable(L);
    set_funcs(L, isaac);
    static const char *const kUnsupported[] = {"ExecuteCommand", "GetItemIdByName",
                                               "GetMusicManager", "GetSoundEffects", NULL};
    for (const char *const *u = kUnsupported; *u; u++) {
        char name[48];
        snprintf(name, sizeof name, "Isaac.%s", *u);
        lua_pushstring(L, name);
        lua_pushcclosure(L, l_unsupported, 1);
        lua_setfield(L, -2, *u);
    }
    lua_setglobal(L, "Isaac");

    static const luaL_Reg input[] = {{"IsActionPressed", l_action_pressed},
                                     {"IsActionTriggered", l_action_triggered},
                                     {"IsButtonPressed", l_button_pressed},
                                     {"IsButtonTriggered", l_button_triggered},
                                     {"IsMouseBtnPressed", l_false},
                                     {"GetMousePosition", l_mouse_position},
                                     {NULL, NULL}};
    lua_newtable(L);
    set_funcs(L, input);
    lua_setglobal(L, "Input");

    lua_pushcfunction(L, l_ptr_hash);
    lua_setglobal(L, "GetPtrHash");

    /* native helpers the prelude wraps into Game() / Room */
    static const luaL_Reg game[] = {{"GetFrameCount", l_game_frame_count}, {"GetNumPlayers", l_num_players},
                                    {"IsGreedMode", l_is_greed}, {"RoomFrameCount", l_room_frame_count},
                                    {NULL, NULL}};
    lua_newtable(L);
    set_funcs(L, game);
    lua_setfield(L, LUA_REGISTRYINDEX, "isaaclua.game");

    sprite_open(L);
    font_open(L);
}

void api_set_vector_ctor(lua_State *L) {  // expects Vector on the stack top
    s_vector_ref = luaL_ref(L, LUA_REGISTRYINDEX);
}
