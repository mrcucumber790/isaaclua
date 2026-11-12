/* Sprite (the PC API's wrapper of the engine's ANM2), Isaac.GetItemConfig and Isaac.WorldToScreen.
 *
 * A Sprite is either the mod's own ANM2, held inside the userdata, or the sprite of an entity
 * (entity + 0x50), checked against the room's entity list on every use like Entity handles.
 * Mods create sprites while their main.lua runs, which on Switch is during boot, before the game's
 * file system and renderer are up. So an own sprite only records Load / ReplaceSpritesheet /
 * LoadGraphics / SetFrame until the first game update, and is built from that record on first use. */
#include <string.h>

#include "isaaclua.h"
#include "lauxlib.h"
#include "lua.h"

#define SPRITE_MT "isaaclua.Sprite"
#define MAX_SHEETS 16
#define PATH_LEN 160

typedef struct {
    u8 buf[0x180 + 16];          // own ANM2 (kAnmSize used), 16-aligned inside: userdata is only 8-aligned
    u8 *ent;                     // entity sprite: the entity, NULL for an own sprite
    s32 ent_type;
    bool live;                   // own: raw holds a constructed ANM2
    bool want_graphics;          // recorded, not live yet
    uint16_t sheet_mask;
    char path[PATH_LEN];
    char sheets[MAX_SHEETS][PATH_LEN];
    char anim[48];               // recorded SetFrame / Play / SetAnimation
    int frame;
    int pending;                 // 0 none, 1 SetFrame, 2 Play, 3 SetAnimation
    float color[7];              // R G B A RO GO BO, applied to own sprites before rendering
    Vector2 scale;
    float rotation;              // Sprite.Rotation, FlipX/FlipY: copied into the ANM2 before rendering
    bool flip_x, flip_y;
    Vector2 offset;              // Sprite.Offset, added to the render position
    float speed, speed_acc;      // Sprite.PlaybackSpeed: Update() steps the animation speed times on average
} SpriteUD;

bool g_engine_ready = false;
#define kGridSprite (-2)  /* ent_type of a GridEntity's sprite */
#define kGrid_Sprite 72   /* ANM2 in GridEntity (GridEntity::GridEntity, GridEntity_Lock::Render) */

static u8 *raw_of(SpriteUD *s) { return (u8 *)(((uintptr_t)s->buf + 15) & ~(uintptr_t)15); }

void std_string_init(StdString *s, const char *p) {
    size_t n = strlen(p);
    memset(s, 0, sizeof *s);
    if (n <= 22) {
        u8 *b = (u8 *)s;
        b[0] = (u8)(n << 1);
        memcpy(b + 1, p, n);
    } else {  // long form pointing at the caller's buffer; the callee only reads (copies) it
        s->w[0] = ((n + 16) & ~(u64)15) | 1;
        s->w[1] = n;
        s->w[2] = (u64)(uintptr_t)p;
    }
}

static bool have_anm(void) {
    return G.anm_ctor && G.anm_reset && G.anm_load && G.anm_replace_sheet && G.anm_load_graphics &&
           G.anm_set_frame && G.anm_render;
}

static void apply_pending(SpriteUD *s, u8 *a) {
    if (s->pending == 1) G.anm_set_frame(a, s->anim, s->frame);
    else if (s->pending == 2 && G.anm_play) G.anm_play(a, s->anim, true);
    else if (s->pending == 3 && G.anm_set_animation) G.anm_set_animation(a, s->anim, true);
    s->pending = 0;
}

/* The engine ANM2, or NULL (entity gone, or an own sprite before the game runs). */
static u8 *anm(SpriteUD *s) {
    if (s->ent && s->ent_type == kGridSprite) return s->ent + kGrid_Sprite;  // the room owns it while the mod looks
    if (s->ent) return api_entity_alive(s->ent, s->ent_type) ? s->ent + kEnt_Sprite : NULL;
    if (s->live) return raw_of(s);
    if (!g_engine_ready || !have_anm()) return NULL;
    memset(raw_of(s), 0, 0x180);
    G.anm_ctor(raw_of(s));
    s->live = true;
    StdString str;
    if (s->path[0]) {
        std_string_init(&str, s->path);
        G.anm_load(raw_of(s), &str, false);
    }
    for (int i = 0; i < MAX_SHEETS; i++)
        if (s->sheet_mask & (1u << i)) {
            std_string_init(&str, s->sheets[i]);
            G.anm_replace_sheet(raw_of(s), i, &str);
        }
    if (s->want_graphics) G.anm_load_graphics(raw_of(s));
    apply_pending(s, raw_of(s));
    return raw_of(s);
}

