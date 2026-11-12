/* On-screen keyboard: PC mods read keys (Input.IsButtonPressed(Keyboard.KEY_F1, 0)), the Switch has
 * none. A button combo (default + and -) opens a keyboard drawn over the HUD; the stick / d-pad picks
 * a key, A presses it (held while A is held), B closes. While it is open the game gets no input.
 * rom:/isaaclua.ini can bind pad buttons straight to keys (L3 = F1) and change the combo.
 *
 * Input comes from the game's own nn::hid::GetNpadState calls (hooked in main.c): we see each state
 * after the SDK filled it, and can clear buttons before the game reads them. A single press of a combo
 * button is held back for a few polls, so pressing + and - together never reaches the game as + (pause);
 * a quick tap of + alone is passed on late instead. */
#include <string.h>
#include <stdlib.h>

#include "isaaclua.h"
#include "nn.h"

/* nn::hid::NpadButton bits */
enum {
    B_A = 0, B_B = 1, B_X = 2, B_Y = 3, B_L3 = 4, B_R3 = 5, B_L = 6, B_R = 7, B_ZL = 8, B_ZR = 9,
    B_PLUS = 10, B_MINUS = 11, B_LEFT = 12, B_UP = 13, B_RIGHT = 14, B_DOWN = 15,
    B_SL_LEFT = 16, B_SL_UP = 17, B_SL_RIGHT = 18, B_SL_DOWN = 19,
};
#define BIT(b) (1ull << (b))

typedef struct {  // nn::hid::NpadFullKeyState and the other Npad*State: same layout
    s64 sampling;
    u64 buttons;
    s32 lx, ly, rx, ry;
    u32 attributes;
} NpadState;

#define NKEYS 512
static u8 s_down[NKEYS];        // key held (A held on it, or a bound button held)
static u8 s_latched[NKEYS];     // Shift / Ctrl / Alt toggled on the keyboard
static u8 s_trig_render[NKEYS]; // triggered, not yet seen by a render / an update
static u8 s_trig_update[NKEYS];
/* entity renders (POST_PLAYER_RENDER...) come after the next update, before the next HUD: they see the keys
 * of the last POST_RENDER once */
static u8 s_trig_entity[NKEYS];

static bool s_open = false;
/* Cursor mode (the "Mouse" key, or a button bound to MOUSE): the PC mouse for mods' menus. The left stick
 * moves a cursor, A is the left button, Y the right one, B turns it off; the game gets no input meanwhile. */
static bool s_mouse = false;
static float s_mx = 213.0f, s_my = 120.0f;
static int s_mbuttons;             // 1 left, 2 right
static s32 s_stick_x, s_stick_y;   // strongest left stick seen since the last frame
static s64 s_mouse_on_tick;
static bool s_mouse_armed;         // buttons released since the cursor came on
static u64 s_combo = BIT(B_PLUS) | BIT(B_MINUS);
static u64 s_raw;               // buttons seen since the last render frame (all pads)
static u64 s_prev;              // previous render frame's s_raw
static int s_sel_row = 1, s_sel_col = 1, s_pressed = -1;
static int s_repeat = 0;
/* GetNpadState calls (several per frame: every pad id and style). The keyboard only opens while the
 * HUD is being drawn (in a run), closes when the HUD stops (back to the menus), and one press of the
 * combo toggles it once even though several pads / styles report it. */
static s64 s_last_frame_tick = 0, s_last_toggle_tick = 0;

static s64 ms_since(s64 t) { return (nnosGetSystemTick() - t) * 1000 / nnosGetSystemTickFrequency(); }

typedef struct { u64 button; int key; } Binding;
static Binding s_bind[16];
static int s_nbind = 0;
static u64 s_bound_mask = 0;

/* per pad: combo state */
typedef struct {
    bool latched;  // combo seen, until all its buttons are released
    u8 hold[2];    // polls a combo button has been held alone
    u8 replay[2];  // polls left to pass a quick single tap on to the game
} PadState;
static PadState s_pad[10];

/* ------------------------------------------------------------ layout (GLFW key codes, as PC Isaac) */

