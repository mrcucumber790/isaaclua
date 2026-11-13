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
 *    (PC 0x51e854 -> REP+ 0x53184e). Switch: Random(20) returns to 0x1d635c, 0 = attack.
 *  - Delirium (ai_delirium, Switch 0x1837fc; PC 0x4929a0, REP+ 0x4a6880), patch 1.9.7.11 "Mom's / Satan's
 *    foot not sped up, no extra shots": on a form change 1.7.9b rolls Random(100) < chance for 120 frames
 *    of DeliriumAggro (faster, extra shots), else keeps the old aggression and chance += 10. REP+ 0x4a710e
 *    skips the roll for Mom (45) and Satan's foot (84.10) and clears the aggression, and a miss clears it
 *    too. Switch: Random(100) returns to 0x183e28 with the old word in x20.
 *  - Kineti (ai_polty variant 1, Switch 0x204e98) and Blood Cultist (ai_cultist, 0x213a64), patch 1.9.7.11:
 *    REP+ asks Camera::IsPosVisible(own position) after the roll that starts the rock grab (REP+ 0x55e22b)
 *    or the blood shot (0x4ee6c7); off screen they take the no path. Switch: Random(10) returns to 0x2062a4,
 *    Random(3) to 0x214454. */

#define RP_BEAST_SITE_SOUL 0x12b2e0u    /* after ANM2::Play("SoulCall"), w8 = 13 */
#define RP_BEAST_SITE_SUCK 0x12b5bcu    /* after ANM2::Play("SuckStart"), w8 = 8 */
#define RP_BEAST_SITE_DOUBLE 0x12b638u  /* after ANM2::Play("DoubleShot"), w8 = 10 */
#define RP_BEAST_SITE_SUCK_RND1 0x12b47cu
#define RP_BEAST_SITE_SUCK_RND2 0x12b4a4u
#define RP_HUSH_SITE_WIGGLE 0x19d4c0u
#define RP_NEEDLE_SITE 0x1eda6cu
#define RP_CODWORM_SITE 0x1d635cu
#define RP_DELIRIUM_SITE 0x183e28u
#define RP_KINETI_SITE 0x2062a4u       /* after Random(10) before "TeleStart" in ai_polty (Kineti) */
#define RP_CULTIST_SITE 0x214454u      /* after Random(3) before "Attack" in ai_cultist (Blood Cultist) */    /* after Random(100) in ai_delirium's form change */
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
static __attribute__((used)) uintptr_t s_rpb_delirium_site;
static u32 s_rpb_delirium_calls;
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

/* Random(100) in ai_delirium when it takes a new form; *bits = its x20, the old aggression word
 * (bits 0-6 chance, 7-14 DeliriumAggro frames), rewritten by 0x183e28.. from the roll:
 * hit (r < chance) -> aggression 120, chance 0; miss -> aggression kept, chance + 10.
 * REP+ (0x4a7108): Mom (45) or Satan's foot (84.10) -> no roll, aggression 0, chance kept;
 * miss -> aggression 0. */
HIDDEN __attribute__((used)) u32 rpb_delirium_c(u32 max, const u8 *npc, u64 *bits) {
    if (!RP.on || !npc) return s_rpb_rand_u(max);
    s_rpb_delirium_calls++;
    int form = *(const int *)(npc + 0xbf8), var = *(const int *)(npc + 0xbfc);
    u64 b = *bits;
    if (form == 45 || (form == 84 && var == 10)) {
        u64 c = b & 0x7f;
        *bits = (b & ~(u64)0x7fff) | ((c + 0x80 - 10) & 0x7f);  // the miss path adds 10 back
        return 0xffffffffu;                                     // >= any chance: miss
    }
    u32 r = s_rpb_rand_u(max);
    if (r >= (u32)(b & 0x7f)) *bits = b & ~(u64)0x7f80;
    return r;
}

typedef bool (*RpPosVisibleFn)(void *camera, const float *pos);
static RpPosVisibleFn s_rpb_pos_visible;
static u32 s_rpb_offscreen_calls;