static SpriteUD *check_sprite(lua_State *L, int idx) { return (SpriteUD *)luaL_checkudata(L, idx, SPRITE_MT); }

static SpriteUD *new_sprite(lua_State *L) {
    SpriteUD *s = (SpriteUD *)lua_newuserdata(L, sizeof(SpriteUD));
    memset(s, 0, sizeof *s);
    s->color[0] = s->color[1] = s->color[2] = s->color[3] = 1.0f;
    s->scale = (Vector2){1.0f, 1.0f};
    s->speed = 1.0f;
    luaL_setmetatable(L, SPRITE_MT);
    return s;
}

void sprite_push_entity(lua_State *L, u8 *e) {
    SpriteUD *s = new_sprite(L);
    s->ent = e;
    s->ent_type = ENT_I32(e, kEnt_Type);
}

static int l_grid_sprite(lua_State *L) {  // registry isaaclua.grid_sprite(GridEntity*): GridEntity:GetSprite
    u8 *g = (u8 *)lua_touserdata(L, 1);
    if (!g) return 0;
    SpriteUD *s = new_sprite(L);
    s->ent = g;
    s->ent_type = kGridSprite;
    return 1;
}

static int l_sprite_new(lua_State *L) {
    new_sprite(L);
    return 1;
}

static void copy_str(char *d, const char *s, size_t cap) {
    strncpy(d, s, cap - 1);
    d[cap - 1] = 0;
}

static Vector2 opt_vector(lua_State *L, int idx) {
    Vector2 v = {0, 0};
    if (lua_istable(L, idx)) {
        lua_getfield(L, idx, "X");
        lua_getfield(L, idx, "Y");
        v.x = (float)luaL_optnumber(L, -2, 0), v.y = (float)luaL_optnumber(L, -1, 0);
        lua_pop(L, 2);
    }
    return v;
}

/* ------------------------------------------------------------ methods */

static int s_reset(lua_State *L) {  // Reset(): back to an empty sprite (ANM2::Reset), as on PC
    SpriteUD *s = check_sprite(L, 1);
    if (s->ent || s->live) {
        u8 *a = anm(s);
        if (a && G.anm_reset) G.anm_reset(a);
    }
    if (!s->ent) {
        s->path[0] = 0, s->sheet_mask = 0, s->want_graphics = false, s->pending = 0;
        s->rotation = 0, s->flip_x = s->flip_y = false, s->offset = (Vector2){0, 0};
        s->scale = (Vector2){1.0f, 1.0f}, s->speed = 1.0f;
        s->color[0] = s->color[1] = s->color[2] = s->color[3] = 1.0f, s->color[4] = s->color[5] = s->color[6] = 0;
    }
    return 0;
}

static int s_overlay_priority(lua_State *L) {  // SetOverlayRenderPriority(renderFirst): ANM2+0x90 (Reset clears it)
    u8 *a = anm(check_sprite(L, 1));
    if (a) a[kAnm_OverlayFirst] = lua_toboolean(L, 2);
    return 0;
}

static int s_load(lua_State *L) {  // Load(path, loadGraphics)
    SpriteUD *s = check_sprite(L, 1);
    const char *path = luaL_checkstring(L, 2);
    bool gfx = lua_toboolean(L, 3);
    if (s->ent || s->live) {
        u8 *a = anm(s);
        if (!a) return 0;
        StdString str;
        std_string_init(&str, path);
        // no ANM2::Reset first: ANM2::Load keeps Rotation/Offset/Scale/FlipX as on PC (a side door's
        // sprite is rotated, Andromeda reloads door sprites, HW 0.72)
        G.anm_load(a, &str, gfx);
        return 0;
    }
    copy_str(s->path, path, PATH_LEN);
    s->sheet_mask = 0, s->want_graphics = gfx, s->pending = 0;
    return 0;
}

static int s_replace_sheet(lua_State *L) {  // ReplaceSpritesheet(layer, path)
    SpriteUD *s = check_sprite(L, 1);
    int layer = (int)luaL_checkinteger(L, 2);
    const char *path = luaL_checkstring(L, 3);
    if (s->ent || s->live) {
        u8 *a = anm(s);
        if (!a) return 0;
        StdString str;
        std_string_init(&str, path);
        G.anm_replace_sheet(a, layer, &str);
        return 0;
    }
    if (layer < 0 || layer >= MAX_SHEETS) return 0;  // as on PC: a layer the sprite does not have is ignored
    copy_str(s->sheets[layer], path, PATH_LEN);
    s->sheet_mask |= (uint16_t)(1u << layer);
    return 0;
}

static int s_load_graphics(lua_State *L) {
    SpriteUD *s = check_sprite(L, 1);
    if (s->ent || s->live) {
        u8 *a = anm(s);
        if (a) G.anm_load_graphics(a);
    } else {
        s->want_graphics = true;
    }
    return 0;
}