typedef struct { const char *label; int key; u8 w; } Key;  // w: width in half units
enum { K_SHIFT = 340, K_CTRL = 341, K_ALT = 342, K_MOUSE = 470 };  // K_MOUSE: not a PC key, turns the cursor on
static const Key kRow0[] = {{"Esc", 256, 3}, {"F1", 290, 2}, {"F2", 291, 2}, {"F3", 292, 2}, {"F4", 293, 2},
                            {"F5", 294, 2},  {"F6", 295, 2}, {"F7", 296, 2}, {"F8", 297, 2}, {"F9", 298, 2},
                            {"F10", 299, 2}, {"F11", 300, 2}, {"F12", 301, 2}, {NULL, 0, 0}};
static const Key kRow1[] = {{"`", 96, 2},  {"1", 49, 2}, {"2", 50, 2}, {"3", 51, 2}, {"4", 52, 2},
                            {"5", 53, 2},  {"6", 54, 2}, {"7", 55, 2}, {"8", 56, 2}, {"9", 57, 2},
                            {"0", 48, 2},  {"-", 45, 2}, {"=", 61, 2}, {"Bksp", 259, 4}, {NULL, 0, 0}};
static const Key kRow2[] = {{"Tab", 258, 3}, {"Q", 81, 2}, {"W", 87, 2}, {"E", 69, 2}, {"R", 82, 2},
                            {"T", 84, 2},    {"Y", 89, 2}, {"U", 85, 2}, {"I", 73, 2}, {"O", 79, 2},
                            {"P", 80, 2},    {"[", 91, 2}, {"]", 93, 2}, {"\\", 92, 3}, {NULL, 0, 0}};
static const Key kRow3[] = {{"Caps", 280, 4}, {"A", 65, 2}, {"S", 83, 2}, {"D", 68, 2}, {"F", 70, 2},
                            {"G", 71, 2},     {"H", 72, 2}, {"J", 74, 2}, {"K", 75, 2}, {"L", 76, 2},
                            {";", 59, 2},     {"'", 39, 2}, {"Enter", 257, 4}, {NULL, 0, 0}};
static const Key kRow4[] = {{"Shift", K_SHIFT, 5}, {"Z", 90, 2}, {"X", 88, 2}, {"C", 67, 2}, {"V", 86, 2},
                            {"B", 66, 2},          {"N", 78, 2}, {"M", 77, 2}, {",", 44, 2}, {".", 46, 2},
                            {"/", 47, 2},          {"Del", 261, 3}, {"Home", 268, 3}, {NULL, 0, 0}};
static const Key kRow5[] = {{"Ctrl", K_CTRL, 4}, {"Alt", K_ALT, 4}, {"Space", 32, 7}, {"<", 263, 2},
                            {"^", 265, 2},       {"v", 264, 2},     {">", 262, 2},     {"End", 269, 3},
                            {"Mouse", K_MOUSE, 4}, {NULL, 0, 0}};
static const Key *const kRows[] = {kRow0, kRow1, kRow2, kRow3, kRow4, kRow5};
#define NROWS 6

static int row_len(int r) {
    int n = 0;
    while (kRows[r][n].label) n++;
    return n;
}
static bool is_modifier(int key) { return key == K_SHIFT || key == K_CTRL || key == K_ALT; }

/* ------------------------------------------------------------ key state */

static void key_press(int key) {
    if (key <= 0 || key >= NKEYS) return;
    if (!s_down[key]) s_trig_render[key] = s_trig_update[key] = 1;
    s_down[key] = 1;
}
static void key_release(int key) {
    if (key > 0 && key < NKEYS) s_down[key] = 0;
}

#ifdef ISAACLUA_HOST_TEST
void test_key_tap(int key) { key_press(key), key_release(key); }  // host test: a key pressed and let go
void test_key_hold(int key, bool down) { down ? key_press(key) : key_release(key); }
void test_mouse_on(void) { s_mouse = true, s_mouse_armed = false, s_mx = 213.0f, s_my = 120.0f; }
#endif
bool kb_key_down(int key) { return key > 0 && key < NKEYS && (s_down[key] || s_latched[key]); }
bool kb_key_triggered(int key) {
    if (key <= 0 || key >= NKEYS) return false;
    return g_in_render ? s_trig_render[key] : g_in_entity_render ? s_trig_entity[key] : s_trig_update[key];
}
/* PC gamepad buttons (Input.IsButtonPressed(Controller.X, n), as MCM names them) kept like keys, in
 * slots above the keyboard codes; meaning-for-meaning: BUTTON_A is the Switch A (confirm). */
