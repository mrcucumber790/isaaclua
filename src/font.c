/* Font: the PC API's wrapper of KAGE::Graphics::Font (BMFont .fnt + page images).
 *
 * Mods create and Load() fonts while they load, at boot, before the game's file system and renderer
 * exist, so the engine font is built on first use after the first game update (like sprites).
 * Paths may point into the mod folder; modfs.c resolves them for the game. */
#include <stdlib.h>
#include <string.h>

#include "isaaclua.h"
#include "lauxlib.h"
#include "lua.h"

#define FONT_MT "isaaclua.Font"
#define kFontSize 0x20200       /* KAGE::Graphics::Font is 0x20050 bytes (glyph map of 65536 u16 at +0x50) */
#define kFont_Loaded 0x0        /* u8 (Font::IsLoaded) */
#define kFont_LineHeight 0x14   /* u16 (Font::GetLineHeight) */
#define kFont_Baseline 0x16     /* u16 (Font::GetBaselineHeight) */

typedef struct {
    u8 *raw;          /* malloc'd engine object (16-aligned inside), NULL until built */
    char path[512];
    bool tried;       /* the engine load ran (successfully or not) */
    int missing;      /* SetMissingCharacter, -1 = unset */
} FontUD;

static struct {
    bool init;
    void (*ctor)(void *);
    bool (*load)(void *, const char *, const char *);
    void (*unload)(void *);
    void (*set_missing)(void *, u32);
} F;

static void init_fns(void) {
    if (F.init) return;
    F.init = true;
    F.ctor = (void (*)(void *))api_sym("_ZN4KAGE8Graphics4FontC1Ev");
    F.load = (bool (*)(void *, const char *, const char *))api_sym("_ZN4KAGE8Graphics4Font4LoadEPKcS3_");
    F.unload = (void (*)(void *))api_sym("_ZN4KAGE8Graphics4Font6UnloadEv");
    F.set_missing = (void (*)(void *, u32))api_sym("_ZN4KAGE8Graphics4Font19SetMissingCharacterEt");
}

static u8 *obj(FontUD *f) { return f->raw ? (u8 *)(((uintptr_t)f->raw + 15) & ~(uintptr_t)15) : NULL; }

/* the engine font, built now if it can be; NULL if not (yet) usable */
static u8 *font_of(FontUD *f) {
    if (!f->tried && f->path[0] && g_engine_ready) {
        init_fns();
        f->tried = true;
        if (F.ctor && F.load) {
            if (!f->raw) f->raw = (u8 *)calloc(1, kFontSize + 16);
            if (f->raw) {
                F.ctor(obj(f));
                F.load(obj(f), f->path, "");
                if (f->missing >= 0 && F.set_missing) F.set_missing(obj(f), (u32)f->missing);
            }
        }
    }
    u8 *o = obj(f);
    return o && o[kFont_Loaded] ? o : NULL;
}

static FontUD *check_font(lua_State *L) { return (FontUD *)luaL_checkudata(L, 1, FONT_MT); }

static int l_font_new(lua_State *L) {
    FontUD *f = (FontUD *)lua_newuserdata(L, sizeof(FontUD));
    memset(f, 0, sizeof *f);
    f->missing = -1;
    luaL_setmetatable(L, FONT_MT);
    return 1;
}

static void unload(FontUD *f) {
    u8 *o = obj(f);
    if (o && o[kFont_Loaded] && F.unload) F.unload(o);
}

static int f_load(lua_State *L) {  // Load(path)
    FontUD *f = check_font(L);
    unload(f);
    snprintf(f->path, sizeof f->path, "%s", luaL_checkstring(L, 2));
    f->tried = false;
    lua_pushboolean(L, font_of(f) != NULL || !g_engine_ready);
    return 1;
}

static int f_unload(lua_State *L) {
    FontUD *f = check_font(L);
    unload(f);
    f->path[0] = 0;
    return 0;
}

/* Before the engine can load it, a font counts as loaded if its file is there: a mod file must exist,
 * a game path is trusted (the game's own index is not readable yet). */
static int f_is_loaded(lua_State *L) {
    FontUD *f = check_font(L);
    bool ok;
    if (g_engine_ready) ok = font_of(f) != NULL;
    else {
        char buf[1024];
        bool absolute = strchr(f->path, ':') != NULL;
        ok = f->path[0] && (!absolute || modfs_resolve(f->path, buf, sizeof buf) != NULL);
    }
    lua_pushboolean(L, ok);
    return 1;
}

static int f_set_missing(lua_State *L) {
    FontUD *f = check_font(L);
    f->missing = (int)luaL_checkinteger(L, 2) & 0xffff;
    u8 *o = font_of(f);
    init_fns();
    if (o && F.set_missing) F.set_missing(o, (u32)f->missing);
    return 0;
}