/* SetFrame(anim, frame) or SetFrame(frame); Play(anim, force); SetAnimation(anim, reset) */
static int set_anim(lua_State *L, int kind) {
    SpriteUD *s = check_sprite(L, 1);
    if (kind == 1 && lua_isnumber(L, 2) && lua_isnone(L, 3)) {
        int f = (int)lua_tointeger(L, 2);
        u8 *a = anm(s);
        if (a && G.anm_set_frame_i) G.anm_set_frame_i(a, f);
        else if (!a) s->frame = f;
        return 0;
    }
    const char *name = luaL_checkstring(L, 2);
    u8 *a = (s->ent || s->live || g_engine_ready) ? anm(s) : NULL;
    if (!a) {
        if (!s->ent) {
            copy_str(s->anim, name, sizeof s->anim);
            s->frame = kind == 1 ? (int)luaL_optinteger(L, 3, 0) : 0;
            s->pending = kind;
        }
        if (kind == 3) lua_pushboolean(L, 1);
        return kind == 3;
    }
    if (kind == 1) G.anm_set_frame(a, name, (int)luaL_optinteger(L, 3, 0));
    else if (kind == 2 && G.anm_play) G.anm_play(a, name, lua_isnone(L, 3) ? false : lua_toboolean(L, 3));
    else if (kind == 3 && G.anm_set_animation) {
        lua_pushboolean(L, G.anm_set_animation(a, name, lua_isnone(L, 3) ? true : lua_toboolean(L, 3)));
        return 1;
    }
    return 0;
}
static int s_set_frame(lua_State *L) { return set_anim(L, 1); }
static int s_play(lua_State *L) { return set_anim(L, 2); }
static int s_set_animation(lua_State *L) { return set_anim(L, 3); }

static int s_update(lua_State *L) {
    SpriteUD *s = check_sprite(L, 1);
    u8 *a = anm(s);
    if (!a || !G.anm_update) return 0;
    if (s->speed == 1.0f) {
        G.anm_update(a);
        return 0;
    }
    s->speed_acc += s->speed;
    for (int i = 0; s->speed_acc >= 1.0f && i < 8; i++, s->speed_acc -= 1.0f) G.anm_update(a);
    return 0;
}

/* overlay and layer frames: ANM2 methods looked up by name (an older build without them does nothing) */
static int s_set_layer_frame(lua_State *L) {  // SetLayerFrame(layer, frame)
    u8 *a = anm(check_sprite(L, 1));
    void (*f)(void *, int, int) = (void (*)(void *, int, int))api_sym("_ZN15IsaacRepentance4ANM213SetLayerFrameEii");
    if (a && f) f(a, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3));
    return 0;
}
/* _NullPos(id): the null's offset on the current frame (ANM2::GetNullFrame: position first), or nothing
 * when the frame has no such null; loader-internal (REP+ pause screen nulls Seed, CompletionWidget) */