#define PAD_BASE 480
static const int kPadBits[16] = {B_LEFT, B_RIGHT, B_UP, B_DOWN, B_A, B_B, B_X, B_Y,
                                 B_L, B_ZL, B_L3, B_R, B_ZR, B_R3, B_MINUS, B_PLUS};
bool kb_pad_down(int button) { return button >= 0 && button < 16 && s_down[PAD_BASE + button]; }
bool kb_pad_triggered(int button) { return button >= 0 && button < 16 && kb_key_triggered(PAD_BASE + button); }
/* L3+R3 restarts the run on Switch, and R3 alone opens Mod Config Menu: a stick press reaches mods a few
 * frames late, and not at all when the other stick joins it (the game still sees both). */
#define STICK_WAIT 5
static u8 s_pad_wait[16];
static bool s_sticks_combo;
static void pad_buttons(u64 now, u64 edge) {
    const u64 sticks = BIT(B_L3) | BIT(B_R3);
    if ((now & sticks) == sticks) s_sticks_combo = true;
    else if (!(now & sticks)) s_sticks_combo = false;
    for (int i = 0; i < 16; i++) {
        u64 bit = BIT(kPadBits[i]);
        bool stick = (bit & sticks) != 0;
        if (s_open || s_mouse || (stick && s_sticks_combo)) {
            s_pad_wait[i] = 0;
            if (s_open || s_mouse || !(now & bit)) key_release(PAD_BASE + i);
        } else if (edge & bit) {
            if (stick) s_pad_wait[i] = STICK_WAIT;
            else key_press(PAD_BASE + i);
        } else if (s_pad_wait[i]) {
            if (--s_pad_wait[i] == 0) key_press(PAD_BASE + i);  // a quick tap is released the frame after
        } else if (!(now & bit)) {
            key_release(PAD_BASE + i);
        }
    }
}

void kb_end_update(void) { memset(s_trig_update, 0, sizeof s_trig_update); }
bool kb_is_open(void) { return s_open; }

/* ------------------------------------------------------------ pad filter */

static int pad_slot(u32 id) { return id < 8 ? (int)id : id == 0x20 ? 8 : 9; }

static int combo_bit(int i) {  // i-th (0/1) button of the combo
    int n = 0;
    for (int b = 0; b < 32; b++)
        if (s_combo & BIT(b)) {
            if (n == i) return b;
            n++;
        }
    return -1;
}