/* Camera::IsPosVisible(entity position), as REP+ asks it (Room+0x1928 = camera, Entity+0x310 = position) */
static bool rpb_on_screen(const u8 *e) {
    u8 *room = room_ptr();
    void *cam = room ? *(void **)(room + 0x1928) : 0;
    if (!s_rpb_pos_visible || !cam || !e) return true;
    return s_rpb_pos_visible(cam, (const float *)(e + 0x310));
}

/* IsaacRepentance::Random(max) elsewhere; self = the caller's x19 */
HIDDEN __attribute__((used)) u32 rpb_rand_u_c(u32 max, uintptr_t lr, const u8 *self) {
    if (RP.on && max == 20 && lr == RP.base + RP_CODWORM_SITE) {
        s_rpb_cod_calls++;
        return 0;
    }
    /* 1.9.7.11: Kineti grab rocks / Blood Cultists shoot only while on screen (REP+ 0x55e22b, 0x4ee6c7:
     * Camera::IsPosVisible after the same roll; off screen takes the roll's "no" path) */
    if (RP.on && ((max == 10 && lr == RP.base + RP_KINETI_SITE) || (max == 3 && lr == RP.base + RP_CULTIST_SITE))) {
        u32 r = s_rpb_rand_u(max);
        if (r == 0 && !rpb_on_screen(self)) {
            s_rpb_offscreen_calls++;
            return 1;
        }
        return r;
    }
    return s_rpb_rand_u(max);
}


/* ------------------------------------------------------------ Poker Chip (trinket 76) and the gambling machines
 * REP+ Entity_Slot::Update (0x65d480; PC 1.7.9b 0x639870 has no trinket 76 there) adds a Poker Chip roll next
 * to the Lucky Foot (46) ones: "slightly increases the payout chance" (wiki, Repentance+). Switch
 * Entity_Slot::Update 0x32905c; the slot's RNG is Slot+0x3d8. Sites (return address of the game's call):
 *  - slot machine prize (REP+ 0x65e490): Lucky Foot rolls Random(15) instead of Random(21) (+3 = prize kind);
 *    REP+ also when Poker Chip and (RNG::Next() & 3) == 0. Switch: RNG::Random(21) returns to 0x329850.
 *  - crane game (0x65ea36): 1.7.9b wins on Random(4) == 0; REP+ Next() % 100 - 5 (Poker Chip) < 25.
 *    Switch: RNG::Random(4) returns to 0x329eac, 0 = prize.
 *  - fortune machine (0x65fbbd): the prize chance x0.46 with Lucky Foot, REP+ also with Poker Chip and
 *    Random(5) == 0. Switch: chance in s8, RNG::Random() returns to 0x32a808, player in x22.
 *  - shell game (0x660e7d): after a missed Random(3) and Lucky Foot's second Random(3), REP+ Poker Chip
 *    wins on Random(20) == 0 of the same RNG (Switch: player's RNG x21+0x2e0; Random(3) returns to 0x32a170
 *    first, to 0x32a194 for Lucky Foot's roll).
 *  - restock machine (0x6623cc): on a miss of the tier roll (Random 4/3/2 == 0, or Random(4) != 0 from tier
 *    3) REP+ Poker Chip wins on Random(20) == 0. Switch: 0x32c390 (0 = win) and 0x32c3ac (!0 = win).
 * Golden / Mom's Box payout x2 / x3 (REP+ 0x65f0c1: GetTrinketMultiplier(76), at most 3, as the spawn count)
 * is not done here. */
#define RP_PC_SLOT_PRIZE 0x329850u
#define RP_PC_CRANE 0x329eacu
#define RP_PC_FORTUNE 0x32a808u
#define RP_PC_SHELL1 0x32a170u
#define RP_PC_SHELL2 0x32a194u
#define RP_PC_RESTOCK1 0x32c390u
#define RP_PC_RESTOCK2 0x32c3acu
typedef u32 (*RpbRngUFn)(void *, u32);
typedef float (*RpbRngFFn)(void *);
static __attribute__((used)) RpbRngUFn s_rpb_rng_u;
static __attribute__((used)) RpbRngFFn s_rpb_rng_f;
static u32 (*s_rpb_rng_next)(void *);
static __attribute__((used)) uintptr_t s_rpb_pc_lo, s_rpb_pc_hi, s_rpb_pc_fortune;
static u32 s_rpb_poker_calls, s_rpb_poker_seen;