static int s_null_pos(lua_State *L) {
    u8 *a = anm(check_sprite(L, 1));
    const float *(*f)(const void *, int) = (const float *(*)(const void *, int))api_sym(
        "_ZNK15IsaacRepentance4ANM212GetNullFrameEi");
    const float *n = a && f ? f(a, (int)luaL_checkinteger(L, 2)) : NULL;
    if (!n) return 0;
    lua_pushnumber(L, n[0]);
    lua_pushnumber(L, n[1]);
    return 2;
}
static int s_set_last_frame(lua_State *L) {  // SetLastFrame(): the last frame of the current animation
    u8 *a = anm(check_sprite(L, 1));
    void (*f)(void *) = (void (*)(void *))api_sym("_ZN15IsaacRepentance4ANM212SetLastFrameEv");
    if (a && f) f(a);
    return 0;
}
static int s_remove_overlay(lua_State *L) {
    u8 *a = anm(check_sprite(L, 1));
    void (*f)(void *) = (void (*)(void *))api_sym("_ZN15IsaacRepentance4ANM213RemoveOverlayEv");
    if (a && f) f(a);
    return 0;
}
static int s_set_overlay_frame(lua_State *L) {  // SetOverlayFrame(anim, frame)
    u8 *a = anm(check_sprite(L, 1));
    void (*f)(void *, const char *, int) =
        (void (*)(void *, const char *, int))api_sym("_ZN15IsaacRepentance4ANM215SetOverlayFrameEPKci");
    if (a && f) f(a, luaL_checkstring(L, 2), (int)luaL_checkinteger(L, 3));
    return 0;
}
static int s_play_overlay(lua_State *L) {  // PlayOverlay(anim, force)
    u8 *a = anm(check_sprite(L, 1));
    void (*f)(void *, const char *, bool) =
        (void (*)(void *, const char *, bool))api_sym("_ZN15IsaacRepentance4ANM211PlayOverlayEPKcb");
    if (a && f) f(a, luaL_checkstring(L, 2), lua_isnoneornil(L, 3) || lua_toboolean(L, 3));
    return 0;
}
static int s_is_overlay_playing(lua_State *L) {  // IsOverlayPlaying(anim)
    u8 *a = anm(check_sprite(L, 1));
    bool (*f)(const void *, const char *) =
        (bool (*)(const void *, const char *))api_sym("_ZNK15IsaacRepentance4ANM216IsOverlayPlayingEPKc");
    lua_pushboolean(L, a && f && lua_isstring(L, 2) && f(a, lua_tostring(L, 2)));
    return 1;
}
/* coverage batch 1: the ANM2 calls left out so far */
static int s_get_overlay_frame(lua_State *L) {  // GetOverlayFrame() -> frame of the overlay animation
    u8 *a = anm(check_sprite(L, 1));
    int (*f)(void *) = (int (*)(void *))api_sym("_ZN15IsaacRepentance4ANM215GetOverlayFrameEv");
    lua_pushinteger(L, a && f ? f(a) : 0);
    return 1;
}
static int s_anm_str_bool(lua_State *L, const char *mangled) {  // bool ANM2::X(const char *) const
    u8 *a = anm(check_sprite(L, 1));
    bool (*f)(const void *, const char *) = (bool (*)(const void *, const char *))api_sym(mangled);
    lua_pushboolean(L, a && f && f(a, luaL_optstring(L, 2, "")));
    return 1;
}
static int s_is_overlay_finished(lua_State *L) {
    return s_anm_str_bool(L, "_ZNK15IsaacRepentance4ANM217IsOverlayFinishedEPKc");
}
static int s_was_event_triggered(lua_State *L) {
    return s_anm_str_bool(L, "_ZNK15IsaacRepentance4ANM217WasEventTriggeredEPKc");
}
static int s_set_overlay_animation(lua_State *L) {  // SetOverlayAnimation(anim, force) -> bool
    u8 *a = anm(check_sprite(L, 1));
    bool (*f)(void *, const char *, bool) =
        (bool (*)(void *, const char *, bool))api_sym("_ZN15IsaacRepentance4ANM219SetOverlayAnimationEPKcb");
    lua_pushboolean(L, a && f && f(a, luaL_checkstring(L, 2), lua_isnoneornil(L, 3) || lua_toboolean(L, 3)));
    return 1;
}
static int s_play_random(lua_State *L) {  // PlayRandom(seed)
    u8 *a = anm(check_sprite(L, 1));
    void (*f)(void *, u32) = (void (*)(void *, u32))api_sym("_ZN15IsaacRepentance4ANM210PlayRandomEj");
    if (a && f) f(a, (u32)luaL_optinteger(L, 2, 1));
    return 0;
}
static int s_anm_void(lua_State *L, const char *mangled) {
    u8 *a = anm(check_sprite(L, 1));
    void (*f)(void *) = (void (*)(void *))api_sym(mangled);
    if (a && f) f(a);
    return 0;
}
static int s_reload(lua_State *L) { return s_anm_void(L, "_ZN15IsaacRepentance4ANM26ReloadEv"); }
static int s_get_overlay_animation(lua_State *L) {  // not found in the Switch build
    (void)check_sprite(L, 1);
    lua_pushliteral(L, "");
    return 1;
}
static int s_stop(lua_State *L) {
    u8 *a = anm(check_sprite(L, 1));
    if (a && G.anm_stop) G.anm_stop(a);
    return 0;
}

static void apply_look(SpriteUD *s, u8 *a) {
    if (s->ent) return;  // an entity sprite keeps the engine's own colour and scale
    float *m = (float *)(a + kAnm_ColorMod);
    m[0] = s->color[0], m[1] = s->color[1], m[2] = s->color[2], m[3] = s->color[3];
    m[4] = m[5] = m[6] = m[7] = 0.0f;
    m[8] = s->color[4], m[9] = s->color[5], m[10] = s->color[6];
    *(Vector2 *)(a + kAnm_Scale) = s->scale;
    *(float *)(a + kAnm_Rotation) = s->rotation;
    a[kAnm_FlipX] = s->flip_x, a[kAnm_FlipX + 1] = s->flip_y;
}

/* g_ui_scale: a mod's screen drawing shrunk around the screen centre (a menu laid out for PC's 480x270
 * on the Switch's smaller screen); set by the prelude while that mod's callback runs */