void kb_filter_npad(void *state, u32 id) {
    NpadState *st = (NpadState *)state;
    PadState *p = &s_pad[pad_slot(id)];
    u64 b = st->buttons;
    s_raw |= b;

    bool in_run = s_last_frame_tick && ms_since(s_last_frame_tick) < 250;
    if (s_open && !in_run) s_open = false;
    if (s_mouse && !in_run) s_mouse = false;
    if (s_mouse && !s_open) {  // cursor: the stick is ours, the game sees an idle pad
        if (abs(st->lx) > abs(s_stick_x)) s_stick_x = st->lx;
        if (abs(st->ly) > abs(s_stick_y)) s_stick_y = st->ly;
    }
    bool all = (b & s_combo) == s_combo;
    if (all && !p->latched) {
        p->latched = true;
        if (in_run && ms_since(s_last_toggle_tick) > 300) {
            s_last_toggle_tick = nnosGetSystemTick();
            s_open = !s_open;
            if (!s_open && s_pressed >= 0) key_release(s_pressed), s_pressed = -1;  // let go of a held key
        }
    }
    if (p->latched && !(b & s_combo)) p->latched = false;

    if (s_open || p->latched) {  // a combo press is never passed on later as a single tap
        p->hold[0] = p->hold[1] = 0;
        p->replay[0] = p->replay[1] = 0;
    }
    if (s_open || s_mouse) {  // the game sees an idle pad
        st->buttons = 0;
        st->lx = st->ly = st->rx = st->ry = 0;
        return;
    }
    if (p->latched) {
        st->buttons &= ~s_combo;
        return;
    }
    /* a single combo button: hold it back a little, pass a quick tap on afterwards */
    enum { DELAY = 8, REPLAY = 4 };  // polls, not frames: a pad is read about once a frame
    for (int i = 0; i < 2; i++) {
        int cb = combo_bit(i);
        if (cb < 0) continue;
        if (b & BIT(cb)) {
            if (p->hold[i] < 255) p->hold[i]++;
            if (p->hold[i] < DELAY) st->buttons &= ~BIT(cb);
        } else {
            if (p->hold[i] > 0 && p->hold[i] < DELAY) p->replay[i] = REPLAY;
            p->hold[i] = 0;
            if (p->replay[i]) st->buttons |= BIT(cb), p->replay[i]--;
        }
    }
    st->buttons &= ~s_bound_mask;  // bound buttons are keys now
#ifdef ISAACLUA_DEBUG
    /* the autotest starts its runs itself: outside a run, A for 0.1 s every 1.5 s (title, save file, New Run,
     * the character) from 8 s after the start */
    extern int g_autotest;
    s64 f = nnosGetSystemTickFrequency(), ms = f ? nnosGetSystemTick() / (f / 1000) : 0;
    static s64 last_run;  // also the start: 5 minutes without a run (stuck in a menu) end the batch
    if (!last_run || in_run) last_run = ms;
    if (g_autotest && !in_run) {
        if (ms - last_run > 8000 && ms % 1500 < 100) st->buttons |= BIT(B_A);
        static bool gave_up;
        if (ms - last_run > 5 * 60000 && !gave_up) {
            extern void autotest_next(const char *result);
            gave_up = true;
            autotest_next("no run for 5 minutes (stuck in a menu?)");
        }
    }
    /* remote control (src/remote.c): buttons the relay holds down; outside a run its chunks run from here */
    extern u32 remote_buttons(void);
    extern void isaaclua_remote_tick(bool render);
    st->buttons |= remote_buttons();
    if (!in_run) isaaclua_remote_tick(false);
#endif
}

/* ------------------------------------------------------------ per render frame: navigation and drawing */

static void move_sel(int dr, int dc) {
    if (dr) {
        /* keep roughly the same x when changing rows */
        int x = 0;
        for (int i = 0; i < s_sel_col; i++) x += kRows[s_sel_row][i].w;
        x += kRows[s_sel_row][s_sel_col].w / 2;
        s_sel_row = (s_sel_row + dr + NROWS) % NROWS;
        int acc = 0, n = row_len(s_sel_row);
        s_sel_col = n - 1;
        for (int i = 0; i < n; i++) {
            acc += kRows[s_sel_row][i].w;
            if (acc > x) {
                s_sel_col = i;
                break;
            }
        }
    } else {
        int n = row_len(s_sel_row);
        s_sel_col = (s_sel_col + dc + n) % n;
    }
}

static void draw_text(const char *s, float x, float y, float sc, float r, float g, float b, float a) {
    u8 *mgr = manager_ptr();
    if (!mgr || !G.draw_utf8 || !G.color_ctor) return;
    ColorStorage c;
    G.color_ctor(&c, r, g, b, a);
    G.draw_utf8(mgr + kManager_HudFont, s, x, y, sc, sc, &c, 0, false);
}

static float text_w(const char *s, float sc) {
    u8 *mgr = manager_ptr();
    return mgr && G.string_width_utf8 ? (float)G.string_width_utf8(mgr + kManager_HudFont, s) * sc : 0.0f;
}

/* Paper backdrop: the blank sheet of the game's own popup (gfx/ui/genericpopup.anm2, layer
 * "Confirmation", 240x144 frame, pivot 120,64, placed at 234,123), stretched over the keys.
 * The paper inside the frame spans about x -112..113, y -56..69 around the pivot. */
