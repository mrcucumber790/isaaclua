/* ------------------------------------------------------------ isaac-repplus: enemy / boss AI changes
 *
 * Included at the end of src/repplus_hooks.inc; rp_bosses_install() is called from repplus_install()
 * after every other hook, so each hook here chains to whatever the GOT slot held before (an earlier
 * isaac-repplus hook or the game's function). Every change is made only at exact return sites (LR) in
 * Repentance.nro 1.7.9b (build 91c73fdd) and only while the isaac-repplus mod is on (RP.on).
 *
 * Found by diffing PC Repentance 1.7.9b (isaac-ng.exe) against REP+ 1.9.7.16 (isaac-ng-repplus.exe):
 *
 *  - The Beast (Entity_NPC_Beast::UpdateAI, Switch 0x127ea4; PC 0x57f400, REP+ 0x592590), patch 1.9.7.13
 *    "attacks random, not tied to health". Idle state, second attack of a cycle (+0xbf0 == 1): 1.7.9b
 *    picks SuckStart (state 8) while the fight phase (+0xbf4) is 0, else DoubleShot (10) / SoulCall (13)
 *    50/50 (Switch 0x12b2b4). REP+ (0x593691) picks one of the three with RandomU32() % 3, at any phase.
 *    The three branches each call ANM2::Play and then only set w8 = state and join at 0x12b63c (x19 =
 *    the Beast), so the ANM2::Play hook plays the picked animation and returns to that branch's site.
 *  - The Beast, suction (states 8/9): 1.7.9b ends it with MoveEnd after 360 frames (cmp 0x167, Switch
 *    0x12b530; PC 0x580aa6), REP+ after 240 (0x593cb6), unless phase 2. StateFrame (+0x400) is read
 *    again after IsaacRepentance::Random() (float), which is called just before on every frame of that
 *    state (returns to 0x12b47c / 0x12b4a4, x19 = the Beast): there a StateFrame >= 240 is raised to 360.
 *  - Hush (ai_hush, Switch 0x19a48c; PC 0x4ac870, REP+ 0x4bf680), patch 1.9.7.4 "less aggressive below
 *    50% HP": 1.7.9b skips the attack cooldown (StateFrame > 0) and the 50% "wait" roll while HP/MaxHP
 *    < 0.5 (PC 0x4ae3a9 / 0x4ae453); REP+ uses 0.005 for both. Switch 0x19d498: s8 = 0.5, s9 = HP
 *    fraction, x27 = Hush; the first check (19d49c) jumps to ANM2::IsPlaying("Wiggle") (returns to
 *    0x19d4c0), the second (19d4d8) compares s9 with s8 again after Random(2). The IsPlaying hook at that
 *    site answers false (= cooldown not over) while StateFrame > 0 and HP >= 0.5%, and sets s8 to 0.005
 *    (callee-saved, not read again before it is reloaded).
 *  - Needle / Pasty (ai_needle, Switch 0x1ed1e8; PC 0x53b650, REP+ 0x54ef10), patch 1.9.7.8 "min spawn
 *    distance up": the resurface spot must be 120..240 px from the nearest player (dist^2 14400..57600)
 *    instead of 80..200 (6400..40000). Switch 0x1eda60: s8 = dist^2, Random(2) returns to 0x1eda6c, then
 *    s8 is compared with 40000 and 6400 and not read again: s8 becomes 20000 (in range) or 0 (out).
 *  - Cod Worm (ai_cod_worm, Switch 0x1d5f14; PC 0x51e6a0, REP+ 0x531690), patch 1.9.7.11: 1.7.9b pops out
 *    of the ground only on a 1-in-20 roll each frame without line of sight, REP+ without the roll
 *    (PC 0x51e854 -> REP+ 0x53184e). Switch: Random(20) returns to 0x1d635c, 0 = attack. */

#define RP_BEAST_SITE_SOUL 0x12b2e0u    /* after ANM2::Play("SoulCall"), w8 = 13 */
#define RP_BEAST_SITE_SUCK 0x12b5bcu    /* after ANM2::Play("SuckStart"), w8 = 8 */
#define RP_BEAST_SITE_DOUBLE 0x12b638u  /* after ANM2::Play("DoubleShot"), w8 = 10 */
#define RP_BEAST_SITE_SUCK_RND1 0x12b47cu
#define RP_BEAST_SITE_SUCK_RND2 0x12b4a4u
#define RP_HUSH_SITE_WIGGLE 0x19d4c0u
#define RP_NEEDLE_SITE 0x1eda6cu
#define RP_CODWORM_SITE 0x1d635cu
#define kNpc_StateFrame 0x400
#define kBeast_Phase 0xbf4