float g_ui_scale = 1.0f;
void ui_scale_point(float *x, float *y) {
    float cx = (G.width ? *G.width : 480.0f) / 2, cy = (G.height ? *G.height : 270.0f) / 2;
    *x = cx + (*x - cx) * g_ui_scale;
    *y = cy + (*y - cy) * g_ui_scale;
}
static void ui_scale_sprite(SpriteUD *s, u8 *a, Vector2 *pos) {
    if (g_ui_scale == 1.0f || s->ent) return;
    ui_scale_point(&pos->x, &pos->y);
    Vector2 *sc = (Vector2 *)(a + kAnm_Scale);
    sc->x *= g_ui_scale, sc->y *= g_ui_scale;
}

static int s_render(lua_State *L) {  // Render(pos, topLeftClamp, bottomRightClamp)
    SpriteUD *s = check_sprite(L, 1);
    u8 *a = anm(s);
    if (!a) return 0;
    Vector2 pos = opt_vector(L, 2), tl = opt_vector(L, 3), br = opt_vector(L, 4);
    pos.x += s->offset.x, pos.y += s->offset.y;
    apply_look(s, a);
    ui_scale_sprite(s, a, &pos);
    G.anm_render(a, &pos, &tl, &br);
    return 0;
}

static int s_render_layer(lua_State *L) {  // RenderLayer(layer, pos, topLeftClamp, bottomRightClamp)
    SpriteUD *s = check_sprite(L, 1);
    int layer = (int)luaL_checkinteger(L, 2);
    u8 *a = anm(s);
    if (!a || !G.anm_render_layer) return 0;
    Vector2 pos = opt_vector(L, 3), tl = opt_vector(L, 4), br = opt_vector(L, 5);
    pos.x += s->offset.x, pos.y += s->offset.y;
    apply_look(s, a);
    ui_scale_sprite(s, a, &pos);
    G.anm_render_layer(a, layer, &pos, &tl, &br);
    return 0;
}

static const char *cur_anim(SpriteUD *s, u8 *a) {
    if (!a) return s->pending ? s->anim : "";
    const u8 *data = *(const u8 *const *)(a + kAnm_CurData);
    return data ? std_str(data) : "";
}

static int s_get_animation(lua_State *L) {
    SpriteUD *s = check_sprite(L, 1);
    lua_pushstring(L, cur_anim(s, (s->ent || s->live || g_engine_ready) ? anm(s) : NULL));
    return 1;
}

static int s_get_frame(lua_State *L) {
    SpriteUD *s = check_sprite(L, 1);
    u8 *a = (s->ent || s->live) ? anm(s) : NULL;
    lua_pushinteger(L, a && G.anm_get_frame ? G.anm_get_frame(a) : s->frame);
    return 1;
}

static int anim_query(lua_State *L, bool (*fn)(const void *, const char *)) {
    SpriteUD *s = check_sprite(L, 1);
    u8 *a = (s->ent || s->live) ? anm(s) : NULL;
    lua_pushboolean(L, a && fn && fn(a, luaL_optstring(L, 2, "")));
    return 1;
}
static int s_is_finished(lua_State *L) { return anim_query(L, G.anm_is_finished); }
static int s_is_playing(lua_State *L) { return anim_query(L, G.anm_is_playing); }
static int s_is_event_triggered(lua_State *L) {
    luaL_checkstring(L, 2);
    return anim_query(L, G.anm_is_event_triggered);
}

/* the .anm2 path: ours for a Sprite() we loaded, else the engine's (ANM2 +0 std::string, set by ANM2::Load):
 * an entity's sprite has one from the moment it is spawned (Da Rules removes spawns whose name is "") */
static int s_get_filename(lua_State *L) {
    SpriteUD *s = check_sprite(L, 1);
    const u8 *a = (!s->path[0] && s->ent) ? anm(s) : NULL;
    if (a) {
        if (a[0] & 1) lua_pushlstring(L, *(const char *const *)(a + 16), *(const size_t *)(a + 8));
        else lua_pushlstring(L, (const char *)a + 1, a[0] >> 1);
        return 1;
    }
    lua_pushstring(L, s->path);
    return 1;
}


/* GetTexel(samplePos, renderPos, alphaThreshold, layer) -> KColor of the rendered pixel */
/* KAGE::Graphics::Color has a copy constructor, so the game returns it through x8 (not in s0-s3).
 * A struct over 16 bytes makes the compiler pass x8 too; the game writes the first 16. */