static struct { u8 buf[0x180 + 16]; bool tried, ok; } s_paper;
static u8 *paper_anm(void) { return (u8 *)(((uintptr_t)s_paper.buf + 15) & ~(uintptr_t)15); }

static void draw_paper(float cx, float cy, float w, float h) {
    if (!s_paper.tried) {
        s_paper.tried = true;
        if (!G.anm_ctor || !G.anm_load || !G.anm_set_frame || !G.anm_render_layer) return;
        u8 *a = paper_anm();
        memset(a, 0, 0x180);
        G.anm_ctor(a);
        float *m = (float *)(a + kAnm_ColorMod);  // darkened: light letters read like the console's
        m[0] = m[1] = m[2] = 0.18f, m[3] = 0.9f;
        StdString str;
        std_string_init(&str, "gfx/ui/genericpopup.anm2");
        G.anm_load(a, &str, true);
        G.anm_set_frame(a, "Idle", 0);
        s_paper.ok = *(void **)(a + kAnm_CurData) != NULL;  // the animation exists: the file loaded
    }
    if (!s_paper.ok) return;
    u8 *a = paper_anm();
    Vector2 sc = {w / 225.0f, h / 125.0f};
    *(Vector2 *)(a + kAnm_Scale) = sc;
    Vector2 pos = {cx - sc.x * (234.0f + 0.5f), cy - sc.y * (123.0f + 6.5f)}, zero = {0, 0};
    G.anm_render_layer(a, 1, &pos, &zero, &zero);
}

/* Readable on the Switch's 1280x720 panel: the font at whole-pixel scale 1 on whole-pixel positions
 * (fractional scales blur it), near-black ink on opaque paper, the selection bold and red. */
static void draw_keyboard(void) {
    float W = G.width ? *G.width : 480.0f, H = G.height ? *G.height : 270.0f;
    const float unit = 13.0f, row_h = 16.0f, sc = 1.0f;  // unit: half a key
    float box_h = NROWS * row_h + 22.0f, top = __builtin_floorf(H - box_h - 2.0f);
    draw_paper(W / 2.0f, top + box_h / 2.0f, 418.0f, box_h);
    const char *title = "Keyboard   A: press   B: close";
    float tx0 = __builtin_floorf((W - text_w(title, sc)) / 2.0f);
    draw_text(title, tx0 + 1.0f, top + 3.0f, sc, 0, 0, 0, 1.0f);
    draw_text(title, tx0, top + 2.0f, sc, 0.7f, 0.7f, 0.7f, 1.0f);
    float y = top + 15.0f;
    for (int r = 0; r < NROWS; r++, y += row_h) {
        int n = row_len(r), units = 0;
        for (int i = 0; i < n; i++) units += kRows[r][i].w;
        float x = (W - units * unit) / 2.0f;
        for (int i = 0; i < n; i++) {
            const Key *k = &kRows[r][i];
            float kw = k->w * unit;
            bool sel = r == s_sel_row && i == s_sel_col, on = kb_key_down(k->key);
            float tx = __builtin_floorf(x + (kw - text_w(k->label, sc)) / 2.0f);
            /* as the console: light letters with a black shadow (dark letters merge with the font's outline) */
            float cr = 1.0f, cg = 1.0f, cb = 1.0f;
            if (sel) cr = 1.0f, cg = 0.85f, cb = 0.2f;
            else if (on) cr = 0.45f, cg = 1.0f, cb = 0.45f;
            if (sel) {
                float bx = __builtin_floorf(x + 1.0f), ex = __builtin_floorf(x + kw - 1.0f - text_w("]", sc));
                draw_text("[", bx + 1.0f, y + 1.0f, sc, 0, 0, 0, 1.0f);
                draw_text("]", ex + 1.0f, y + 1.0f, sc, 0, 0, 0, 1.0f);
                draw_text("[", bx, y, sc, cr, cg, cb, 1.0f);
                draw_text("]", ex, y, sc, cr, cg, cb, 1.0f);
            }
            draw_text(k->label, tx + 1.0f, y + 1.0f, sc, 0, 0, 0, 1.0f);
            draw_text(k->label, tx, y, sc, cr, cg, cb, 1.0f);
            x += kw;
        }
    }
}