typedef void (*RpAnmPlayFn)(void *, const char *, bool);
typedef bool (*RpAnmIsPlayingFn)(const void *, const char *);
typedef float (*RpRandFFn)(void);
typedef u32 (*RpRandUFn)(u32);
static RpAnmPlayFn s_rpb_anm_play;
static RpRandFFn s_rpb_randf;
static u32 (*s_rpb_rand_u32)(void);
/* read by the trampolines below */
static __attribute__((used)) RpAnmIsPlayingFn s_rpb_is_playing;
static __attribute__((used)) RpRandUFn s_rpb_rand_u;
static __attribute__((used)) uintptr_t s_rpb_hush_site, s_rpb_needle_site;
static __attribute__((used)) float s_rpb_hush_s8 = 0.5f, s_rpb_needle_s8;
static u32 s_rpb_beast_calls, s_rpb_suck_calls, s_rpb_hush_calls, s_rpb_needle_calls, s_rpb_cod_calls;

/* ANM2::Play(anm, name, force); returns where the caller continues */
HIDDEN __attribute__((used)) uintptr_t rpb_anm_play_c(void *anm, const char *name, bool force, uintptr_t lr) {
    uintptr_t off = lr - RP.base;
    if (RP.on && s_rpb_rand_u32 && (off == RP_BEAST_SITE_SOUL || off == RP_BEAST_SITE_SUCK || off == RP_BEAST_SITE_DOUBLE)) {
        static const char *const names[3] = {"SuckStart", "DoubleShot", "SoulCall"};  // REP+ 0x5936f9 / d5 / b2
        static const u32 sites[3] = {RP_BEAST_SITE_SUCK, RP_BEAST_SITE_DOUBLE, RP_BEAST_SITE_SOUL};
        u32 k = s_rpb_rand_u32() % 3u;
        s_rpb_anm_play(anm, names[k], force);
        s_rpb_beast_calls++;
        return RP.base + sites[k];
    }
    s_rpb_anm_play(anm, name, force);
    return lr;
}

/* after IsaacRepentance::Random() (float) returned: x19 of the caller in `self` */
HIDDEN __attribute__((used)) void rpb_randf_c(uintptr_t lr, u8 *self) {
    uintptr_t off = lr - RP.base;
    if (!RP.on || (off != RP_BEAST_SITE_SUCK_RND1 && off != RP_BEAST_SITE_SUCK_RND2) || !self) return;
    int *sf = (int *)(self + kNpc_StateFrame);
    if (*(int *)(self + kBeast_Phase) != 2 && *sf >= 0xf0 && *sf < 0x168) {
        *sf = 0x168;  // 1.7.9b's own limit: MoveEnd follows at 0x12b538
        s_rpb_suck_calls++;
    }
}

/* ANM2::IsPlaying("Wiggle") in ai_hush's attack choice: playing = what the game's answered */
HIDDEN __attribute__((used)) bool rpb_hush_c(bool playing, const u8 *hush, float hp) {
    if (!RP.on || !hush) {
        s_rpb_hush_s8 = 0.5f;
        return playing;
    }
    s_rpb_hush_s8 = 0.005f;  // REP+ 0x4c12b8 / 0x4c1365
    if (playing && !(hp < 0.005f) && *(const int *)(hush + kNpc_StateFrame) > 0) {
        s_rpb_hush_calls++;
        return false;  // as 1.7.9b at HP >= 50%: wait for the cooldown
    }
    return playing;
}

/* IsaacRepentance::Random(max) at Needle's resurface roll: d2 = s8 there; the result goes back to s8 */
HIDDEN __attribute__((used)) u32 rpb_needle_c(u32 max, float d2) {
    u32 r = s_rpb_rand_u(max);
    if (RP.on) {
        s_rpb_needle_s8 = (d2 > 14400.0f && d2 < 57600.0f) ? 20000.0f : 0.0f;  // REP+ 0x54f5c5 / 0x54f5d8
        s_rpb_needle_calls++;
    } else {
        s_rpb_needle_s8 = d2;
    }
    return r;
}

/* IsaacRepentance::Random(max) elsewhere */
HIDDEN __attribute__((used)) u32 rpb_rand_u_c(u32 max, uintptr_t lr) {
    if (RP.on && max == 20 && lr == RP.base + RP_CODWORM_SITE) {
        s_rpb_cod_calls++;
        return 0;
    }
    return s_rpb_rand_u(max);
}