static int f_line_height(lua_State *L) {
    u8 *o = font_of(check_font(L));
    lua_pushinteger(L, o ? *(uint16_t *)(o + kFont_LineHeight) : 0);
    return 1;
}

static int f_baseline(lua_State *L) {
    u8 *o = font_of(check_font(L));
    lua_pushinteger(L, o ? *(uint16_t *)(o + kFont_Baseline) : 0);
    return 1;
}

static int f_width(lua_State *L) {  // GetStringWidth(UTF8)(str)
    u8 *o = font_of(check_font(L));
    const char *s = luaL_checkstring(L, 2);
    lua_pushinteger(L, o && G.string_width_utf8 ? G.string_width_utf8(o, s) : 0);
    return 1;
}

static int f_char_width(lua_State *L) {  // GetCharacterWidth(char)
    u8 *o = font_of(check_font(L));
    const char *s = luaL_checkstring(L, 2);
    char one[8] = {0};
    size_t n = 1;
    while (n < 4 && ((u8)s[n] & 0xc0) == 0x80) n++;  // first UTF-8 character
    memcpy(one, s, s[0] ? n : 0);
    lua_pushinteger(L, o && G.string_width_utf8 ? G.string_width_utf8(o, one) : 0);
    return 1;
}

/* KColor (Red, Green, Blue, Alpha) or Color (R, G, B, A) at idx; white if absent */
static void check_kcolor(lua_State *L, int idx, float c[4]) {
    c[0] = c[1] = c[2] = c[3] = 1.0f;
    if (!lua_istable(L, idx)) return;
    static const char *const kK[] = {"Red", "Green", "Blue", "Alpha"}, *const kC[] = {"R", "G", "B", "A"};
    for (int i = 0; i < 4; i++) {
        if (lua_getfield(L, idx, kK[i]) == LUA_TNIL) {
            lua_pop(L, 1);
            lua_getfield(L, idx, kC[i]);
        }
        if (lua_isnumber(L, -1)) c[i] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
}

/* DrawString(str, x, y, color, boxWidth, center) and the Scaled form with sx, sy after y */
static int draw(lua_State *L, bool scaled) {
    FontUD *f = check_font(L);
    const char *s = luaL_checkstring(L, 2);
    float x = (float)luaL_checknumber(L, 3), y = (float)luaL_checknumber(L, 4), sx = 1, sy = 1;
    int i = 5;
    if (scaled) sx = (float)luaL_checknumber(L, 5), sy = (float)luaL_checknumber(L, 6), i = 7;
    float c[4];
    check_kcolor(L, i, c);
    u32 box = (u32)luaL_optinteger(L, i + 1, 0);
    bool center = lua_toboolean(L, i + 2);
    u8 *o = font_of(f);
    if (!o || !g_in_render || !G.draw_utf8 || !G.color_ctor) return 0;
    if (g_ui_scale != 1.0f) {
        ui_scale_point(&x, &y);
        sx *= g_ui_scale, sy *= g_ui_scale, box = (u32)(box * g_ui_scale + 0.5f);
    }
    ColorStorage color;
    G.color_ctor(&color, c[0], c[1], c[2], c[3]);
    G.draw_utf8(o, s, x, y, sx, sy, &color, box, center);
    return 0;
}
static int f_draw(lua_State *L) { return draw(L, false); }
static int f_draw_scaled(lua_State *L) { return draw(L, true); }

static int f_gc(lua_State *L) {
    FontUD *f = check_font(L);
    unload(f);
    free(f->raw);
    f->raw = NULL;
    return 0;
}

void font_open(lua_State *L) {
    static const luaL_Reg m[] = {
        {"Load", f_load},
        {"Unload", f_unload},
        {"IsLoaded", f_is_loaded},
        {"SetMissingCharacter", f_set_missing},
        {"GetLineHeight", f_line_height},
        {"GetBaselineHeight", f_baseline},
        {"GetStringWidth", f_width},
        {"GetStringWidthUTF8", f_width},
        {"GetCharacterWidth", f_char_width},
        {"DrawString", f_draw},
        {"DrawStringUTF8", f_draw},
        {"DrawStringScaled", f_draw_scaled},
        {"DrawStringScaledUTF8", f_draw_scaled},
        {NULL, NULL},
    };
    luaL_newmetatable(L, FONT_MT);
    lua_newtable(L);
    luaL_setfuncs(L, m, 0);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, f_gc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);
    lua_pushcfunction(L, l_font_new);
    lua_setglobal(L, "Font");
}