static bool rpb_poker(void) { return rp_pm_trinket_mult(76) > 0; }
static bool rpb_lucky_foot(void *player) { return player && RP.has && RP.has(player, 46, false); }

/* ------------------------------------------------------------ X - Wheel of Fortune (card 11), 1.9.7.11
 * REP+ UseCard 0x7b34ae: r = Random(100) of the card RNG; r == 0 -> Crane Game (6.16) when achievement 0x25f
 * is unlocked, else fortune machine (6.3); r <= 25 -> fortune machine; else slot machine (6.1). 1.7.9b (Switch
 * UseCard): RNG::Random(3) returns to 0x2ebba4, 0 -> fortune machine (6.3) else slot machine (6.1). Here the
 * roll becomes REP+'s; the crane is set up for the next type 6 spawn (rpb_spawn_var, from hk_spawn_ex). */
#define RP_WOF_SITE 0x2ebba4u
static __attribute__((used)) uintptr_t s_rpb_wof_site;
static bool s_rpb_wof_crane;
static u32 s_rpb_wof_calls;
static bool (*s_rpb_unlocked)(void *, int);
static u32 rpb_wof_c(void *rng) {
    u32 r = s_rpb_rng_u(rng, 100);
    s_rpb_wof_calls++;
    if (r == 0) {
        u8 *m = manager_ptr();
        s_rpb_wof_crane = m && s_rpb_unlocked && s_rpb_unlocked(m + 0x18, 0x25f);
        return 0;
    }
    return r <= 25 ? 0u : 1u;
}
static u32 rpb_spawn_var(u32 type, u32 var) {
    if (type == 6 && s_rpb_wof_crane) {
        s_rpb_wof_crane = false;
        if (RP.on && var == 3) return 16;
    }
    return var;
}

/* RNG::Random(max) inside Entity_Slot::Update; x21 / x22 of the caller */
HIDDEN __attribute__((used)) u32 rpb_rng_u_c(void *rng, u32 max, uintptr_t lr, u8 *x21, void *x22) {
    uintptr_t o = lr - RP.base;
    if (o == RP_WOF_SITE) return RP.on && max == 3 ? rpb_wof_c(rng) : s_rpb_rng_u(rng, max);
    if (!RP.on || !rpb_poker()) return s_rpb_rng_u(rng, max);
    if (o == RP_PC_SLOT_PRIZE || o == RP_PC_CRANE || o == RP_PC_SHELL1 || o == RP_PC_SHELL2 || o == RP_PC_RESTOCK1 ||
        o == RP_PC_RESTOCK2)
        s_rpb_poker_seen++;
    if (o == RP_PC_SLOT_PRIZE && max == 21 && s_rpb_rng_next) {
        if ((s_rpb_rng_next(rng) & 3u) == 0) { max = 15; s_rpb_poker_calls++; }
        return s_rpb_rng_u(rng, max);
    }
    if (o == RP_PC_CRANE && max == 4) {
        u32 r = s_rpb_rng_u(rng, 100);
        s_rpb_poker_calls++;
        return r < 30 ? 0u : 1u;  // REP+: r - 5 < 25
    }
    if (o == RP_PC_SHELL1 && max == 3) {
        u32 r = s_rpb_rng_u(rng, max);
        // without Lucky Foot the game goes straight to the miss: roll Poker Chip's Random(20) here
        if (r != 0 && x21 && !rpb_lucky_foot(x22) && s_rpb_rng_u(x21 + 0x2e0, 20) == 0) { s_rpb_poker_calls++; return 0; }
        return r;
    }
    if (o == RP_PC_SHELL2 && max == 3) {
        u32 r = s_rpb_rng_u(rng, max);
        if (r != 0 && s_rpb_rng_u(rng, 20) == 0) { s_rpb_poker_calls++; return 0; }
        return r;
    }
    if (o == RP_PC_RESTOCK1 && max >= 2 && max <= 4) {
        u32 r = s_rpb_rng_u(rng, max);
        if (r != 0 && s_rpb_rng_u(rng, 20) == 0) { s_rpb_poker_calls++; return 0; }
        return r;
    }
    if (o == RP_PC_RESTOCK2 && max == 4) {
        u32 r = s_rpb_rng_u(rng, max);
        if (r == 0 && s_rpb_rng_u(rng, 20) == 0) { s_rpb_poker_calls++; return 1; }
        return r;
    }
    return s_rpb_rng_u(rng, max);
}