#ifndef ISAACLUA_HOST_TEST
void rpb_hk_anm_play(void);
void rpb_hk_randf(void);
void rpb_hk_is_playing(void);
void rpb_hk_rand_u(void);
static __attribute__((used)) RpRandFFn s_rpb_randf_prev;
__asm__(".text\n.p2align 2\n"
        ".hidden rpb_hk_anm_play\n.type rpb_hk_anm_play,%function\n"
        "rpb_hk_anm_play:\n"
        "stp x29, x30, [sp, #-16]!\nmov x29, sp\nmov x3, x30\nbl rpb_anm_play_c\n"
        "ldp x29, x30, [sp], #16\nmov x30, x0\nret\n"
        /* Random(): float result in s0 kept across the C call */
        ".hidden rpb_hk_randf\n.type rpb_hk_randf,%function\n"
        "rpb_hk_randf:\n"
        "stp x29, x30, [sp, #-32]!\nmov x29, sp\n"
        "adrp x16, s_rpb_randf_prev\nldr x16, [x16, :lo12:s_rpb_randf_prev]\nblr x16\n"
        "str s0, [sp, #16]\nldr x0, [sp, #8]\nmov x1, x19\nbl rpb_randf_c\n"
        "ldr s0, [sp, #16]\nldp x29, x30, [sp], #32\nret\n"
        ".hidden rpb_hk_is_playing\n.type rpb_hk_is_playing,%function\n"
        "rpb_hk_is_playing:\n"
        "adrp x16, s_rpb_hush_site\nldr x16, [x16, :lo12:s_rpb_hush_site]\ncmp x16, x30\nb.eq 1f\n"
        "adrp x16, s_rpb_is_playing\nldr x16, [x16, :lo12:s_rpb_is_playing]\nbr x16\n"
        "1:\nstp x29, x30, [sp, #-16]!\nmov x29, sp\n"
        "adrp x16, s_rpb_is_playing\nldr x16, [x16, :lo12:s_rpb_is_playing]\nblr x16\n"
        "and w0, w0, #1\nmov x1, x27\nfmov s0, s9\nbl rpb_hush_c\n"
        "adrp x16, s_rpb_hush_s8\nldr s8, [x16, :lo12:s_rpb_hush_s8]\n"
        "ldp x29, x30, [sp], #16\nret\n"
        ".hidden rpb_hk_rand_u\n.type rpb_hk_rand_u,%function\n"
        "rpb_hk_rand_u:\n"
        "adrp x16, s_rpb_needle_site\nldr x16, [x16, :lo12:s_rpb_needle_site]\ncmp x16, x30\nb.eq 1f\n"
        "mov x1, x30\nb rpb_rand_u_c\n"
        "1:\nstp x29, x30, [sp, #-16]!\nmov x29, sp\nfmov s0, s8\nbl rpb_needle_c\n"
        "adrp x16, s_rpb_needle_s8\nldr s8, [x16, :lo12:s_rpb_needle_s8]\n"
        "ldp x29, x30, [sp], #16\nret\n");
#endif

static void rp_bosses_install(const ModView *v) {
#ifndef ISAACLUA_HOST_TEST
    s_rpb_rand_u32 = (u32 (*)(void))api_sym("_ZN15IsaacRepentance9RandomU32Ev");
    s_rpb_hush_site = RP.base + RP_HUSH_SITE_WIGGLE;
    s_rpb_needle_site = RP.base + RP_NEEDLE_SITE;
    s_rpb_anm_play = (RpAnmPlayFn)hook_got(v, "_ZN15IsaacRepentance4ANM24PlayEPKcb", (void *)rpb_hk_anm_play);
    if (!s_rpb_anm_play) plugin_log("[isaac-repplus] ANM2::Play GOT slot not found, no Beast attack change");
    s_rpb_randf_prev = (RpRandFFn)hook_got(v, "_ZN15IsaacRepentance6RandomEv", (void *)rpb_hk_randf);
    if (!s_rpb_randf_prev) plugin_log("[isaac-repplus] Random() GOT slot not found, no Beast suction change");
    s_rpb_is_playing = (RpAnmIsPlayingFn)hook_got(v, "_ZNK15IsaacRepentance4ANM29IsPlayingEPKc", (void *)rpb_hk_is_playing);
    if (!s_rpb_is_playing) plugin_log("[isaac-repplus] ANM2::IsPlaying GOT slot not found, no Hush change");
    s_rpb_rand_u = (RpRandUFn)hook_got(v, "_ZN15IsaacRepentance6RandomEj", (void *)rpb_hk_rand_u);
    if (!s_rpb_rand_u) plugin_log("[isaac-repplus] Random(n) GOT slot not found, no Needle/Cod Worm change");
#else
    (void)v;
#endif
}