void kb_frame(void) {
    u64 now = s_raw, edge = now & ~s_prev;
    s_prev = now;
    s_raw = 0;
    s_last_frame_tick = nnosGetSystemTick();
    pad_buttons(now, edge);

    /* bound buttons */
    for (int i = 0; i < s_nbind && !s_open; i++) {
        if (s_bind[i].key == K_MOUSE) {
            if (edge & s_bind[i].button) s_mouse = !s_mouse, s_mouse_armed = false, s_mouse_on_tick = nnosGetSystemTick();
            continue;
        }
        if (s_mouse) continue;
        if (edge & s_bind[i].button) key_press(s_bind[i].key);
        else if (!(now & s_bind[i].button) && s_down[s_bind[i].key] && s_bind[i].key != s_pressed)
            key_release(s_bind[i].key);
    }

    if (s_open) {
        u64 dirs = BIT(B_LEFT) | BIT(B_RIGHT) | BIT(B_UP) | BIT(B_DOWN) | BIT(B_SL_LEFT) | BIT(B_SL_RIGHT) |
                   BIT(B_SL_UP) | BIT(B_SL_DOWN);
        u64 d = now & dirs;
        bool step = (edge & dirs) != 0;
        if (d && !step) {  // auto-repeat while held
            if (++s_repeat > 18 && s_repeat % 5 == 0) step = true;
        } else {
            s_repeat = 0;
        }
        if (step) {
            if (d & (BIT(B_LEFT) | BIT(B_SL_LEFT))) move_sel(0, -1);
            else if (d & (BIT(B_RIGHT) | BIT(B_SL_RIGHT))) move_sel(0, 1);
            else if (d & (BIT(B_UP) | BIT(B_SL_UP))) move_sel(-1, 0);
            else if (d & (BIT(B_DOWN) | BIT(B_SL_DOWN))) move_sel(1, 0);
        }
        int key = kRows[s_sel_row][s_sel_col].key;
        if (edge & BIT(B_A)) {
            if (key == K_MOUSE) {
                s_open = false;
                s_mouse = true, s_mouse_armed = false;
                s_mouse_on_tick = nnosGetSystemTick();
                return;
            }
            if (is_modifier(key)) {
                s_latched[key] = !s_latched[key];
                if (s_latched[key]) s_trig_render[key] = s_trig_update[key] = 1;
            } else {
                key_press(key);
                s_pressed = key;
            }
        }
        if (s_pressed >= 0 && !(now & BIT(B_A))) key_release(s_pressed), s_pressed = -1;
        if (edge & BIT(B_B)) {
            s_open = false;
            if (s_pressed >= 0) key_release(s_pressed), s_pressed = -1;
        }
    }

    if (s_mouse && !s_open) {
        float W = G.width ? *G.width : 480.0f, H = G.height ? *G.height : 270.0f;
        float fx = s_stick_x / 32767.0f, fy = -s_stick_y / 32767.0f;  // stick up is +y
        float m = fx * fx + fy * fy;
        if (m > 0.04f) {  // dead zone; faster the further it is pushed
            float speed = 1.0f + 5.0f * m;
            s_mx += fx * speed, s_my += fy * speed;
        }
        if (edge & BIT(B_LEFT)) s_mx -= 1;
        if (edge & BIT(B_RIGHT)) s_mx += 1;
        if (edge & BIT(B_UP)) s_my -= 1;
        if (edge & BIT(B_DOWN)) s_my += 1;
        s_mx = s_mx < 0 ? 0 : s_mx > W - 1 ? W - 1 : s_mx;
        s_my = s_my < 0 ? 0 : s_my > H - 1 ? H - 1 : s_my;
        if (!(now & (BIT(B_A) | BIT(B_Y)))) s_mouse_armed = true;  // the A that turned it on is not a click
        s_mbuttons = !s_mouse_armed ? 0 : (now & BIT(B_A)) ? 1 : (now & BIT(B_Y)) ? 2 : 0;
        if (edge & BIT(B_B)) s_mouse = false, s_mbuttons = 0;
    }
    s_stick_x = s_stick_y = 0;
}