/* before RNG::Random() at the fortune machine: the factor for its chance in s8 */
HIDDEN __attribute__((used)) float rpb_fortune_c(void *rng, void *player) {
    if (!RP.on || !rpb_poker() || rpb_lucky_foot(player)) return 1.0f;
    if (s_rpb_rng_u(rng, 5) != 0) return 1.0f;
    s_rpb_poker_calls++;
    return 0.46f;
}

#ifndef ISAACLUA_HOST_TEST
void rpb_hk_anm_play(void);
void rpb_hk_randf(void);
void rpb_hk_is_playing(void);
void rpb_hk_rand_u(void);
void rpb_hk_rng_u(void);
void rpb_hk_rng_f(void);
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
        "adrp x16, s_rpb_delirium_site\nldr x16, [x16, :lo12:s_rpb_delirium_site]\ncmp x16, x30\nb.eq 2f\n"
        "adrp x16, s_rpb_needle_site\nldr x16, [x16, :lo12:s_rpb_needle_site]\ncmp x16, x30\nb.eq 1f\n"
        "mov x1, x30\nmov x2, x19\nb rpb_rand_u_c\n"
        "1:\nstp x29, x30, [sp, #-16]!\nmov x29, sp\nfmov s0, s8\nbl rpb_needle_c\n"
        "adrp x16, s_rpb_needle_s8\nldr s8, [x16, :lo12:s_rpb_needle_s8]\n"
        "ldp x29, x30, [sp], #16\nret\n"
        "2:\nstp x29, x30, [sp, #-32]!\nmov x29, sp\nstr x20, [sp, #16]\nmov x1, x19\nadd x2, sp, #16\n"
        "bl rpb_delirium_c\nldr x20, [sp, #16]\nldp x29, x30, [sp], #32\nret\n"
        ".hidden rpb_hk_rng_u\n.type rpb_hk_rng_u,%function\n"
        "rpb_hk_rng_u:\n"
        "adrp x16, s_rpb_wof_site\nldr x16, [x16, :lo12:s_rpb_wof_site]\ncmp x30, x16\nb.eq 2f\n"
        "adrp x16, s_rpb_pc_lo\nldr x16, [x16, :lo12:s_rpb_pc_lo]\ncmp x30, x16\nb.lo 1f\n"
        "adrp x16, s_rpb_pc_hi\nldr x16, [x16, :lo12:s_rpb_pc_hi]\ncmp x30, x16\nb.hi 1f\n"
        "2:\nmov x2, x30\nmov x3, x21\nmov x4, x22\nb rpb_rng_u_c\n"
        "1:\nadrp x16, s_rpb_rng_u\nldr x16, [x16, :lo12:s_rpb_rng_u]\nbr x16\n"
        ".hidden rpb_hk_rng_f\n.type rpb_hk_rng_f,%function\n"
        "rpb_hk_rng_f:\n"
        "adrp x16, s_rpb_pc_fortune\nldr x16, [x16, :lo12:s_rpb_pc_fortune]\ncmp x30, x16\nb.ne 1f\n"
        "stp x29, x30, [sp, #-32]!\nmov x29, sp\nstr x0, [sp, #16]\nmov x1, x22\nbl rpb_fortune_c\n"
        "fmul s8, s8, s0\nldr x0, [sp, #16]\nldp x29, x30, [sp], #32\n"
        "1:\nadrp x16, s_rpb_rng_f\nldr x16, [x16, :lo12:s_rpb_rng_f]\nbr x16\n");
#endif