typedef struct { float r, g, b, a, pad; } Texel;
static int s_get_texel(lua_State *L) {
    SpriteUD *s = check_sprite(L, 1);
    static Texel (*fn)(const void *, Vector2, Vector2, float, int);
    static bool looked;
    if (!looked) looked = true, fn = (Texel (*)(const void *, Vector2, Vector2, float, int))api_sym(
                                        "_ZNK15IsaacRepentance4ANM28GetTexelEN4KAGE4Math7Vector2ES3_fi");
    Vector2 sample = opt_vector(L, 2), at = opt_vector(L, 3);
    float threshold = (float)luaL_optnumber(L, 4, 0);
    int layer = (int)luaL_optinteger(L, 5, -1);
    u8 *a = (s->ent || s->live || g_engine_ready) ? anm(s) : NULL;
    Texel t = {0, 0, 0, 0, 0};
    if (a && fn) {
        apply_look(s, a);
        t = fn(a, sample, at, threshold, layer);
    }
    lua_getglobal(L, "KColor");
    lua_pushnumber(L, t.r), lua_pushnumber(L, t.g), lua_pushnumber(L, t.b), lua_pushnumber(L, t.a);
    lua_call(L, 4, 1);
    return 1;
}

#define kAnm_DefaultAnim 0x18  /* std::string */
#define kAnm_LayerCount 0xb0   /* u32 */
static u8 *anm_now(SpriteUD *s) { return (s->ent || s->live || g_engine_ready) ? anm(s) : NULL; }
/* _isaaclua_sprite_anm(sprite) -> the engine ANM2 (lightuserdata) or nil: for game functions taking an ANM2 const& */
static int l_sprite_anm(lua_State *L) {
    SpriteUD *s = check_sprite(L, 1);
    u8 *a = anm_now(s);
    if (!a) return 0;
    lua_pushlightuserdata(L, a);
    return 1;
}
static int s_get_layer_count(lua_State *L) {
    u8 *a = anm_now(check_sprite(L, 1));
    lua_pushinteger(L, a ? *(u32 *)(a + kAnm_LayerCount) : 0);
    return 1;
}
/* PC: false when the anm2 could not be loaded (HW 0.99.123: a missing gfx/ui/pausescreen_small.anm2 read as loaded,
 * so the REP+ pause menu took that empty sprite and drew nothing without a mod that ships one). Before the engine
 * can build sprites (mods' load time) the file is only remembered, so a set path counts then. */
static int s_is_loaded(lua_State *L) {
    SpriteUD *s = check_sprite(L, 1);
    if (!s->ent && !s->path[0]) return lua_pushboolean(L, 0), 1;
    u8 *a = anm_now(s);
    lua_pushboolean(L, a ? *(u32 *)(a + kAnm_LayerCount) > 0 : 1);
    return 1;
}
static int s_get_default_animation(lua_State *L) {
    u8 *a = anm_now(check_sprite(L, 1));
    lua_pushstring(L, a ? std_str(a + kAnm_DefaultAnim) : "");
    return 1;
}

static const luaL_Reg kSpriteMethods[] = {
    {"GetTexel", s_get_texel},
    {"GetLayerCount", s_get_layer_count},
    {"GetDefaultAnimation", s_get_default_animation},
    {"GetDefaultAnimationName", s_get_default_animation},
    {"Load", s_load},
    {"ReplaceSpritesheet", s_replace_sheet},
    {"LoadGraphics", s_load_graphics},
    {"SetFrame", s_set_frame},
    {"Play", s_play},
    {"SetAnimation", s_set_animation},
    {"Update", s_update},
    {"Stop", s_stop},
    {"Render", s_render},
    {"RenderLayer", s_render_layer},
    {"GetAnimation", s_get_animation},
    {"GetFrame", s_get_frame},
    {"IsFinished", s_is_finished},
    {"IsPlaying", s_is_playing},
    {"IsEventTriggered", s_is_event_triggered},
    {"GetFilename", s_get_filename},
    {"IsLoaded", s_is_loaded},
    {"SetLayerFrame", s_set_layer_frame},
    {"SetLastFrame", s_set_last_frame},
    {"_NullPos", s_null_pos},
    {"RemoveOverlay", s_remove_overlay},
    {"SetOverlayFrame", s_set_overlay_frame},
    {"PlayOverlay", s_play_overlay},
    {"IsOverlayPlaying", s_is_overlay_playing},
    {"GetOverlayFrame", s_get_overlay_frame},
    {"IsOverlayFinished", s_is_overlay_finished},
    {"WasEventTriggered", s_was_event_triggered},
    {"SetOverlayAnimation", s_set_overlay_animation},
    {"PlayRandom", s_play_random},
    {"Reload", s_reload},
    {"Reset", s_reset},
    {"SetOverlayRenderPriority", s_overlay_priority},
    {"GetOverlayAnimation", s_get_overlay_animation},
    {NULL, NULL},
};