bool kb_mouse(float *x, float *y, int *buttons) {
    if (!s_mouse) return false;
    *x = s_mx, *y = s_my, *buttons = s_mbuttons;
    return true;
}

void kb_draw(void) {
    if (s_open) draw_keyboard();
    if (s_mouse && !s_open) {
        float x = __builtin_floorf(s_mx), y = __builtin_floorf(s_my), w = text_w("+", 1.0f);
        float g = s_mbuttons ? 0.85f : 1.0f, b = s_mbuttons ? 0.2f : 1.0f;  // yellow while a button is held
        draw_text("+", x - w / 2 + 1, y - 6, 1.0f, 0, 0, 0, 1.0f);
        draw_text("+", x - w / 2, y - 7, 1.0f, 1.0f, g, b, 1.0f);
        if (ms_since(s_mouse_on_tick) < 4000) {
            const char *hint = "Cursor: stick moves, A click, Y right click, B off";
            float H = G.height ? *G.height : 270.0f;
            draw_text(hint, 9, H - 13, 1.0f, 0, 0, 0, 1.0f);
            draw_text(hint, 8, H - 14, 1.0f, 0.85f, 0.85f, 0.85f, 1.0f);
        }
    }
}

void kb_end_render(void) {
    memcpy(s_trig_entity, s_trig_render, sizeof s_trig_entity);
    memset(s_trig_render, 0, sizeof s_trig_render);
}

/* ------------------------------------------------------------ rom:/isaaclua.ini */

static const struct { const char *name; int bit; } kButtons[] = {
    {"A", B_A},         {"B", B_B},       {"X", B_X},       {"Y", B_Y},         {"L3", B_L3},     {"R3", B_R3},
    {"L", B_L},         {"R", B_R},       {"ZL", B_ZL},     {"ZR", B_ZR},       {"PLUS", B_PLUS}, {"+", B_PLUS},
    {"MINUS", B_MINUS}, {"-", B_MINUS},   {"LEFT", B_LEFT}, {"UP", B_UP},       {"RIGHT", B_RIGHT},
    {"DOWN", B_DOWN},
};

static int button_bit(const char *s) {
    for (size_t i = 0; i < sizeof kButtons / sizeof *kButtons; i++)
        if (strcasecmp(s, kButtons[i].name) == 0) return kButtons[i].bit;
    return -1;
}

int kb_button_bit(const char *s) { return button_bit(s); }

static int key_code(const char *s) {
    if (strncasecmp(s, "KEY_", 4) == 0) s += 4;
    for (int r = 0; r < NROWS; r++)
        for (const Key *k = kRows[r]; k->label; k++)
            if (strcasecmp(s, k->label) == 0) return k->key;
    static const struct { const char *n; int k; } kAlias[] = {
        {"ESCAPE", 256}, {"BACKSPACE", 259}, {"SPACE", 32}, {"ENTER", 257}, {"LEFT_SHIFT", K_SHIFT},
        {"LEFT_CONTROL", K_CTRL}, {"LEFT_ALT", K_ALT}, {"LEFT", 263}, {"UP", 265}, {"DOWN", 264}, {"RIGHT", 262},
        {"DELETE", 261}, {"GRAVE_ACCENT", 96}, {"MOUSE", K_MOUSE},
    };
    for (size_t i = 0; i < sizeof kAlias / sizeof *kAlias; i++)
        if (strcasecmp(s, kAlias[i].n) == 0) return kAlias[i].k;
    return -1;
}

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

char g_mod_language[8];
float g_menu_scale;
bool g_perf;  // isaaclua.ini perf = 1: frame timings on screen
/* isaaclua.ini errors = 1: mod errors and warnings on screen (always in errors.log). Off by default in the
 * release (Egor, 1.0.0: players should not see them), on in the debug build */
#ifdef ISAACLUA_DEBUG
bool g_errors_on = true;
#else
bool g_errors_on = false;
#endif