static void rp_bosses_install(const ModView *v) {
#ifndef ISAACLUA_HOST_TEST
    s_rpb_rand_u32 = (u32 (*)(void))api_sym("_ZN15IsaacRepentance9RandomU32Ev");
    s_rpb_hush_site = RP.base + RP_HUSH_SITE_WIGGLE;
    s_rpb_needle_site = RP.base + RP_NEEDLE_SITE;
    s_rpb_delirium_site = RP.base + RP_DELIRIUM_SITE;
    s_rpb_pos_visible = (RpPosVisibleFn)api_sym("_ZN15IsaacRepentance6Camera12IsPosVisibleERKN4KAGE4Math7Vector2E");
    s_rpb_anm_play = (RpAnmPlayFn)hook_got(v, "_ZN15IsaacRepentance4ANM24PlayEPKcb", (void *)rpb_hk_anm_play);
    if (!s_rpb_anm_play) plugin_log("[isaac-repplus] ANM2::Play GOT slot not found, no Beast attack change");
    s_rpb_randf_prev = (RpRandFFn)hook_got(v, "_ZN15IsaacRepentance6RandomEv", (void *)rpb_hk_randf);
    if (!s_rpb_randf_prev) plugin_log("[isaac-repplus] Random() GOT slot not found, no Beast suction change");
    s_rpb_is_playing = (RpAnmIsPlayingFn)hook_got(v, "_ZNK15IsaacRepentance4ANM29IsPlayingEPKc", (void *)rpb_hk_is_playing);
    if (!s_rpb_is_playing) plugin_log("[isaac-repplus] ANM2::IsPlaying GOT slot not found, no Hush change");
    s_rpb_rand_u = (RpRandUFn)hook_got(v, "_ZN15IsaacRepentance6RandomEj", (void *)rpb_hk_rand_u);
    s_rpb_pc_lo = RP.base + RP_PC_SLOT_PRIZE;
    s_rpb_pc_hi = RP.base + RP_PC_RESTOCK2;
    s_rpb_pc_fortune = RP.base + RP_PC_FORTUNE;
    s_rpb_wof_site = RP.base + RP_WOF_SITE;
    s_rpb_unlocked = (bool (*)(void *, int))api_sym("_ZNK15IsaacRepentance18PersistentGameData8UnlockedENS_12eAchievementE");
    s_rpb_rng_next = (u32 (*)(void *))api_sym("_ZN15IsaacRepentance3RNG4NextEv");
    s_rpb_rng_u = (RpbRngUFn)hook_got(v, "_ZN15IsaacRepentance3RNG6RandomEj", (void *)rpb_hk_rng_u);
    if (!s_rpb_rng_u) plugin_log("[isaac-repplus] RNG::Random(n) GOT slot not found, no Poker Chip odds");
    s_rpb_rng_f = (RpbRngFFn)hook_got(v, "_ZN15IsaacRepentance3RNG6RandomEv", (void *)rpb_hk_rng_f);
    if (!s_rpb_rng_f) plugin_log("[isaac-repplus] RNG::Random() GOT slot not found, no Poker Chip fortune odds");
    if (!s_rpb_rand_u) plugin_log("[isaac-repplus] Random(n) GOT slot not found, no Needle/Cod Worm change");
#else
    (void)v;
#endif
}

/* C.repplus_ai(): hits of the hooks above since start (Beast, suction, Hush, Needle, Cod Worm, Delirium,
 * off screen, Poker Chip) */
static int core_repplus_ai(lua_State *l) {
    lua_pushinteger(l, s_rpb_beast_calls);
    lua_pushinteger(l, s_rpb_suck_calls);
    lua_pushinteger(l, s_rpb_hush_calls);
    lua_pushinteger(l, s_rpb_needle_calls);
    lua_pushinteger(l, s_rpb_cod_calls);
    lua_pushinteger(l, s_rpb_delirium_calls);
    lua_pushinteger(l, s_rpb_offscreen_calls);
    lua_pushinteger(l, s_rpb_poker_calls);
    lua_pushinteger(l, s_rpb_poker_seen);
    lua_pushinteger(l, s_rpb_wof_calls);
    return 10;
}