/* ------------------------------------------------------------ fields */

static void push_color(lua_State *L, const float *c) {  // Color(R, G, B, A, RO, GO, BO) from the prelude
    lua_getglobal(L, "Color");
    for (int i = 0; i < 7; i++) lua_pushnumber(L, c[i]);
    lua_call(L, 7, 1);
}

static void read_color(lua_State *L, int idx, float *c) {
    static const char *const k[7] = {"R", "G", "B", "A", "RO", "GO", "BO"};
    luaL_checktype(L, idx, LUA_TTABLE);
    for (int i = 0; i < 7; i++) {
        lua_getfield(L, idx, k[i]);
        c[i] = (float)luaL_optnumber(L, -1, i < 4 ? 1.0 : 0.0);
        lua_pop(L, 1);
    }
}

static int sprite_index(lua_State *L) {
    SpriteUD *s = check_sprite(L, 1);
    const char *k = luaL_checkstring(L, 2);
    for (const luaL_Reg *m = kSpriteMethods; m->name; m++)
        if (strcmp(k, m->name) == 0) {
            lua_pushcfunction(L, m->func);
            return 1;
        }
    if (strcmp(k, "Color") == 0) {
        float c[7];
        u8 *a = s->ent ? anm(s) : NULL;
        if (a) {
            const float *m = (const float *)(a + kAnm_ColorMod);
            c[0] = m[0], c[1] = m[1], c[2] = m[2], c[3] = m[3], c[4] = m[8], c[5] = m[9], c[6] = m[10];
        } else {
            memcpy(c, s->color, sizeof c);
        }
        push_color(L, c);
    } else if (strcmp(k, "Scale") == 0) {
        u8 *a = s->ent ? anm(s) : NULL;
        Vector2 v = a ? *(Vector2 *)(a + kAnm_Scale) : s->scale;
        lua_getglobal(L, "Vector");
        lua_pushnumber(L, v.x);
        lua_pushnumber(L, v.y);
        lua_call(L, 2, 1);
    } else if (strcmp(k, "Rotation") == 0) {
        u8 *a = s->ent ? anm(s) : NULL;
        lua_pushnumber(L, a ? *(float *)(a + kAnm_Rotation) : s->rotation);
    } else if (strcmp(k, "Offset") == 0) {
        u8 *a = s->ent ? anm(s) : NULL;
        api_push_vector(L, a ? *(Vector2 *)(a + kAnm_Offset) : s->offset);
    } else if (strcmp(k, "PlaybackSpeed") == 0) {
        u8 *a = s->ent ? anm(s) : NULL;
        lua_pushnumber(L, a ? *(float *)(a + kAnm_Speed) : s->speed);
    } else if (strcmp(k, "FlipX") == 0 || strcmp(k, "FlipY") == 0) {
        bool y = k[4] == 'Y';
        u8 *a = s->ent ? anm(s) : NULL;
        lua_pushboolean(L, a ? a[kAnm_FlipX + y] != 0 : (y ? s->flip_y : s->flip_x));
    } else {
        char what[64];
        snprintf(what, sizeof what, "Sprite.%s", k);
        api_report_missing(L, what);
        lua_pushnil(L);
    }
    return 1;
}

static int sprite_newindex(lua_State *L) {
    SpriteUD *s = check_sprite(L, 1);
    const char *k = luaL_checkstring(L, 2);
    if (strcmp(k, "Color") == 0) {
        float c[7];
        read_color(L, 3, c);
        u8 *a = s->ent ? anm(s) : NULL;
        if (a) {
            float *m = (float *)(a + kAnm_ColorMod);
            m[0] = c[0], m[1] = c[1], m[2] = c[2], m[3] = c[3], m[8] = c[4], m[9] = c[5], m[10] = c[6];
        } else if (!s->ent) {
            memcpy(s->color, c, sizeof c);
        }
    } else if (strcmp(k, "Scale") == 0) {
        Vector2 v = opt_vector(L, 3);
        u8 *a = s->ent ? anm(s) : NULL;
        if (a) *(Vector2 *)(a + kAnm_Scale) = v;
        else if (!s->ent) s->scale = v;
    } else if (s->ent && strcmp(k, "Offset") == 0) {  // an entity sprite: the engine's own fields
        u8 *a = anm(s);
        if (a) *(Vector2 *)(a + kAnm_Offset) = opt_vector(L, 3);
    } else if (s->ent && strcmp(k, "PlaybackSpeed") == 0) {
        u8 *a = anm(s);
        if (a) *(float *)(a + kAnm_Speed) = (float)luaL_checknumber(L, 3);
    } else if (s->ent && strcmp(k, "Rotation") == 0) {
        u8 *a = anm(s);
        if (a) *(float *)(a + kAnm_Rotation) = (float)luaL_checknumber(L, 3);
    } else if (strcmp(k, "Offset") == 0) {
        s->offset = opt_vector(L, 3);
    } else if (strcmp(k, "PlaybackSpeed") == 0) {
        s->speed = (float)luaL_checknumber(L, 3);
    } else if (strcmp(k, "Rotation") == 0) {
        s->rotation = (float)luaL_checknumber(L, 3);
    } else if (strcmp(k, "FlipX") == 0 || strcmp(k, "FlipY") == 0) {
        bool y = k[4] == 'Y', v = lua_toboolean(L, 3);
        u8 *a = s->ent ? anm(s) : NULL;
        if (a) a[kAnm_FlipX + y] = v;
        else if (!s->ent) *(y ? &s->flip_y : &s->flip_x) = v;
    } else {
        char what[64];
        snprintf(what, sizeof what, "Sprite.%s (write)", k);
        api_report_missing(L, what);
    }
    return 0;
}