/*   language = ru              Options.Language for mods (default: the game's language)
 *   keyboard = PLUS+MINUS      button combo that opens the keyboard (two buttons)
 *   L3 = F1                    a pad button pressing a key
 *   # comment */
void kb_load_config(void) {
    FILE *f = fopen("rom:/isaaclua.ini", "r");
    if (!f) return;
    static char buf[4096];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    char *line = buf;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        char *eq = strchr(line, '=');
        line = trim(line);
        if (*line && *line != '#' && *line != ';' && eq) {
            *eq = 0;
            char *k = trim(line), *v = trim(eq + 1), msg[96];
            if (strcasecmp(k, "language") == 0) {
                snprintf(g_mod_language, sizeof g_mod_language, "%s", v);
            } else if (strcasecmp(k, "menu_scale") == 0) {
                g_menu_scale = (float)strtod(v, NULL);
                if (!(g_menu_scale >= 0.3f && g_menu_scale <= 2.0f)) {
                    g_menu_scale = 0;
                    snprintf(msg, sizeof msg, "isaaclua.ini: bad menu_scale %s", v), overlay_add(msg, OVERLAY_WARN);
                }
            } else if (strcasecmp(k, "content") == 0) {
                extern bool g_native_content;
                g_native_content = !(strcmp(v, "0") == 0 || strcasecmp(v, "off") == 0 || strcasecmp(v, "false") == 0);
#ifdef ISAACLUA_DEBUG
            } else if (strcasecmp(k, "autotest") == 0) {
                extern int g_autotest;
                int m = (int)strtol(v, NULL, 10);
                if (strcasecmp(v, "on") == 0 || strcasecmp(v, "true") == 0) m = 30;
                g_autotest = m < 0 ? 0 : m > 600 ? 600 : m;
            } else if (strcasecmp(k, "autotest_batch") == 0) {
                extern int g_autotest_batch_size;
                int m = (int)strtol(v, NULL, 10);
                g_autotest_batch_size = m < 0 ? 0 : m;
            } else if (strcasecmp(k, "autotest_only") == 0) {  // "1, 8, 14": only these batches
                extern char g_autotest_only[64];
                snprintf(g_autotest_only, sizeof g_autotest_only, "%s", v);
            } else if (strcasecmp(k, "autotest_shots") == 0) {
                extern bool g_autotest_shots;
                g_autotest_shots = !(strcmp(v, "0") == 0 || strcasecmp(v, "off") == 0 || strcasecmp(v, "false") == 0);
#endif
            } else if (strcasecmp(k, "errors") == 0) {
                g_errors_on = strcmp(v, "1") == 0 || strcasecmp(v, "on") == 0 || strcasecmp(v, "true") == 0;
            } else if (strcasecmp(k, "perf") == 0) {
                g_perf = strcmp(v, "1") == 0 || strcasecmp(v, "on") == 0 || strcasecmp(v, "true") == 0;
            } else if (strcasecmp(k, "keyboard") == 0) {
                char *plus = strchr(v + 1, '+');  // "PLUS+MINUS", "+ + -" is not supported: write names
                u64 m = 0;
                if (plus) {
                    *plus = 0;
                    int a = button_bit(trim(v)), b = button_bit(trim(plus + 1));
                    if (a >= 0 && b >= 0 && a != b) m = BIT(a) | BIT(b);
                }
                if (m) s_combo = m;
                else snprintf(msg, sizeof msg, "isaaclua.ini: bad keyboard combo"), overlay_add(msg, OVERLAY_WARN);
            } else {
                int b = button_bit(k), key = key_code(v);
                if (b < 0 || key < 0) {
                    snprintf(msg, sizeof msg, "isaaclua.ini: unknown %s = %s", k, v);
                    overlay_add(msg, OVERLAY_WARN);
                } else if (s_nbind < (int)(sizeof s_bind / sizeof *s_bind)) {
                    s_bind[s_nbind++] = (Binding){BIT(b), key};
                    s_bound_mask |= BIT(b);
                }
            }
        }
        line = next;
    }
    s_bound_mask &= ~s_combo;  // the combo keeps working as a combo
}