static int sprite_gc(lua_State *L) {
    SpriteUD *s = check_sprite(L, 1);
    if (!s->ent && s->live && G.anm_dtor) G.anm_dtor(raw_of(s));
    s->live = false;
    return 0;
}

/* ------------------------------------------------------------ Isaac.GetItemConfig / WorldToScreen */

static const void *item_config(void) {
    u8 *m = manager_ptr();
    return m ? m + kManager_ItemConfig : NULL;
}

/* ItemConfigItem as a plain table (the fields mods read) */
static int push_item(lua_State *L, const u8 *it) {
    if (!it) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 6);
    lua_pushinteger(L, *(const s32 *)(it + kItem_ID));
    lua_setfield(L, -2, "ID");
    lua_pushinteger(L, *(const s32 *)(it + kItem_Type));
    lua_setfield(L, -2, "Type");
    lua_pushstring(L, std_str(it + kItem_Name));
    lua_setfield(L, -2, "Name");
    lua_pushstring(L, std_str(it + kItem_Description));
    lua_setfield(L, -2, "Description");
    lua_pushstring(L, std_str(it + kItem_GfxFileName));
    lua_setfield(L, -2, "GfxFileName");
    return 1;
}

static int ic_get(lua_State *L, const void *(*fn)(const void *, int)) {
    int id = (int)luaL_checkinteger(L, lua_gettop(L) >= 2 ? 2 : 1);  // ic:GetCollectible(id) or ic.GetCollectible(id)
    const void *ic = item_config();
    return push_item(L, ic && fn ? (const u8 *)fn(ic, id) : NULL);
}
static int ic_get_collectible(lua_State *L) { return ic_get(L, G.get_collectible); }
static int ic_get_trinket(lua_State *L) { return ic_get(L, G.get_trinket); }

static int l_get_item_config(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "isaaclua.ItemConfig");
    return 1;
}

static int l_world_to_screen(lua_State *L) {
    Vector2 p = opt_vector(L, 1);
    u8 *r = room_ptr();
    if (r && G.world_to_screen) p = G.world_to_screen(r, p);
    lua_getglobal(L, "Vector");
    lua_pushnumber(L, p.x);
    lua_pushnumber(L, p.y);
    lua_call(L, 2, 1);
    return 1;
}

void sprite_open(lua_State *L) {
    luaL_newmetatable(L, SPRITE_MT);
    static const luaL_Reg mt[] = {
        {"__index", sprite_index}, {"__newindex", sprite_newindex}, {"__gc", sprite_gc}, {NULL, NULL}};
    luaL_setfuncs(L, mt, 0);
    lua_pop(L, 1);
    lua_pushcfunction(L, l_sprite_new);
    lua_setglobal(L, "Sprite");
    lua_pushcfunction(L, l_grid_sprite);
    lua_setfield(L, LUA_REGISTRYINDEX, "isaaclua.grid_sprite");
    lua_pushcfunction(L, l_sprite_anm);
    lua_setglobal(L, "_isaaclua_sprite_anm");

    static const luaL_Reg ic[] = {
        {"GetCollectible", ic_get_collectible}, {"GetTrinket", ic_get_trinket}, {NULL, NULL}};
    lua_newtable(L);
    luaL_setfuncs(L, ic, 0);
    lua_setfield(L, LUA_REGISTRYINDEX, "isaaclua.ItemConfig");

    lua_getglobal(L, "Isaac");
    lua_pushcfunction(L, l_get_item_config);
    lua_setfield(L, -2, "GetItemConfig");
    lua_pushcfunction(L, l_world_to_screen);
    lua_setfield(L, -2, "WorldToScreen");
    lua_pop(L, 1);
}
