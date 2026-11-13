/* ------------------------------------------------------------ isaac-repplus: native call-site hooks
 *
 * Appended to isaaclua's src/main.c by isaac-repplus/build.sh (the isaaclua tree itself is not edited).
 * Repentance+ changes that sit in the middle of Entity_Player::EvaluateItems (a running sum, later fed
 * through a non-linear formula or multiplied) cannot be redone from MC_EVALUATE_CACHE. Every call from
 * Repentance.nro to one of its exported functions goes through its PLT/GOT, so the function called right
 * after the place where REP+ inserted its code is hooked, and the hook, when it returns to exactly that
 * call site (LR), adds what REP+ adds there. Other callers only pay a compare.
 *
 * Sites (Switch EvaluateItems 0x280804, build 91c73fdd; REP+ = PC isaac-ng.exe of REP+, func 0x7638e0):
 *  - tears: the "tears up" sum is [sp+0x190] from 0x2815e0 until it is read at 0x2817f0 for
 *    16 - 6*sqrt(1.3*x + 1). REP+ drops 1.7.9b's Odd Mushroom (Thin) +1 from it; only additions
 *    follow until the formula, on Switch too, so that is undone at the return of
 *    TemporaryEffects::HasEffect(625) (0x281790 -> 0x281794), the last call before it. (REP+'s Odd Mushroom,
 *    Equality! and 2Spooky bonuses, 0x764fd7..0x7650d4, go to the shots per second: rp_equality_c.)
 *  - shot speed: REP+ adds 2Spooky (0x767bcc..0x767c16) after Cancer's (trinket 27) +0.5 and before the
 *    multipliers (item 5, 149, ...); Switch has the same order: added at the return of
 *    HasCollectible(5) (0x28406c -> 0x284070), after Cancer's sum is stored to Player+0x1838. */

#define RP_SITE_TEARS 0x281794u
#define RP_SITE_SHOTSPEED 0x284070u
#define RP_SP_TEARS_UP 0x190

typedef bool (*RpHasFn)(void *, int, bool);
typedef int (*RpNumFn)(void *, int, bool);
typedef bool (*RpTeHasFn)(void *, int);
typedef int (*RpTeNumFn)(void *, int);
typedef int (*RpMultFn)(void *, int);
static struct {
    uintptr_t base;
    bool on;  // set from the Lua layer (C.repplus): the isaac-repplus mod is enabled
    RpHasFn has;            // Entity_Player::HasCollectible (what the GOT led to before)
    RpTeHasFn te_has;       // TemporaryEffects::HasEffect(collectible) (same)
    RpNumFn num;            // Entity_Player::NumCollectibleHeld
    RpTeNumFn te_num;       // TemporaryEffects::GetEffectNum(collectible)
    RpTeHasFn te_has_trinket;  // TemporaryEffects::HasEffect(trinket)
    RpMultFn mult;          // Entity_Player::GetTrinketMultiplier
    u32 tears_calls, ss_calls, blood_calls, linger_calls, megablast_calls, superball_calls, leash_calls, epi_calls, penny_calls, ph_calls, ithurts_calls, bridge_calls, void_calls, bum_calls, gold_calls, pocket_calls, gulp_calls, deal_calls, tear_calls, coin_calls, magnet_calls, callus_calls, clip_calls, head_calls, eq_calls, crown_calls, skull_calls, rocket_calls, bomb_calls, necro_calls, infamy_calls, adopt_calls, dim_calls, sale_calls, jar_calls;
} RP;

/* what REP+ adds to the tears-up sum that 1.7.9b does not */
static float rp_tears_up(void *p, void *te) {
    float d = 0.0f;
    /* Odd Mushroom (Thin): 1.7.9b puts it in this sum twice, +1 when held or active (0x2815fc) and its held
     * + effect count in the item count chain (0x2812dc..0x2812f8, worth 0.7 each here: HW 1 copy = +1.7);
     * REP+ has neither before the formula (its chain 0x764580 goes from 370 straight to 341) and adds its
     * bonus to the shots per second after it instead (rp_equality_c below) */
    if (RP.has(p, 120, false) || RP.te_has(te, 120))
        d -= 1.0f + 0.7f * (float)(RP.num(p, 120, false) + RP.te_num(te, 120));
    return d;
}

static u32 *s_rp_astral_flags;  // Astral Projection in Boss Rush (below)
HIDDEN __attribute__((used)) bool rp_te_has_effect_c(void *te, int id, uintptr_t sp, uintptr_t lr, void *player) {
    bool r = RP.te_has(te, id);
    if (s_rp_astral_flags && lr == RP.base + 0x2a83b4u) {  // RP_SITE_ASTRAL_TE
        *s_rp_astral_flags |= 1u;
        s_rp_astral_flags = NULL;
    }
    if (RP.on && lr == RP.base + RP_SITE_TEARS) {
        *(float *)(sp + RP_SP_TEARS_UP) += rp_tears_up(player, te);
        RP.tears_calls++;
    }
    return r;
}

/* Blood Rights (186), REP+ 0x775db7 / 0x7658f0:
 *  - Entity_Player::TakeDamage counts hits (Player+0x16f4, the Bloody Lust / Bloody Gust counter) also
 *    for a Blood Rights holder: the HasCollectible(695) check that guards the count (returns to 0x2a61dc)
 *    answers true for one;
 *  - EvaluateItems, after Bloody Gust and before the G FUEL weapon multiplier: while Blood Rights' effect
 *    is on, + clamp(count, 0, 999)*0.15 + 0.33 to the shots per second (s9 there), unless the flag that
 *    also turns Bloody Gust off is set ([sp+0x84]; REP+ Player+0x1519). Added by the hook of
 *    GetGFuelWeaponType, the next call (returns to 0x282168). */
#define RP_SITE_BLOODHIT 0x2a61dcu
#define RP_SITE_BLOODSPS 0x282168u
#define RP_PL_BLOOD_HITS 0x16f4
typedef int (*RpIntFn)(void *);
static __attribute__((used)) RpIntFn s_rp_gfuel;

HIDDEN __attribute__((used)) float rp_gfuel_c(void *p, uintptr_t lr, uintptr_t caller_sp, void *te) {
    if (!RP.on || lr != RP.base + RP_SITE_BLOODSPS) return 0.0f;
    if (*(u32 *)(caller_sp + 0x84) != 0 || !RP.te_has(te, 186)) return 0.0f;
    float n = (float)*(int16_t *)((u8 *)p + RP_PL_BLOOD_HITS);
    if (n < 0.0f) n = 0.0f;
    if (n > 999.0f) n = 999.0f;
    RP.blood_calls++;
    return n * 0.15f + 0.33f;
}

/* Equality (103), REP+ Entity_Player::Update 0x7834d2 / EvaluateItems 0x76502a: while the player holds it with
 * as many coins as bombs and keys, + 2*multiplier to the shots per second (fire delay cache; REP+ keeps a trinket
 * effect 103 for it and re-evaluates when it changes, done in repplus.lua). Switch EvaluateItems has the shots
 * per second (before the weapon multipliers) in s8 and TemporaryEffects in x25 when GetGFuelWeaponType returns
 * to 0x281bc8. */
#define RP_SITE_EQUALITY 0x281bc8u
static __attribute__((used)) uintptr_t s_rp_eq_site = (uintptr_t)-1;
HIDDEN __attribute__((used)) float rp_equality_c(u8 *p, void *te) {
    if (!RP.on || !p || !RP.mult) return 0.0f;
    float d = 0.0f;
    /* REP+ 0x764fbd..0x7650d4 adds these to the shots per second ([ebp-0x47c] = 30/(delay+1), set at
     * 0x764cad right after the 16 - 6*sqrt(1.3x+1) formula), not to the tears-up sum:
     *  Odd Mushroom (Thin): +1 + 0.7*(held + effect); 2Spooky: +0.5 per copy */
    if (te && (RP.has(p, 120, false) || RP.te_has(te, 120)))
        d += 1.0f + 0.7f * (float)(RP.num(p, 120, false) + RP.te_num(te, 120));
    d += 0.5f * (float)RP.num(p, 554, false);
    s32 c = *(s32 *)(p + kPlayer_Coins), b = *(s32 *)(p + kPlayer_Bombs), k = *(s32 *)(p + kPlayer_Keys);
    if (c != b || c != k) return d;
    int m = RP.mult(p, 103);
    if (m <= 0) return d;
    RP.eq_calls++;
    return d + 2.0f * (float)m;
}

/* Epiphora (368), REP+ 0x780448 / 0x76575f: the firing counter (Switch Player+0x1d1c) counts to 180 and
 * the tears multiplier is 1 + counter/180, re-evaluated every frame it grows (smooth), instead of 1.7.9b's
 * 1 + floor(counter/90)/3 up to 270, evaluated every 90 frames. EvaluateItems' HasCollectible(368) (returns
 * to 0x281fd4) is answered false by the stub, which multiplies the shots per second (s10 there) itself;
 * control_shooting's one (returns to 0x2b38fc) re-evaluates while the counter is in 1..179. */
#define RP_SITE_EPI_EVAL 0x281fd4u
#define RP_SITE_EPI_SHOOT 0x2b38fcu
#define RP_PL_EPI_CTR 0x1d1c
static __attribute__((used)) uintptr_t s_rp_epi_site = (uintptr_t)-1;
static VoidFn s_rp_eval_items;

HIDDEN __attribute__((used)) float rp_epi_mult_c(void *p, int id) {
    if (!RP.on || id != 368) return -1.0f;
    if (!RP.has(p, 368, false)) return 0.0f;
    int c = *(int32_t *)((u8 *)p + RP_PL_EPI_CTR);
    if (c < 0) c = 0;
    if (c > 180) c = 180;
    RP.epi_calls++;
    return 1.0f + (float)c / 180.0f;
}

/* It Hurts (560), Entity_Player::TakeDamage (Switch HasCollectible returns to 0x2a68ec; REP+ 0x7763dd): in greed
 * and greedier (Game difficulty >= 2) the tear burst comes only 3 times, counted in a byte REP+ adds to the
 * player (+0x2ed0, zeroed only in the player's constructor 0x757c4a). Kept here by player pointer and InitSeed. */
#define RP_SITE_IT_HURTS 0x2a68ecu
static struct { void *p; u32 seed; u8 n; } s_rp_ithurts[8];
static bool rp_it_hurts_allowed(void *p) {
    u8 *g = game_ptr();
    if (!g || *(u32 *)(g + 0x2f0028) < 2) return true;
    u32 seed = *(u32 *)((u8 *)p + kEnt_InitSeed);
    int slot = -1;
    for (int i = 0; i < 8; i++)
        if (s_rp_ithurts[i].p == p) { slot = i; break; }
    if (slot < 0) {
        for (int i = 0; i < 8 && slot < 0; i++)
            if (!s_rp_ithurts[i].p) slot = i;
        if (slot < 0) slot = 0;
        s_rp_ithurts[slot].p = p, s_rp_ithurts[slot].seed = seed, s_rp_ithurts[slot].n = 0;
    }
    if (s_rp_ithurts[slot].seed != seed) s_rp_ithurts[slot].seed = seed, s_rp_ithurts[slot].n = 0;
    if (s_rp_ithurts[slot].n >= 3) return false;
    s_rp_ithurts[slot].n++;
    RP.ithurts_calls++;
    return true;
}

/* Callus (trinket 14), REP+ GridEntity::hurt_func 0x70a454 and Entity_Player::TakeDamage 0x7740ac:
 *  - multiplier > 1: spikes (grid type 8) in a sacrifice room (13) hurt half as much, not in greed modes;
 *    Switch hurt_func passes the grid type as the source's variant (type 0), so it is done before
 *    ENTITY_TAKE_DMG in take_dmg (main.c);
 *  - multiplier > 2: damage is cut like The Wafer (Switch: HasCollectible(108) returns to 0x2a475c). */
#define RP_SITE_WAFER 0x2a475cu
/* Walnut (108), REP+ Entity_Player::TakeDamage 0x773358: when it breaks, each of its drops (trinket, heart,
 * key, coin) comes multiplier times instead of once (Switch Game::SpawnEx returns to the four LRs below,
 * spawner = the player; copies are made in hk_spawn_ex, main.c). */
static int rp_pm_trinket_mult(int id);
static int rp_spawn_extra(uintptr_t lr, void *spawner, u32 type, u32 var) {
    if (!RP.on || !RP.mult) return 0;
    uintptr_t o = lr - RP.base;
    /* Golden Poker Chip (76) or Poker Chip + Mom's Box: REP+ Entity_Slot::Update pays its pickups through
     * one helper (0x665a30) with a count of min(GetTrinketMultiplier(76), 3) (0x65f0c1) where 1.7.9b spawns
     * one: hearts, coins, keys, bombs, pills, batteries, cards (5.10/20/30/40/70/90/300). Switch: Game::SpawnEx
     * returns inside Entity_Slot::Update 0x32905c..0x32e5c0. */
    if (type == 5 && o >= 0x32905cu && o < 0x32e5c0u &&
        (var == 10 || var == 20 || var == 30 || var == 40 || var == 70 || var == 90 || var == 300)) {
        int m = rp_pm_trinket_mult(76);
        if (m > 3) m = 3;
        return m > 1 ? m - 1 : 0;
    }
    if (!spawner) return 0;
    if (o != 0x2a3ac0u && o != 0x2a3b14u && o != 0x2a3b68u && o != 0x2a3bc4u) return 0;
    if (ENT_I32(spawner, kEnt_Type) != 1) return 0;
    int m = RP.mult(spawner, 108);
    if (m <= 1) return 0;
    RP.callus_calls++;
    return m - 1;
}
static float rp_player_dmg(void *p, float amount, u64 flags, const void *ref) {
    (void)flags;
    if (!RP.on || !RP.mult || !ref || *(const s32 *)ref != 0 || ((const s32 *)ref)[1] != 8) return amount;
    u8 *g = game_ptr(), *r = room_ptr();
    if (!g || !r || *(u32 *)(g + 0x2f0028) >= 2 || *(s32 *)(r + 0x10) != 13) return amount;
    if (RP.mult(p, 14) <= 1) return amount;
    RP.callus_calls++;
    return amount * 0.5f;
}

/* Astral Projection (677) in Boss Rush (REP+ wiki: 1.7.9b never triggers it there). Entity_Player::TakeDamage
 * only starts it while the room's descriptor is not cleared (desc+0x50 bit 0), and the Boss Rush room stays
 * "cleared" during its waves. Switch: HasCollectible(677) returns to 0x2a8384, then the flag is tested and,
 * when clear, TemporaryEffects::HasEffect(677) returns to 0x2a83b4. While the Boss Rush ambush runs
 * (room type 17, Ambush at Game+0x26faa8, running byte +0x38) the bit is dropped between the two calls. */
#define RP_SITE_ASTRAL_HAS 0x2a8384u
#define RP_SITE_ASTRAL_TE 0x2a83b4u
static u32 s_rp_astral_calls;
static void rp_astral_boss_rush(void) {
    u8 *g = game_ptr(), *r = room_ptr();
    if (!g || !r || *(s32 *)(r + 0x10) != 17 || !*(g + 0x26fae0)) return;
    u8 *d = *(u8 **)(r + 8);
    if (!d || !(*(u32 *)(d + 0x50) & 1)) return;
    s_rp_astral_flags = (u32 *)(d + 0x50);
    *s_rp_astral_flags &= ~1u;
    s_rp_astral_calls++;
}

HIDDEN __attribute__((used)) bool rp_has_collectible_c(void *p, int id, bool only_real, uintptr_t lr) {
    if (RP.on && id == 677 && lr == RP.base + RP_SITE_ASTRAL_HAS) {
        bool h = RP.has(p, 677, only_real);
        if (h) rp_astral_boss_rush();
        return h;
    }
    /* Black Powder (420), REP+ Entity_Player::GetBombScale 0x781263: it gives the Mr. Mega (106) bomb scale 1.5
     * (bigger explosion, 185 damage). Switch checks Mr. Mega for the scale in GetBombScale (returns to 0x2ba370)
     * and inlined in control_bombs (0x2ba9b8) and FireBomb (0x2e4330); those checks only pick the scale. */
    if (RP.on && id == 106 && (lr == RP.base + 0x2ba370u || lr == RP.base + 0x2ba9b8u || lr == RP.base + 0x2e4330u)) {
        if (RP.has(p, 106, only_real)) return true;
        if (RP.has(p, 420, false)) { RP.bomb_calls++; return true; }
        return false;
    }
    if (RP.on && lr == RP.base + RP_SITE_WAFER && id == 108) {
        if (RP.has(p, 108, only_real)) return true;
        if (RP.mult && RP.mult(p, 14) > 2) { RP.callus_calls++; return true; }
        return false;
    }
    if (RP.on && lr == RP.base + RP_SITE_IT_HURTS && id == 560)
        return RP.has(p, 560, only_real) && rp_it_hurts_allowed(p);
    if (RP.on && lr == RP.base + RP_SITE_EPI_SHOOT && id == 368) {
        bool r = RP.has(p, 368, only_real);
        int c = *(int32_t *)((u8 *)p + RP_PL_EPI_CTR);
        if (r && c > 0 && c < 180 && s_rp_eval_items) {
            *(u32 *)((u8 *)p + 0x1958) |= 2;  // fire delay
            s_rp_eval_items(p);
        }
        return r;
    }
    if (RP.on && lr == RP.base + RP_SITE_BLOODHIT && id == 695)
        return RP.has(p, 695, only_real) || RP.has(p, 186, only_real);
    if (RP.on && lr == RP.base + RP_SITE_SHOTSPEED) {
        /* 2Spooky (REP+ 0x767bf4): +0.2 shot speed per copy */
        *(float *)((u8 *)p + kPlayer_ShotSpeed) += 0.2f * (float)RP.num(p, 554, false);
        /* Ludovico Technique (REP+ 0x7679cb): +0.2 shot speed per copy, same accumulator */
        *(float *)((u8 *)p + kPlayer_ShotSpeed) += 0.2f * (float)RP.num(p, 329, false);
        RP.ss_calls++;
    }
    return RP.has(p, id, only_real);
}

/* Tonsil (trinket 97) and Bag Lunch (105) in Entity_Player::TakeDamage (Switch 0x2a73a0..0x2a7448;
 * REP+ 0x776ba7..0x776c63, Player::Init 0x7563b6, tonsil reset 0x7da020):
 *  - the hit counter (Player+0x16f8) is set to RandomInt(7)+6 (REP+) instead of RandomInt(9)+12, at
 *    Player::Init (RNG::Random returns to 0x279354) and after each tonsil (returns to 0x2a7404): the
 *    game adds 12 to what Random returns, so the hook returns Random(7)-6;
 *  - tonsils given (Player+0x16fa) go up to GetTrinketMultiplier(97)+1 instead of 2 before the trinket
 *    is removed: 1.7.9b only adds one while the count is <= 1, so the count is read at the start
 *    (GetTrinketMultiplier returns to 0x2a73ac), the missing one is added at the reset, and
 *    TryRemoveTrinket (returns to 0x2a7424) is skipped while the count is below the cap;
 *  - Bag Lunch spawns its item at 1/20, 1/10 with multiplier 2, 1/7 with 3 (REP+) instead of 1/50
 *    (Random returns to 0x2a7448); Wish Bone (104) the same (REP+ 0x776da4, Switch 0x2a7590). */
#define RP_SITE_TONSIL_INIT 0x279354u
#define RP_SITE_TONSIL_MULT 0x2a73acu
#define RP_SITE_TONSIL_RESET 0x2a7404u
#define RP_SITE_TONSIL_REMOVE 0x2a7424u
#define RP_SITE_BAGLUNCH 0x2a7448u
#define RP_SITE_WISHBONE 0x2a7590u
#define RP_PL_TONSIL_CTR 0x16f8
#define RP_PL_TONSIL_NUM 0x16fa
#define RP_PL_CACHE_FLAGS 0x1958
typedef u32 (*RpRandFn)(void *, u32);
typedef bool (*RpRemoveTrinketFn)(void *, int);
static RpRandFn s_rp_random;
static RpMultFn s_rp_mult_got;
static RpRemoveTrinketFn s_rp_remove_trinket;
static void *s_rp_tonsil_p;
static int s_rp_tonsil_before;

/* Purple Heart (trinket 5), champion loot, Entity_NPC::deathspawn_champion (Switch RNG::Random(3) returns to
 * 0x10bbb8; REP+ 0x6bf44f..0x6bf4c5): in hard mode 1.7.9b drops the loot when Random(3) == 0; REP+ drops it when
 * Random(100) < chance, 33, at least 50 / 66 / 75 with Purple Heart multiplier 1 / 2 / 3+. */
#define RP_SITE_CHAMP_LOOT 0x10bbb8u
/* Keeper's Bargain (171), REP+ shop price 0x815665 / 0x8156de: 1.7.9b prices a devil deal in coins when
 * RNG(seed, 77).Random(2) == 0 for a Keeper's Bargain owner (Switch Room::GetShopItemPrice, returns to
 * 0x4866a4); REP+ always does at multiplier 2+ (it reads the first owner's, here PlayerManager's). */
#define RP_SITE_KEEPER_DEAL 0x4866a4u
/* Used Diaper (93), REP+ Entity_Player::HasCollectible 0x770ccf: Skatole (9) counts as held when the room's
 * RNG(seed).Random(100) < 33; Switch HasCollectible (Random(100) returns to 0x27d6e4) when <= 14. */
#define RP_SITE_DIAPER 0x27d6e4u
static __attribute__((used)) void *s_rp_pm_mult;
static int rp_pm_trinket_mult(int id) {
    u8 *g = game_ptr();
    if (!g || !s_rp_pm_mult) return 0;
    return ((int (*)(void *, int))s_rp_pm_mult)(g + 0x25c40, id);
}
/* Myosotis (137), REP+ 0x802f32: carries 4/6/8 pickups for multiplier 1/2/3. Switch Room::Update 0x46d9b8..
 * 0x46d9d4 takes w22 = min(pickups kept, 4), then builds RNG(seed, 3) (RNG::RNG returns to 0x46d9ec) and spawns
 * w22 of them (the picks are a partial shuffle with Random(w22..1), positions Random(4) of 4 spots near the door,
 * freed by FindFreePickupSpawnPosition). The kept vector's begin/end are the caller's [sp+0x788]/[sp+0x790]
 * (0x78 bytes each); the RNG::RNG stub sets x22 from this. -1 keeps the game's count. */
#define RP_SITE_MYOSOTIS 0x46d9ecu
static __attribute__((used)) uintptr_t s_rp_myo_site = (uintptr_t)-1;
static __attribute__((used)) void *s_rp_rng_ctor;
HIDDEN __attribute__((used)) int rp_myosotis_c(u8 *csp) {
    if (!RP.on) return -1;
    int m = rp_pm_trinket_mult(137);
    if (m < 2) return -1;
    u8 *b = *(u8 **)(csp + 0x788), *e = *(u8 **)(csp + 0x790);
    if (!b || e < b) return -1;
    long n = (e - b) / 0x78, cap = m >= 3 ? 8 : 6;
    return (int)(n < cap ? n : cap);
}


/* Devil deal chance, golden Number Magnet (174) / Rosary Bead (7), REP+ Room::GetDevilRoomChance 0x7f983c..0x7f98e7:
 * with the multiplier m of the first owner, Number Magnet adds 0.10 / 0.15 / 0.20 for m 1 / 2 / 3 (none above),
 * Rosary Bead 0.05 / 0.10 for m 2 / 3. Switch (0x466568) adds 0.10 for any Number Magnet and nothing for Rosary
 * Bead; the sum is s9 when PlayerManager::FirstCollectibleOwner(669) returns to 0x4665a0, so the difference
 * is added to s9 there. */
#define RP_SITE_DEAL_CHANCE 0x4665a0u
static __attribute__((used)) uintptr_t s_rp_deal_site = (uintptr_t)-1;
static __attribute__((used)) void *s_rp_first_coll;
static void *(*s_rp_first_trinket)(void *, int, void **, bool);
/* Planetariums on Womb/Utero (raw stage 9-10), REP+ planetarium chance 0x74df40: 1.7.9b gives 0 past stage 8
 * (and past 6 without Telescope Lens); REP+ allows stages 9-10 when the Telescope Lens owner has a golden one
 * or Mom's Box (0x74dfae..0x74e004). Switch Level::GetPlanetariumChance (0x3d58bc) keeps the raw stage in w23
 * (callee-saved) only for the two compares right after FirstTrinketOwner(152) returns (0x3d591c): the hook
 * gives back 8 there for such a stage, so the rest of the function runs with the real stage (w22). */
#define RP_SITE_PLANET_LENS 0x3d591cu
static __attribute__((used)) uintptr_t s_rp_planet_site = (uintptr_t)-1;
static __attribute__((used)) void *s_rp_first_trinket_got;
static u32 s_rp_planet_calls;
HIDDEN __attribute__((used)) u64 rp_planet_stage_c(void *owner, u64 stage) {
    if (!RP.on || !owner || stage < 9 || stage > 10) return stage;
    if (RP.mult(owner, 152) < 2 && !RP.has(owner, 439, false)) return stage;
    s_rp_planet_calls++;
    return 8;
}
/* golden Adoption Papers (184), REP+ shop price 0x816664: a shop slot of type 18 (Adoption Papers' familiar) costs
 * 2/3 of its price in 1.7.9b and REP+ below multiplier 2, and a flat 6 at 2+. Switch Room::TryGetShopDiscount
 * (room x20, slot w19, price w21, all callee-saved) does price * 2 / 3 right after FirstTrinketOwner(184) returns
 * to 0x486a98: the hook sets the price to 9 there (9 * 2 / 3 = 6). */
#define RP_SITE_ADOPT 0x486a98u
static __attribute__((used)) uintptr_t s_rp_adopt_site = (uintptr_t)-1;
HIDDEN __attribute__((used)) u64 rp_adopt_price_c(void *owner, u8 *room, u64 slot, u64 price) {
    if (!RP.on || !owner || !room || RP.mult(owner, 184) < 2) return price;
    u8 *types = *(u8 **)(room + 8);
    if (!types || *(uint16_t *)(types + (int32_t)slot * 2 + 0xd8) != 0x12) return price;
    RP.adopt_calls++;
    return 9;
}
/* Keeper's Bargain at multiplier 3+, REP+ shop price 0x816705: in a devil room (type 14) one deal in 3 (RNG of the
 * room seed and the slot, Random(3) == 0) gets one more Steam Sale (the price is divided by count + 1). Switch
 * Room::TryGetShopDiscount takes the count from PlayerManager::GetNumCollectibles(64) (returns to 0x486ae4;
 * room x20, slot w19 callee-saved); the hook adds 1 there. The 1-in-3 pick is a hash of the room's spawn seed
 * (descriptor +0x68) and the slot, so it stays the same for every price update. */
#define RP_SITE_KEEPER_SALE 0x486ae4u
static __attribute__((used)) uintptr_t s_rp_sale_site = (uintptr_t)-1;
static __attribute__((used)) void *s_rp_num_coll_got;
HIDDEN __attribute__((used)) u64 rp_keeper_sale_c(u64 count, u8 *room, u64 slot) {
    if (!RP.on || !room || *(int32_t *)(room + 0x10) != 14 || rp_pm_trinket_mult(171) < 3) return count;
    u8 *desc = *(u8 **)(room + 8);
    if (!desc) return count;
    u32 h = (*(u32 *)(desc + 0x68) ^ ((u32)slot + 1u) * 0x9e3779b9u) * 0x85ebca6bu;
    h ^= h >> 13;
    if (h % 3u != 0) return count;
    RP.sale_calls++;
    return (u32)count + 1u;
}
/* Necromancer powered Necronomicon icon, REP+ 0x8428de..0x84295f: the HUD active slot of item 35 loads
 * gfx/ui/hud_NecronomiconPowered.png when the Necromancer counter is 3+ and the slot is powered. Switch
 * HUD::PlayerHUD::Update builds the slot's image path and calls Manager::LoadImage (returns to 0x3a1274) with
 * the player in x20 and the item in w26 (both callee-saved): the hook swaps the path there. The Lua side
 * calls HUD::InvalidateActiveItem when the form turns on or off so the slot reloads its image. */
#define RP_SITE_HUD_ACTIVE_IMG 0x3a1274u
static __attribute__((used)) uintptr_t s_rp_hud_img_site = (uintptr_t)-1;
static __attribute__((used)) void *s_rp_load_image_got;
HIDDEN __attribute__((used)) const char *rp_hud_img_c(const char *path, void *p, u64 item) {
    if (!RP.on || !p || (u32)item != 35 || !RP.mult) return path;
    if (!RP.has(p, 35, false) || !RP.has(p, 262, false) || RP.mult(p, 48) < 1) return path;
    for (int s = 0; s < 3; s++)  /* only a powered slot (VarData 1, see rp_hk_max_charge) */
        if (*(int32_t *)((u8 *)p + 0x1964 + s * 0x1c) == 35 && *(int32_t *)((u8 *)p + 0x1964 + s * 0x1c + 0x18) == 1)
            return "gfx/ui/hud_NecronomiconPowered.png";
    return path;
}
/* Modeling Clay (166) and Error (75) in the Extra HUD, REP+ HistoryHUD recompute 0x83b74a..0x83b7e4: a trinket
 * entry of the player's history shows what the trinket is in this room: Error the trinket it copies (one
 * xorshift 2/7/25 step of the room's spawn seed, % 189 + 1, golden bit kept; the same roll as
 * GetTrinketMultiplier 0x29b3dc), Modeling Clay the collectible it gives (REP+ Player+0x1fb8, Switch
 * Player+0x2730 set by TriggerNewRoom_TemporaryEffects 0x2f4f80). Switch HUD::HistoryHUD::Recompute(int, bool)
 * calls ItemConfig::GetTrinket for such an entry (returns to 0x3b0a04) with the player in x23 (callee-saved);
 * the hook gives back the other item's config there. The Lua side invalidates the history on every new room. */
#define RP_SITE_HISTORY_TRINKET 0x3b0a04u
static __attribute__((used)) uintptr_t s_rp_hist_site = (uintptr_t)-1;
static __attribute__((used)) void *s_rp_get_trinket_got;
static void *(*s_rp_get_collectible)(void *, int);
HIDDEN __attribute__((used)) void *rp_hist_item_c(void *cfg, u64 id, u8 *p) {
    void *(*orig)(void *, int) = (void *(*)(void *, int))s_rp_get_trinket_got;
    u32 t = (u32)id & 0x7fff;
    if (!RP.on || !p || (t != 75 && t != 166)) return orig(cfg, (int)id);
    if (t == 166) {
        int item = *(int32_t *)(p + 0x2730);
        void *c = item > 0 && s_rp_get_collectible ? s_rp_get_collectible(cfg, item) : NULL;
        return c ? c : orig(cfg, (int)id);
    }
    u8 *r = room_ptr(), *desc = r ? *(u8 **)(r + 8) : NULL;
    u32 x = desc ? *(u32 *)(desc + 0x68) : 0;
    if (!x) return orig(cfg, (int)id);
    x ^= x >> 2; x ^= x << 7; x ^= x >> 25;
    void *c = orig(cfg, (int)((x % 189u + 1u) | ((u32)id & 0x8000u)));
    return c ? c : orig(cfg, (int)id);
}
/* Doctor's Remote (47) with Car Battery, REP+ Weapon_Rockets::Fire 0xa0950f: for a weapon with no slot (the
 * Doctor's Remote one: sign bit of its slot index, Switch Weapon+0x3c, REP+ Weapon+0x34) the multishot count is
 * doubled when the owner has Car Battery, so a second missile follows the first. Switch Weapon_Rockets::Fire
 * calls Weapon::GetMultiShotParams (returns to 0x4bac50) with the weapon in x19 and its owner player in x26;
 * the params come back through x8 with the count as an s16 at +0. */
#define RP_SITE_ROCKET_PARAMS 0x4bac50u
static __attribute__((used)) uintptr_t s_rp_rocket_site = (uintptr_t)-1;
static __attribute__((used)) void *s_rp_multishot_got;
HIDDEN __attribute__((used)) void rp_rocket_params_c(void *w, void *p, int16_t *out) {
    if (!RP.on || !w || !p || !out) return;
    if (*(s32 *)((u8 *)w + 0x3c) >= 0 || !RP.has(p, 356, false)) return;
    out[0] = (int16_t)(out[0] * 2);
    RP.rocket_calls++;
}
HIDDEN __attribute__((used)) float rp_deal_bonus_c(void) {
    u8 *g = game_ptr();
    if (!RP.on || !g || !s_rp_first_trinket) return 0.0f;
    float d = 0.0f;
    void *p = s_rp_first_trinket(g + 0x25c40, 174, NULL, true);
    if (p) {
        int m = RP.mult(p, 174);
        d += m == 2 ? 0.05f : m == 3 ? 0.10f : m > 3 ? -0.10f : 0.0f;
    }
    p = s_rp_first_trinket(g + 0x25c40, 7, NULL, true);
    if (p) {
        int m = RP.mult(p, 7);
        d += m == 2 ? 0.05f : m == 3 ? 0.10f : 0.0f;
    }
    if (d != 0.0f) RP.deal_calls++;
    return d;
}


/* Golden Broken Magnet (6) / Super Magnet (68), REP+ 0x6edaab / 0x6edb38: with multiplier 2+ the player pulls
 * pickups as with Magneto (53). Switch Entity_Pickup::update_magnet_effect asks
 * PlayerManager::FirstCollectibleOwner(53) first (returns to 0x27037c): with no Magneto owner, a player with a
 * golden one of them is answered. */
#define RP_SITE_MAGNETO 0x27037cu
HIDDEN __attribute__((used)) void *rp_first_coll_c(void *pm, int id, void **rng, bool b, uintptr_t lr) {
    void *r = ((void *(*)(void *, int, void **, bool))s_rp_first_coll)(pm, id, rng, b);
    if (!r && RP.on && id == 53 && lr == RP.base + RP_SITE_MAGNETO) {
        for (int i = 0; i < 8; i++) {
            void *p = api_player(i);
            if (!p) break;
            if (RP.mult(p, 6) > 1 || RP.mult(p, 68) > 1) { RP.magnet_calls++; return p; }
        }
    }
    /* Golden crowns, REP+ 0x7065dc / 0x73e66c / 0x7f3f59: Wicked Crown (161) in Sheol (stage 10 type 0),
     * Holy Crown (155) in the Cathedral (10 type 1), Bloody Crown (111) in the Womb (7, 8) with multiplier
     * > 1 act as More Options there (FirstCollectibleOwner(414) returns to 0x3d6db4 Level::load_room,
     * 0x45d1fc Room::Init, 0x42a460 NightmareScene::Show). */
    if (!r && RP.on && id == 414 && (lr == RP.base + 0x3d6db4u || lr == RP.base + 0x45d1fcu ||
                                     lr == RP.base + 0x42a460u)) {
        u8 *g = game_ptr();
        int t = -1;
        if (g) {
            s32 st = *(s32 *)(g + kGame_Stage), ty = *(s32 *)(g + 4);
            if (st == 10 && ty == 0) t = 161;
            else if (st == 10 && ty == 1) t = 155;
            else if (st == 7 || st == 8) t = 111;
        }
        for (int i = 0; t > 0 && i < 8; i++) {
            void *p = api_player(i);
            if (!p) break;
            if (RP.mult(p, t) > 1) { RP.crown_calls++; return p; }
        }
    }
    return r;
}
#define RP_SITE_VOID_PORTAL 0x459190u
/* Golden Key at a Key Master, Golden Bomb at a Bomb Bum (REP+ 10). Entity_Slot::handle_collision rolls
 * RNG::Random(100) on the slot's RNG (+0x3d8) after taking the key or bomb: Key Master pays when < 35
 * (Switch 0x32f9ac), Bomb Bum picks prize 1/2/3 for < 25/35/40 (0x32f9fc). REP+ 0x66336f..0x663504: with a
 * golden key the Key Master always pays, takes the golden key (Entity_Player::RemoveGoldenKey) and marks the
 * slot; with a golden bomb the Bomb Bum gives prize 3 with no roll and takes the golden bomb. The marked Key
 * Master's prize (Slot::Update) takes the Random(8) == 0 branch (0x32abd0, REP+ 0x66145b) and skips its
 * Paper Clip roll (0x32bcd4, REP+ 0x6616b9), which clears the mark. The slot's golden key/bomb sprite
 * layers need REP+'s own slot anm2, which is not shipped. */
#define RP_SITE_KM_PAY 0x32f9b0u
#define RP_SITE_BB_PAY 0x32fa00u
#define RP_SITE_KM_PRIZE 0x32abd4u
#define RP_SITE_KM_CLIP 0x32bcd8u
#define RP_PL_GOLDEN_KEY 0x16d8
#define RP_PL_GOLDEN_BOMB 0x16d9
static __attribute__((used)) void *s_rp_rm_gold_key, *s_rp_rm_gold_bomb;
/* REP+ 0x66337c..0x6634a4: on every payment the slot sprite's layer 2 (Coin) is shown without and layer 3
 * (Gold, only in REP+'s 006.007_Key Master.anm2 / 006.009_Bomb Bum.anm2, shipped in the mod) with the
 * golden key / bomb. ANM2::GetLayer -> LayerState, visible byte +0x7c (HUD::ShowItemText 0x3ab47c). */
static void *(*s_rp_anm_get_layer)(void *, int);
/* ANM2 layer count u32 +0xb0 (sprite.c kAnm_LayerCount) */
static void rp_slot_gold_layers(void *slot, int gold) {
    u8 *anm = (u8 *)slot + kEnt_Sprite;
    if (!s_rp_anm_get_layer || *(u32 *)(anm + 0xb0) < 4) return;
    u8 *l2 = (u8 *)s_rp_anm_get_layer(anm, 2), *l3 = (u8 *)s_rp_anm_get_layer(anm, 3);
    if (l2) l2[0x7c] = !gold;
    if (l3) l3[0x7c] = gold != 0;
}
static void *s_rp_gold_slot[8];
static int rp_gold_slot(void *slot, int set, int clear) {
    for (int i = 0; i < 8; i++)
        if (s_rp_gold_slot[i] == slot) {
            if (clear) s_rp_gold_slot[i] = 0;
            return 1;
        }
    if (set)
        for (int i = 0; i < 8; i++)
            if (!s_rp_gold_slot[i]) { s_rp_gold_slot[i] = slot; break; }
    return 0;
}
static int rp_is_player(void *e) {
    return e && *(int32_t *)((u8 *)e + 0x38) == 1 && *(int32_t *)((u8 *)e + 0x3c) == 0;
}
/* Jar of Wisps (685), REP+ UseActiveItem 0x5c3910..0x5c39f8: each wisp is, with RandomFloat < 0.67 (0xbaa3ac), a
 * random entry (index RandomInt(n - 1) + 1) of the list of active items that have a wisp (ItemConfig type 3, wisp
 * config count >= 1), else the Book of Virtues wisp (584). 1.7.9b (Switch 0x314944) rolls any collectible id
 * (RNG::Random(n - 1) + 1) and falls back to 584 unless it is an active item with a wisp, so almost all wisps are
 * plain. The hook returns id - 1 for the caller's + 1; the caller's own checks (valid, active, wisp, Book of
 * Belial -> 292) stay. */
#define RP_SITE_JAR_WISP 0x314948u
typedef float (*RpRngFloatFn)(void *);
static RpRngFloatFn s_rp_rng_random;
static u32 rp_jar_wisp(void *rng, u32 max) {
    u8 *mgr = manager_ptr();
    void ***v = mgr ? (void ***)(mgr + 0x36538) : NULL;
    if (!v || !s_rp_rng_random || s_rp_rng_random(rng) >= 0.67f) { s_rp_random(rng, max); return 583u; }
    static uint16_t list[1024];
    u32 n = 0, cnt = (u32)(v[1] - v[0]);
    for (u32 i = 0; i < cnt && n < 1024; i++) {
        u8 *it = (u8 *)v[0][i];
        if (!it || *(u32 *)it != 3) continue;
        u8 *w = *(u8 **)(it + 0xd8);
        if (w && *(int16_t *)(w + 0x194) >= 1) list[n++] = (uint16_t)*(u32 *)(it + 4);
    }
    if (n < 2) { s_rp_random(rng, max); return 583u; }
    RP.jar_calls++;
    return (u32)list[s_rp_random(rng, n - 1) + 1] - 1u;
}
HIDDEN __attribute__((used)) u32 rp_random_c(void *rng, u32 max, uintptr_t lr, void *player, void **sp) {
    if (RP.on) {
        uintptr_t o = lr - RP.base;
        if (o == RP_SITE_JAR_WISP) return rp_jar_wisp(rng, max);
        /* Keeper / T.Keeper turn heart pickups into blue flies (Entity_Pickup::Update 0x26cff4: picks one of
         * them with RNG::Random(n) from a list at caller sp+0x48, x19 = the pickup, then Remove + AddBlueFlies;
         * a null pick skips all that). REP+ 0x6e436d leaves Gold Hearts (subtype 7, +0x40 here) alone, so
         * they can be touched and break into coins (repplus.lua). */
        if (o == 0x26d018u && player && *(const int32_t *)((u8 *)player + 0x40) == 7 && sp) {
            void **list = (void **)sp[0x48 / 8];
            if (list) { list[0] = NULL; return 0; }
        }
        /* Void portal after The Lamb, ??? and Mega Satan (REP+ 10): 1.7.9b Room::SpawnClearAward rolls
         * RNG::Random(5) == 0 (20%) after their big chest (0x459184), REP+ 0x7fcd95 spawns it with no roll; the
         * other checks (no trophy, Void reachable: Level::IsStageAvailable(12)) stay as they are. */
        if (o == RP_SITE_VOID_PORTAL && max == 5) {
            RP.void_calls++;
            return 0u;
        }
        if ((o == RP_SITE_KM_PAY || o == RP_SITE_BB_PAY) && max == 100 && rp_is_player(player))
            rp_slot_gold_layers((u8 *)rng - 0x3d8, *((u8 *)player + (o == RP_SITE_KM_PAY ? RP_PL_GOLDEN_KEY : RP_PL_GOLDEN_BOMB)));
        if (o == RP_SITE_KM_PAY && max == 100 && rp_is_player(player) && s_rp_rm_gold_key &&
            *((u8 *)player + RP_PL_GOLDEN_KEY)) {
            s_rp_random(rng, max);
            ((void (*)(void *))s_rp_rm_gold_key)(player);
            rp_gold_slot((u8 *)rng - 0x3d8, 1, 0);
            RP.gold_calls++;
            return 0u;
        }
        if (o == RP_SITE_BB_PAY && max == 100 && rp_is_player(player) && s_rp_rm_gold_bomb &&
            *((u8 *)player + RP_PL_GOLDEN_BOMB)) {
            ((void (*)(void *))s_rp_rm_gold_bomb)(player);
            RP.gold_calls++;
            return 36u;
        }
        if (o == RP_SITE_KM_PRIZE && max == 8 && rp_gold_slot((u8 *)rng - 0x3d8, 0, 0)) {
            s_rp_random(rng, max);
            return 0u;
        }
        if (o == RP_SITE_KM_CLIP && max == 2 && rp_gold_slot((u8 *)rng - 0x3d8, 0, 1)) return 1u;
        if (o == RP_SITE_CHAMP_LOOT && max == 3) {
            int m = rp_pm_trinket_mult(5);
            u32 chance = m >= 3 ? 75u : m == 2 ? 66u : m == 1 ? 50u : 33u;
            RP.ph_calls++;
            return s_rp_random(rng, 100) < chance ? 0u : 1u;
        }
        if (o == RP_SITE_DIAPER && max == 100) return s_rp_random(rng, max) < 33u ? 0u : 99u;
        if (o == RP_SITE_KEEPER_DEAL && max == 2 && rp_pm_trinket_mult(171) >= 2) {
            s_rp_random(rng, max);
            return 0u;
        }
        if (o == RP_SITE_TONSIL_INIT && max == 9) return s_rp_random(rng, 7) - 6u;
        if (o == RP_SITE_TONSIL_RESET && max == 9) {
            int16_t *num = (int16_t *)((u8 *)player + RP_PL_TONSIL_NUM);
            /* 1.7.9b added one only while the count was <= 1; REP+ adds one while it is below the cap */
            if (player == s_rp_tonsil_p && s_rp_tonsil_before > 1 && *num < RP.mult(player, 97) + 1) {
                (*num)++;
                *(u32 *)((u8 *)player + RP_PL_CACHE_FLAGS) |= 0x200;
                if (s_rp_eval_items) s_rp_eval_items(player);
            }
            s_rp_tonsil_p = NULL;
            return s_rp_random(rng, 7) - 6u;
        }
        if ((o == RP_SITE_BAGLUNCH || o == RP_SITE_WISHBONE) && max == 50) {
            int m = RP.mult(player, o == RP_SITE_BAGLUNCH ? 105 : 104);
            return s_rp_random(rng, m == 2 ? 10u : m == 3 ? 7u : 20u);
        }
    }
    return s_rp_random(rng, max);
}

/* Broken Ankh (28), REP+ Entity_Player::TriggerDeath 0x7a1e23: revive chance 22/33/50% by multiplier 1/2/3+
 * instead of 1.7.9b's 20% (Switch tests trinket RNG 28's seed % 5 == 0 inline after GetTrinketMultiplier
 * returns to 0x2e53bc; the hook rolls REP+'s chance and moves the seed to the matching residue). */
#define RP_SITE_BROKEN_ANKH 0x2e53bcu
static u32 (*s_rp_free_random_fn)(u32);
/* golden Dim Bulb (101), REP+ EvaluateItems 0x76493c..0x76fa93: while the primary active item has no charge
 * (Switch [sp+0x68] = Player+0x1968 == 0) REP+ adds each bonus times the multiplier, 1.7.9b once. The Switch
 * adds follow GetTrinketMultiplier(101) returning to these sites (caller sp, x20, x21, x23 callee-saved);
 * the hook adds the other (multiplier - 1) bonuses. */
#define RP_SITE_DIM_TEARS 0x281614u   /* tears-up sum [sp+0x190] +0.5 */
#define RP_SITE_DIM_DMG 0x282574u     /* damage-up sum [x23] +1.5 */
#define RP_SITE_DIM_SS 0x283ec4u      /* shot speed [x20] +0.3 */
#define RP_SITE_DIM_RANGE 0x284720u   /* range [x21] +60 */
#define RP_SITE_DIM_SPEED 0x285350u   /* speed [x20] +0.5 */
#define RP_SITE_DIM_LUCK 0x28b138u    /* luck Player+0x1950 +2 */
static void rp_dim_bulb(void *p, int m, uintptr_t o, uintptr_t sp, float *x20, float *x21, float *x23) {
    if (m < 2 || *(u32 *)(sp + 0x68) != 1) return;
    float k = (float)(m - 1);
    if (o == RP_SITE_DIM_TEARS) *(float *)(sp + RP_SP_TEARS_UP) += 0.5f * k;
    else if (o == RP_SITE_DIM_DMG && x23) *x23 += 1.5f * k;
    else if (o == RP_SITE_DIM_SS && x20) *x20 += 0.3f * k;
    else if (o == RP_SITE_DIM_RANGE && x21) *x21 += 60.0f * k;
    else if (o == RP_SITE_DIM_SPEED && x20) *x20 += 0.5f * k;
    else if (o == RP_SITE_DIM_LUCK) *(float *)((u8 *)p + 0x1950) += 2.0f * k;
    else return;
    RP.dim_calls++;
}
HIDDEN __attribute__((used)) int rp_trinket_mult_c(void *p, int id, uintptr_t lr, uintptr_t sp, float *x20, float *x21,
                                                    float *x23) {
    int r = s_rp_mult_got(p, id);
    /* Mom's Toenail (16): REP+ has no timed stomp (1.7.9b PC 0x75d1a1 IsFrameIntervalOf(600 / multiplier), not in
     * the REP+ exe; wiki "Removed in Repentance+"). Switch Entity_Player::update_effects asks the multiplier
     * first at 0x2cdd40 (returns to 0x2cdd44) and skips the stomp when it is < 1. The drop stomp is repplus.lua. */
    if (RP.on && id == 16 && lr == RP.base + 0x2cdd44u) return 0;
    if (RP.on && id == 101 && r >= 2) rp_dim_bulb(p, r, lr - RP.base, sp, x20, x21, x23);
    /* Black Feather (80), REP+ EvaluateItems 0x766872..0x766925 (patch 1.9.7.4): The Mark (79) and The Pact (80)
     * count as evil items too. Switch adds (evil count + Black Lipstick + Daemon's Tail) * 0.5 * multiplier to
     * the damage-up sum (Player+0x1844) after GetTrinketMultiplier(80) returns to 0x28306c; add theirs first */
    if (RP.on && id == 80 && r > 0 && lr == RP.base + 0x28306cu) {
        int n = RP.num(p, 79, false) + RP.num(p, 80, false);
        if (n > 0) *(float *)((u8 *)p + 0x1844) += 0.5f * (float)n * (float)r;
    }
    if (RP.on && r > 0 && id == 28 && lr == RP.base + RP_SITE_BROKEN_ANKH && s_rp_free_random_fn) {
        u8 *rngs = *(u8 **)((u8 *)p + 0x1bc8), *rngs_end = *(u8 **)((u8 *)p + 0x1bd0);
        if (rngs && rngs_end - rngs > 0x1c0) {
            u32 *seed = (u32 *)(rngs + 0x1c0);
            int chance = r == 1 ? 22 : r == 2 ? 33 : 50;
            bool win = s_rp_free_random_fn(100) < (u32)chance;
            u32 v = *seed;
            if (win) v = v - v % 5u, v = v ? v : 5u;
            else if (v % 5u == 0) v += 1u;
            *seed = v;
            RP.skull_calls++;
        }
    }
    if (RP.on && lr == RP.base + RP_SITE_TONSIL_MULT) {
        s_rp_tonsil_p = p;
        s_rp_tonsil_before = *(int16_t *)((u8 *)p + RP_PL_TONSIL_NUM);
    }
    return r;
}

/* D4 / D100 / Dice Room / Missing No. (Entity_Player::RerollAllCollectibles, Switch calls CanRerollCollectible at
 * 0x31cb24 and 0x31cb58): REP+'s CanRerollCollectible(item, clicker) (0x7ce620, flag 0 there, 1 for Clicker) also
 * keeps the character's own starting passives: Cain Lucky Foot, Eve Dead Bird / Whore of Babylon, Samson Bloody
 * Lust, Lazarus Anemic, The Lost Holy Mantle, Lilith Incubus / Cambion Conception, Bethany Book of Virtues. */
typedef bool (*RpCanRerollFn)(void *, int);
static RpCanRerollFn s_rp_can_reroll;
/* Clicker (482) in UseActiveItem: 1.7.9b removes the newest rerollable collectible (walks the item history back,
 * CanRerollCollectible at 0x317548, the first yes is removed); REP+ 1.9.7.11 does it only half the time. One roll
 * per use: a "keep" answers no for the rest of that walk (same player, within 10 ms). */
static u32 (*s_rp_clicker_u32)(void);
static void *s_rp_clicker_p;
static s64 s_rp_clicker_tick;
HIDDEN __attribute__((used)) bool rp_can_reroll_c(void *p, int id, uintptr_t lr) {
    bool r = s_rp_can_reroll(p, id);
    if (r && RP.on && lr == RP.base + 0x31754cu && s_rp_clicker_u32) {
        s64 now = nnosGetSystemTick(), f = nnosGetSystemTickFrequency();
        if (p == s_rp_clicker_p && f && (now - s_rp_clicker_tick) < f / 100) return false;
        if (s_rp_clicker_u32() & 1) { s_rp_clicker_p = p; s_rp_clicker_tick = now; return false; }
        s_rp_clicker_p = NULL;
        return true;
    }
    if (!r || !RP.on || (lr != RP.base + 0x31cb28u && lr != RP.base + 0x31cb5cu)) return r;
    switch (*(int32_t *)((u8 *)p + kPlayer_Type)) {
    case 2: return id != 46;
    case 5: return id != 117 && id != 122;
    case 6: return id != 157;
    case 8: return id != 214;
    case 10: return id != 313;
    case 13: return id != 360 && id != 412;
    case 18: return id != 584;
    default: return r;
    }
}

HIDDEN __attribute__((used)) bool rp_remove_trinket_c(void *p, int id, uintptr_t lr) {
    if (RP.on && lr == RP.base + RP_SITE_TONSIL_REMOVE && id == 97 &&
        *(int16_t *)((u8 *)p + RP_PL_TONSIL_NUM) < RP.mult(p, 97) + 1)
        return false;
    return s_rp_remove_trinket(p, id);
}

/* Super Ball (trinket 99), Entity_Player::GetTearHitParams (Switch 0x2aed84; REP+ 0x7c16fe): the bounce
 * chance is 1/n with n = 10 (5 with multiplier 2, 3 with multiplier 3) minus int(luck*0.5), always at n <= 0
 * (REP+), instead of 1/10. 1.7.9b's roll is IsaacRepentance::Random(10) (returns to 0x2aeda0, 0 = bounce),
 * player in x20. */
#define RP_SITE_SUPERBALL 0x2aeda0u
typedef u32 (*RpFreeRandFn)(u32);
static RpFreeRandFn s_rp_free_random;

/* Golden tear trinkets in the same function (REP+ 0x7bfc65..0x7c1967). 1.7.9b rolls Random(n), 0 = the effect:
 * Pinky Eye (30, LR 0x2ad8f8) and Push Pin (31, 0x2ada08) n = max(10 - int(luck*0.5), 1); REP+ rolls a percent:
 * < 10 / 20 / 33 (multiplier 1 / 2 / 3+) + int(luck*5). Chewed Pen (130, 0x2adc88) n = max(10 / 5 / 3 -
 * int(luck*0.5), 1). Black Tooth (95, 0x2aee78) n = 33 / 16 / 11 - int(luck), Jaw Breaker (129, 0x2aef30)
 * n = 10 / 5 / 3 - int(luck), always at n <= 0 (as 1.7.9b's Random(0)). */
#define RP_SITE_PINKY 0x2ad8f8u
#define RP_SITE_PUSHPIN 0x2ada08u
#define RP_SITE_CHEWED 0x2adc88u
#define RP_SITE_BLACKTOOTH 0x2aee78u
#define RP_SITE_JAWBREAKER 0x2aef30u
static int rp_tear_roll(u32 off, void *player, u32 *out) {
    float luck = *(float *)((u8 *)player + kPlayer_Luck);
    int id = off == RP_SITE_PINKY ? 30 : off == RP_SITE_PUSHPIN ? 31 : off == RP_SITE_CHEWED ? 130
           : off == RP_SITE_BLACKTOOTH ? 95 : off == RP_SITE_JAWBREAKER ? 129 : 0;
    if (!id) return 0;
    int m = RP.mult(player, id);
    if (m < 1) return 0;
    RP.tear_calls++;
    if (id == 30 || id == 31) {
        int p = (m == 2 ? 20 : m >= 3 ? 33 : 10) + (int)(luck * 5.0f);
        *out = (int)s_rp_free_random(100) < p ? 0u : 1u;
        return 1;
    }
    int n;
    if (id == 130) {
        n = (m == 2 ? 5 : m >= 3 ? 3 : 10) - (int)(luck * 0.5f);
        if (n < 1) n = 1;
    } else {
        n = (id == 95 ? (m == 2 ? 16 : m >= 3 ? 11 : 33) : (m == 2 ? 5 : m >= 3 ? 3 : 10)) - (int)luck;
        if (n <= 0) { *out = 0; return 1; }
    }
    *out = s_rp_free_random((u32)n) == 0 ? 0u : 1u;
    return 1;
}

/* Red Patch (40), REP+ Entity_Player::TakeDamage 0x774bf5: the rage chance is Random(100) < base + int(luck*5)
 * (greed modes: luck*2.5), base 20/33/50 by multiplier 1/2/3+ (greed 10/20/30), instead of 1.7.9b's
 * Random(max(5 - int(luck*s8), 1)) == 0 (Switch: Random returns to 0x2a518c, player in x19). */
#define RP_SITE_RED_PATCH 0x2a518cu
/* God's Flesh (398), REP+ Entity_Player::GetTearHitParams 0x7c1503: shrinking tears when Random() % 5 == 0 (20%)
 * instead of 1.7.9b's Random(10) == 0 (Switch: returns to 0x2aebd0). */
#define RP_SITE_GODS_FLESH 0x2aebd0u
/* Infamy (242), Entity_Projectile::handle_collision (Switch Random(2) returns to 0x32818c; REP+ 0x65be49..0x65be7f):
 * 1.7.9b blocks the projectile when Random(2) == 0; REP+ rolls Random(4) in greed and greedier (Game difficulty
 * >= 2) and on a block plays SOUND_INFAMY_DEFLECT (863); the count of blocks tells Lua to play it. */
#define RP_SITE_INFAMY 0x32818cu
/* Stone Eye (201, Entity_NPC::ai_stone_eye): 1.7.9b starts its laser on any frame with IsaacRepentance::Random(3)
 * == 0 (returns to 0x1ad2d8, x19 = the NPC) from the moment it wakes; REP+ 1.9.7.4 has it turn a full circle first
 * (it turns 2 degrees a frame, 180 frames), so it cannot fire straight at a player entering from the bottom door. */
static int (*s_rp_ent_frames)(void *);
HIDDEN __attribute__((used)) u32 rp_free_random_c(u32 max, uintptr_t lr, void *player, u8 *x19) {
    if (RP.on && max == 3 && lr == RP.base + 0x1ad2d8u && x19 && s_rp_ent_frames && s_rp_ent_frames(x19) < 180)
        return 1;
    if (RP.on && max == 2 && lr == RP.base + RP_SITE_INFAMY) {
        u8 *g = game_ptr();
        u32 r = s_rp_free_random(g && *(u32 *)(g + 0x2f0028) >= 2 ? 4 : 2);
        if (r == 0) RP.infamy_calls++;
        return r;
    }
    if (RP.on && x19 && lr == RP.base + RP_SITE_RED_PATCH && RP.mult) {
        u8 *g = game_ptr();
        bool greed = g && *(u32 *)(g + 0x2f0028) >= 2;
        int m = RP.mult(x19, 40);
        int base = m >= 3 ? (greed ? 30 : 50) : m == 2 ? (greed ? 20 : 33) : (greed ? 10 : 20);
        float luck = *(float *)(x19 + kPlayer_Luck);
        RP.tear_calls++;
        return s_rp_free_random(100) < (u32)(base + (int)(luck * (greed ? 2.5f : 5.0f))) ? 0u : 1u;
    }
    if (RP.on && max == 10 && lr == RP.base + RP_SITE_GODS_FLESH) return s_rp_free_random(5);
    if (RP.on && player && lr - RP.base >= RP_SITE_PINKY && lr - RP.base <= RP_SITE_JAWBREAKER) {
        u32 r;
        if (rp_tear_roll((u32)(lr - RP.base), player, &r)) return r;
    }
    if (RP.on && max == 10 && lr == RP.base + RP_SITE_SUPERBALL && player) {
        int m = RP.mult(player, 99);
        int n = (m == 2 ? 5 : m == 3 ? 3 : 10) - (int)(*(float *)((u8 *)player + kPlayer_Luck) * 0.5f);
        RP.superball_calls++;
        if (n <= 0) return 0;
        return s_rp_free_random((u32)n) == 0 ? 0u : 1u;
    }
    return s_rp_free_random(max);
}

/* Blessed Penny (trinket 131), Entity_Pickup::handle_collision (Switch 0x2737bc; REP+ 0x6eae42): 1.7.9b spawns a half soul heart
 * when RNG::Random() >= 0.8334^value (s9, value = the coin's worth); REP+ spawns it when Random() <= a chance
 * from a table by trinket multiplier (1: 0.17/0.30/0.60/0.83, 2: 0.25/0.40/0.70/0.90, 3: 0.30/0.50/0.75/1.0 for
 * worth 1/2/5/10, other worths the first; other multipliers the first row). The worth is read back from s9,
 * the player is at [x29 - 0x18]. */
#define RP_SITE_CURSED_PENNY 0x2737c0u
#define RP_SITE_BURNT_PENNY 0x27359cu
#define RP_SITE_FLAT_PENNY 0x27362cu
#define RP_SITE_BLOODY_PENNY 0x2736c0u

HIDDEN __attribute__((used)) float rp_rng_random_c(void *rng, uintptr_t lr, u8 *fp, float thr) {
    if (RP.on && lr == RP.base + RP_SITE_CURSED_PENNY && fp) {
        void *player = *(void **)(fp - 0x18);
        if (player) {
            static const float kChance[3][4] = {{0.17f, 0.30f, 0.60f, 0.83f}, {0.25f, 0.40f, 0.70f, 0.90f},
                                                {0.30f, 0.50f, 0.75f, 1.0f}};
            static const float kThr[4] = {0.8334f, 0.6946f, 0.4024f, 0.1619f};  // 0.8334^1, ^2, ^5, ^10
            int m = RP.mult(player, 131), row = m == 2 ? 1 : m == 3 ? 2 : 0, col = 0;
            for (int i = 0; i < 4; i++)
                if (thr > kThr[i] - 0.01f && thr < kThr[i] + 0.01f) col = i;
            RP.penny_calls++;
            return s_rp_rng_random(rng) <= kChance[row][col] ? 2.0f : -1.0f;  // >= s9 spawns the heart
        }
    }
    /* Burnt (50), Flat (51), Bloody Penny (49), REP+ 0x6eab6f / 0x6eac4b / 0x6ead1d: the chance threshold
     * (1.7.9b: Random() >= 0.75^worth, Switch LRs below) is lowered by 0.25 per multiplier above 1; the same as
     * adding that to the roll */
    if (RP.on && fp && (lr == RP.base + RP_SITE_BURNT_PENNY || lr == RP.base + RP_SITE_FLAT_PENNY ||
                        lr == RP.base + RP_SITE_BLOODY_PENNY)) {
        void *player = *(void **)(fp - 0x18);
        int id = lr == RP.base + RP_SITE_BURNT_PENNY ? 50 : lr == RP.base + RP_SITE_FLAT_PENNY ? 51 : 49;
        float r = s_rp_rng_random(rng);
        int m = player ? RP.mult(player, id) : 1;
        if (m > 1) { RP.coin_calls++; r += 0.25f * (float)(m - 1); }
        return r;
    }
    return s_rp_rng_random(rng);
}

/* Counterfeit Penny (52), REP+ 0x6eae35: the extra coin is AddCoins(multiplier) instead of AddCoins(1)
 * (Switch Entity_Pickup::handle_collision, AddCoins returns to 0x273788). */
#define RP_SITE_COUNTERFEIT 0x273788u
typedef void (*RpAddCoinsFn)(void *, int);
static RpAddCoinsFn s_rp_add_coins;
HIDDEN __attribute__((used)) void rp_add_coins_c(void *p, int n, uintptr_t lr) {
    if (RP.on && p && n == 1 && lr == RP.base + RP_SITE_COUNTERFEIT) {
        int m = RP.mult(p, 52);
        if (m > 1) { RP.coin_calls++; n = m; }
    }
    s_rp_add_coins(p, n);
}

/* Paper Clip (19), REP+ GridEntity_Lock::TryUnlock 0x711e10 and GridEntity_Door::TryUnlock 0x710db5: with
 * multiplier >= 2 key blocks open without a key, with >= 3 also locked doors (variant 1 or 2), instead of
 * Entity_Player::TryUseKey (Switch: returns to 0x3877e4 in the lock, 0x385428 in the door, door in x19,
 * GridEntity variant at +0xc). */
#define RP_SITE_LOCK_KEY 0x3877e4u
#define RP_SITE_LOCK_KEY_UPD 0x387784u  /* GridEntity_Lock::Update: the player-touch unlock is inlined there */
#define RP_SITE_DOOR_KEY 0x385428u
typedef bool (*RpUseKeyFn)(void *);
static RpUseKeyFn s_rp_use_key;
HIDDEN __attribute__((used)) bool rp_use_key_c(void *p, uintptr_t lr, u8 *x19) {
    if (RP.on && p && RP.mult) {
        if ((lr == RP.base + RP_SITE_LOCK_KEY || lr == RP.base + RP_SITE_LOCK_KEY_UPD) && RP.mult(p, 19) >= 2) { RP.clip_calls++; return true; }
        if (lr == RP.base + RP_SITE_DOOR_KEY && x19) {
            s32 var = *(s32 *)(x19 + 0xc);
            if ((var == 1 || var == 2) && RP.mult(p, 19) >= 3) { RP.clip_calls++; return true; }
        }
    }
    return s_rp_use_key(p);
}

/* Isaac's Head (54), REP+ Entity_Familiar fire 0x639dd6: Isaac's Head (familiar 3.12) tears do the player's
 * damage with multiplier 2 and 1.5x the player's damage with 3+, instead of 3.5. Switch Entity_Familiar::
 * fire_projectiles keeps it in s13 (callee-saved) until HasTrinket(127) returns to 0xb0b00, familiar in x19.
 * The Soul (57), REP+ Entity_Player::EvaluateItems 0x76efbd: min(multiplier, 3) Blue Baby's Souls (3.13)
 * instead of one (Switch check_familiar returns to 0x28a53c). */
/* golden Lost Cork (106), REP+ Entity_Effect::Update 0x5e82f8..0x5e84c3 / 0x5eb319 / 0x5f128d: the player's creep
 * grows by the owner's multiplier m (1..3; none above): x1.65 / 1.9 / 2.15 instead of a flat 1.65, and the size
 * of a small one (< 0.99) +1 / +1.25 / +1.5 instead of +1. Switch Entity_Effect::Update checks
 * HasTrinket(106) for the creeps 22-26, 44-46, 53, 54, 56, 92, 94, 169 on their first update (returns to
 * 0x85db0, size in s8, effect x19, both callee-saved: Scale +0x470 x1.65 when the size is >= 2.1, else size +1
 * below 0.99, else size x1.65) and for 32, 37, 78, 90 (returns to 0x85e18: Scale x1.65). The hook answers
 * false there and does REP+'s step itself. */
#define RP_SITE_CORK_SIZE 0x85db0u
#define RP_SITE_CORK_SCALE 0x85e18u
static __attribute__((used)) uintptr_t s_rp_cork1_site = (uintptr_t)-1, s_rp_cork2_site = (uintptr_t)-1;
static u32 s_rp_cork_calls;
static float rp_cork_mul(int m) { return m == 3 ? 2.15f : m == 2 ? 1.9f : 1.65f; }
HIDDEN __attribute__((used)) float rp_cork1_c(u8 *p, u8 *eff, float size) {
    if (!RP.on || !p || !eff || !RP.mult) return -1.0f;
    int m = RP.mult(p, 106);
    if (m < 2) return -1.0f;
    s_rp_cork_calls++;
    if (m > 3) return size;
    if (size >= 2.1f) { *(float *)(eff + 0x470) *= rp_cork_mul(m); return size; }
    if (size < 0.99f) return size + (m == 3 ? 1.5f : 1.25f);
    return size * rp_cork_mul(m);
}
HIDDEN __attribute__((used)) u64 rp_cork2_c(u8 *p, u8 *eff) {
    if (!RP.on || !p || !eff || !RP.mult) return 0;
    int m = RP.mult(p, 106);
    if (m < 2) return 0;
    s_rp_cork_calls++;
    if (m <= 3) *(float *)(eff + 0x470) *= rp_cork_mul(m);
    return 1;
}
#define RP_SITE_HEAD_DMG 0xb0b00u
#define RP_SITE_SOUL_FAM 0x28a53cu
static __attribute__((used)) uintptr_t s_rp_head_site = (uintptr_t)-1;
static __attribute__((used)) void *s_rp_has_trinket;
HIDDEN __attribute__((used)) float rp_head_dmg_c(u8 *fam) {
    if (!RP.on || !fam || !RP.mult || ENT_I32(fam, kEnt_Variant) != 12) return -1.0f;
    u8 *p = *(u8 **)(fam + 0x400);
    if (!p) return -1.0f;
    int m = RP.mult(p, 54);
    if (m < 2) return -1.0f;
    RP.head_calls++;
    float d = *(float *)(p + kPlayer_Damage);
    return m >= 3 ? d * 1.5f : d;
}
typedef u64 (*RpCheckFamFn)(void *, u32, int, void *, const void *, int);
static RpCheckFamFn s_rp_check_fam;
HIDDEN __attribute__((used)) u64 rp_check_fam_c(void *p, u32 var, int count, void *rng, const void *item, int sub,
                                                 uintptr_t lr) {
    if (RP.on && p && var == 13 && count == 1 && lr == RP.base + RP_SITE_SOUL_FAM && RP.mult) {
        int m = RP.mult(p, 57);
        if (m > 1) { RP.head_calls++; count = m < 3 ? m : 3; }
    }
    return s_rp_check_fam(p, var, count, rng, item, sub);
}

/* Cursed Skull (43), REP+ Entity_Player::TakeDamage 0x7785d5: with multiplier >= 2 the teleport goes to a random
 * room of a special type (shop, treasure, miniboss, secret, super secret, arcade, library, sacrifice,
 * Isaac's, barren, chest, dice, planetarium) through REP+ 0x7ce2b0 instead of Game::MoveToRandomRoom
 * (Switch: returns to 0x2a8774). Not done: the x3 health threshold of 2. */
#define RP_SITE_CURSED_SKULL 0x2a8774u
typedef void (*RpMoveRandFn)(void *, bool, u32, void *);
typedef void (*RpTransFn)(void *, int, int, int, void *, int);
static RpMoveRandFn s_rp_move_random;
static RpTransFn s_rp_transition;
HIDDEN __attribute__((used)) void rp_move_random_c(u8 *g, bool b, u32 seed, void *p, uintptr_t lr) {
    if (RP.on && g && p && lr == RP.base + RP_SITE_CURSED_SKULL && RP.mult && RP.mult(p, 43) >= 2 && s_rp_transition) {
        static const u8 ok[25] = {[2] = 1, [4] = 1, [6] = 1, [7] = 1, [8] = 1, [9] = 1, [11] = 1, [12] = 1, [13] = 1,
                                  [18] = 1, [19] = 1, [20] = 1, [21] = 1, [24] = 1};
        s32 cur = *(s32 *)(g + 0x21558), n = *(s32 *)(g + 0x21510), idx[64], k = 0;
        for (s32 i = 0; i < n && i < 512 && k < 64; i++) {
            u8 *d = g + 0x18 + 0x100 * i, *data = *(u8 **)(d + 0x10);
            if (!data) continue;
            s32 t = *(s32 *)(data + 8), gi = *(s32 *)d;
            if (t > 0 && t < 25 && ok[t] && gi != cur && gi >= 0) idx[k++] = gi;
        }
        if (k > 0) {
            RP.skull_calls++;
            s_rp_transition(g, idx[seed % (u32)k], -1, 3, p, -1);
            return;
        }
    }
    s_rp_move_random(g, b, seed, p);
}

/* Purple Heart (trinket 5), Entity_NPC::load_entity_config (Switch 0x108a9c; REP+ 0x6bbbeb): the champion chance
 * is multiplied by 3 * multiplier in REP+ instead of 2 * multiplier. 1.7.9b computes s8 *= 2 * PlayerManager::
 * GetTrinketMultiplier(5) right after the call (returns to 0x108aa0): the stub multiplies s8 by 1.5 there. */
#define RP_SITE_PURPLE_HEART 0x108aa0u
static __attribute__((used)) uintptr_t s_rp_ph_site = (uintptr_t)-1;

HIDDEN __attribute__((used)) float rp_ph_factor_c(void) {
    if (!RP.on) return 1.0f;
    RP.ph_calls++;
    return 1.5f;
}

/* Child Leash (trinket 89) and Hive Mind, Entity_Familiar::GetMultiplier (Switch 0xaac80; REP+ 0x60dfba): REP+ adds
 * 0.25 per trinket multiplier to the base (1, 2 with BFFS!) before the Tainted Bethany x0.75 and hologram
 * x0.25 factors. The whole function is hooked: those factors are recomputed to scale the added part. */
typedef float (*RpFamMultFn)(void *);
typedef bool (*RpPlayerBoolFn)(void *);
static RpFamMultFn s_rp_fam_mult;
static RpPlayerBoolFn s_rp_is_hologram;

static float rp_hk_fam_mult(void *fam) {
    float r = s_rp_fam_mult(fam);
    if (!RP.on || !fam) return r;
    u8 *p = *(u8 **)((u8 *)fam + 0x400);
    if (!p) return r;
    /* Hive Mind (248) and Abyss locusts (REP+ 0x60dfa0 checks variant 231 by number): 1.7.9b already returns 2
     * for them through the locust's EntityConfig flags (HW: locust hit 3.5 -> 4.38 with Hive Mind, hooks off,
     * same as BFFS!), so nothing is added here */
    int m = RP.mult(p, 89);
    if (m <= 0) return r;
    float f = *(int32_t *)(p + 0x1738) == 36 ? 0.75f : 1.0f;
    if (s_rp_is_hologram && s_rp_is_hologram(p)) f *= 0.25f;
    RP.leash_calls++;
    return r + 0.25f * (float)m * f;
}

/* Tick (trinket 53): 1.7.9b lets a held Tick be swapped only for Match Stick (41); REP+ also for A Lighter
 * (135) (REP+ 0x771a30). Entity_Player::TryHoldTrinket (Switch 0x2a1ee8, from Pickup::handle_collision) uses
 * the new trinket only for that check, so A Lighter is asked about as Match Stick. */
typedef bool (*RpHoldTrinketFn)(void *, u32);
static RpHoldTrinketFn s_rp_try_hold_trinket;

static bool rp_hk_try_hold_trinket(void *p, u32 id) {
    if (RP.on && (id & 0x7fffu) == 135) return s_rp_try_hold_trinket(p, 41u | (id & 0x8000u));
    return s_rp_try_hold_trinket(p, id);
}

/* Flip (711): REP+'s items.xml charges it 6 rooms, Tainted Lazarus (types 29 and 38) keeps 4 (REP+
 * GetActiveMaxCharge(item, var) 0x7915fc). The Switch item version is only called directly, so the slot
 * version Entity_Player::GetActiveMaxCharge(int) (HUD, room clear, use, NeedsCharge...) is hooked. */
typedef int (*RpSlotChargeFn)(void *, int);
static RpSlotChargeFn s_rp_slot_max_charge;

static int rp_hk_slot_max_charge(void *p, int slot) {
    int r = s_rp_slot_max_charge(p, slot);
    if (RP.on && p && slot >= 0 && slot < 4 && *(int32_t *)((u8 *)p + 0x1964 + slot * 0x1c) == 711) {
        int t = *(int32_t *)((u8 *)p + 0x1738);
        if (t == 29 || t == 38) return 4;
    }
    return r;
}

/* Linger Bean (447), Entity_Player::update_effects (Switch 0x2d4f34; REP+ 0x79e4eb): the cloud comes when
 * the firing counter (Player+0x1d68) is 0 or 1 mod 240 (REP+) instead of mod 450. The modulo is inline, so
 * the hook of StageTransition::IsActive, the call right after HasCollectible(447) (returns to 0x2d4f5c),
 * answers true there (the game then skips its own spawn) and spawns the cloud itself on REP+'s frames,
 * as the game's block does (0x2d4f60..0x2d5034): EFFECT 105 at the player, no velocity, spawner the
 * player; trinket 134 doubles its scale; then the entity's vtable slot 4. */
/* Bomb rocks make bridges (REP+ 10). Game::BombDamage destroys each grid in range with Room::DestroyGrid
 * and, when that returns true for a solid grid, calls Room::TryMakeBridge on the pit one tile further along
 * the blast (0x355a84..0x355b10). A bomb rock's Destroy only arms it (state 1 -> 3, explodes on its next
 * update) and returns false (GridEntity_Rock::Destroy 0x38fcf0..0x38fd2c), so in Repentance it never bridged
 * (checked on the console: a rock bridges, a bomb rock does not). With REP+ on, arming a bomb rock at that
 * site counts as destroying it, so the same bridge code runs. */
/* Destroying a Devil Beggar, Key Master or Bomb Bum gives the +35% devil deal chance (REP+ 10). 1.7.9b
 * Entity_Slot::TakeDamage, after Entity::BloodExplode (0x330808), sets Level state flag 0 (STATE_BUM_KILLED,
 * read by Room::GetDevilRoomChance 0x466458: +0.35) for beggars 4, 18 and 13, flag 1 for the Devil Beggar
 * (5) and nothing for 7 and 9; REP+ 0x665507 sets flag 0 for 4, 5, 18, 7, 9 and 13. Flag 1 is still set
 * for 5 here (REP+ no longer does; nothing in the deal chances reads it). */
#define RP_SITE_SLOT_BLOOD 0x33080cu
#define RP_GAME_LEVEL_FLAGS 0x215f0u
typedef void (*RpVoidFn)(void *);
static RpVoidFn s_rp_blood_explode;
HIDDEN __attribute__((used)) void rp_blood_explode_c(void *e, uintptr_t lr) {
    if (RP.on && lr == RP.base + RP_SITE_SLOT_BLOOD && e && game_ptr()) {
        int32_t v = *(int32_t *)((u8 *)e + 0x3c);
        if (v == 5 || v == 7 || v == 9) {
            *(uint64_t *)((u8 *)game_ptr() + RP_GAME_LEVEL_FLAGS) |= 1u;
            RP.bum_calls++;
        }
    }
    s_rp_blood_explode(e);
}
/* A Key Master takes no key while the player has a Golden Key, a Bomb Bum no bomb while they have a Golden
 * Bomb (REP+ 0x662ced / 0x662d15 jump straight to the payout). 1.7.9b Entity_Slot::handle_collision needs
 * Keys >= 1 / Bombs >= 1 and takes one (0x32f434 / 0x32f450), unless Entity::IsForcedCollision (called first,
 * 0x32f1f4) was true, which goes straight to the payout (0x32f2cc). So at that site it answers true for these. */
#define RP_SITE_SLOT_FORCED 0x32f1f8u
typedef bool (*RpForcedFn)(void *);
static RpForcedFn s_rp_forced;
HIDDEN __attribute__((used)) bool rp_forced_c(void *slot, void *other, uintptr_t lr) {
    bool r = s_rp_forced(slot);
    if (!r && RP.on && lr == RP.base + RP_SITE_SLOT_FORCED && slot && other &&
        *(int32_t *)((u8 *)other + 0x38) == 1) {
        int32_t v = *(int32_t *)((u8 *)slot + 0x3c);
        if ((v == 7 && *((u8 *)other + RP_PL_GOLDEN_KEY)) || (v == 9 && *((u8 *)other + RP_PL_GOLDEN_BOMB)))
            return true;
    }
    return r;
}
/* A card or pill goes behind a pocket active item (REP+ 10: tainted characters' eternal pocket items).
 * 1.7.9b Entity_Player::add_pocketitem (0x2e8984) always puts the new item in slot 0 and shifts the rest
 * right; REP+ 0x7a3eba puts it in slot 1 when slot 0 holds a pocket active (type 2, id > 0) and the new one
 * is not an active. The drop of a full pocket before it is the same in both, so after the game's own call
 * slots 0 and 1 are put back in REP+'s order. When the game then dropped the new item itself (the
 * Player+0x390 case, 0x2e8ac4; REP+ drops the same item, 0x7a3f6e), slot 0 is already the active. */
#define RP_PL_POCKET 0x1c10
typedef void (*RpAddPocketFn)(void *, int, int);
static RpAddPocketFn s_rp_add_pocket;
HIDDEN __attribute__((used)) void rp_add_pocket_c(void *p, int id, int type) {
    u32 *sl = (u32 *)((u8 *)p + RP_PL_POCKET);
    u32 a0 = sl[0], a1 = sl[1];
    bool behind = RP.on && p && type != 2 && (int32_t)a0 > 0 && a1 == 2;
    s_rp_add_pocket(p, id, type);
    if (behind && sl[0] == (u32)id && sl[1] == (u32)type) {
        /* shifted: slot 1 was the active; full and not shifted: REP+ writes slot 1 instead of slot 0 */
        sl[2] = (u32)id;
        sl[3] = (u32)type;
        sl[0] = a0;
        sl[1] = a1;
        RP.pocket_calls++;
    }
}
/* Horse Gulp! (43), REP+ UsePill 0x7c7396: before the Smelter use, the held trinkets (PC Player+0x16c0/0x16c4,
 * Switch +0x1ab0/+0x1ab4) become golden (| 0x8000), then all cache flags and EvaluateItems. Called by the
 * loader's UsePill hook before the game's own UsePill. */
#define RP_PL_TRINKETS 0x1ab0
static void rp_pre_use_pill(void *p, int effect, int color) {
    if (!RP.on || !p || effect != 43 || !(color & 0x800)) return;
    u32 *t = (u32 *)((u8 *)p + RP_PL_TRINKETS);
    for (int i = 0; i < 2; i++)
        if (t[i]) t[i] |= 0x8000u;
    *(u32 *)((u8 *)p + RP_PL_CACHE_FLAGS) |= 0xffffu;
    if (s_rp_eval_items) s_rp_eval_items(p);
    RP.gulp_calls++;
}
#define RP_SITE_BOMB_BRIDGE 0x355a88u
typedef bool (*RpDestroyGridFn)(void *, int, bool, bool);
static RpDestroyGridFn s_rp_destroy_grid;
HIDDEN __attribute__((used)) bool rp_destroy_grid_c(void *room, int idx, bool a, bool b, uintptr_t lr) {
    u8 *ge = 0;
    int32_t st0 = -1;
    if (RP.on && lr == RP.base + RP_SITE_BOMB_BRIDGE && room && idx >= 0 && idx <= 0x1bf) {
        ge = *(u8 **)((u8 *)room + 0x30 + (size_t)idx * 8);
        if (ge && *(int32_t *)(ge + 8) == 5) st0 = *(int32_t *)(ge + 0x10);
        else ge = 0;
    }
    bool r = s_rp_destroy_grid(room, idx, a, b);
    if (!r && ge && st0 != 3 && *(int32_t *)(ge + 0x10) == 3) {
        RP.bridge_calls++;
        return true;
    }
    return r;
}
#define RP_SITE_LINGER 0x2d4f5cu
#define RP_GOT_VEC_ZERO 0xaac660u
typedef bool (*RpBoolFn)(void *);
typedef u32 (*RpU32Fn)(void);
static RpBoolFn s_rp_stage_active;
static RpU32Fn s_rp_random_u32;
static void *hk_spawn_ex(void *g, u32 type, u32 var, const Vector2 *pos, const Vector2 *vel, void *spawner, u32 sub,
                         u32 seed, u32 flags);

HIDDEN __attribute__((used)) bool rp_stage_active_c(void *st, uintptr_t lr, u8 *p) {
    bool r = s_rp_stage_active(st);
    if (r || !RP.on || lr != RP.base + RP_SITE_LINGER) return r;
    u32 n = *(u32 *)(p + 0x1d68);
    if (p[0x400] && *(int32_t *)(p + 0x404) <= 0 && *(int32_t *)(p + 0x408) <= 0 && n >= 2 && n % 240u <= 1 &&
        s_rp_random_u32 && game_ptr()) {
        const Vector2 *zero = *(const Vector2 **)(RP.base + RP_GOT_VEC_ZERO);
        u8 *e = (u8 *)hk_spawn_ex(game_ptr(), 1000, 105, (const Vector2 *)(p + 0x310), zero, p, 0, s_rp_random_u32(), 0);
        if (e) {
            if (RP.mult(p, 134) >= 1) {
                float *sc = (float *)(e + 0x12c);
                sc[0] *= 2.0f, sc[1] *= 2.0f;
            }
            ((void (*)(void *))(*(void ***)e)[4])(e);
        }
        RP.linger_calls++;
    }
    return true;  // the game's own spawn (mod 450) is skipped
}

/* Mega Blast (441) in Entity_Player::FullCharge (batteries etc.; Switch 0x299634, REP+ 0x7c3cbd): charges
 * GetActiveMaxCharge/2 (REP+) instead of /4 unless forced. Hooked whole (it only computes the amount and
 * tail-calls AddActiveCharge(amount, slot, true, false, force)). */
typedef int (*RpFullChargeFn)(void *, int, bool);
typedef int (*RpMaxChargeFn)(void *, int);
typedef int (*RpAddChargeFn)(void *, int, int, bool, bool, bool);
static RpFullChargeFn s_rp_full_charge;
static RpMaxChargeFn s_rp_max_charge;
static RpAddChargeFn s_rp_add_charge;

/* Battery Bum (REP+ 1.9.7.11): its charge payout is an Entity_Effect timer (callback Switch 0x32f120) that per tick
 * calls AddActiveCharge(1, slot, false, true, false) for slots 0, 1, 2, 3 until one takes it, so slot 0 got
 * overcharged before a second active was filled. REP+ fills every active first, then overcharges. Hooked at the
 * slot-0 call (LR 0x32f15c); the later per-slot calls of the same tick (LR 0x32f17c / 0x32f19c) only run when
 * nothing at all was taken, and then nothing can be. */
static RpAddChargeFn s_rp_add_charge_got;
HIDDEN __attribute__((used)) int rp_add_charge_c(void *p, int n, int slot, bool flash, bool over, bool force,
                                                 uintptr_t lr) {
    if (RP.on && s_rp_add_charge && n == 1 && over && !force) {
        if (lr == RP.base + 0x32f15cu) {
            for (int pass = 0; pass < 2; pass++)
                for (int sl = 0; sl < 4; sl++) {
                    /* ActiveItemDesc (0x1c each at +0x1964): item, charge, battery charge. AddActiveCharge
                     * overcharges with The Battery whatever its flag, so the first pass skips full slots */
                    const int32_t *d = (const int32_t *)((u8 *)p + 0x1964 + sl * 0x1c);
                    if (!d[0] || (pass == 0 && (!s_rp_max_charge || d[1] >= s_rp_max_charge(p, sl)))) continue;
                    int r = s_rp_add_charge(p, 1, sl, flash, pass == 1, false);
                    if (r) return r;
                }
            return 0;
        }
        if (lr == RP.base + 0x32f17cu || lr == RP.base + 0x32f19cu) return 0;
    }
    return s_rp_add_charge_got(p, n, slot, flash, over, force);
}

/* Battery Bum's charge payout first checks ItemConfig::GetCollectible(slot-0 item) (LR 0x32ae9c, x22 = the
 * player), so with only a pocket active (tainted characters) nothing was charged; REP+ charges the pocket item
 * then. Here the pocket item's config is returned for an empty slot 0. */
typedef void *(*RpGetCollFn)(void *, int);
static RpGetCollFn s_rp_get_coll_got;
HIDDEN __attribute__((used)) void *rp_get_coll_c(void *cfg, int id, uintptr_t lr, void *x22) {
    if (id == 0 && RP.on && lr == RP.base + 0x32ae9cu && x22) {
        int pocket = *(const int32_t *)((u8 *)x22 + 0x1964 + 2 * 0x1c);
        if (pocket > 0) id = pocket;
    }
    return s_rp_get_coll_got(cfg, id);
}

/* Keeper / T.Keeper healing with coins (Entity_Player::AddCoins 0x2800ac -> AddHearts(2n)): REP+ AddCoins
 * 0x7597d1 calls TriggerHeartPickedUp when the hearts went up, so it counts for Immaculate Conception (1.9.7.13). */
typedef void (*RpAddHeartsFn)(void *, int, bool);
static RpAddHeartsFn s_rp_add_hearts_got;
static void (*s_rp_heart_picked)(void *);
HIDDEN __attribute__((used)) void rp_add_hearts_c(void *p, int n, bool b, uintptr_t lr) {
    if (!(RP.on && s_rp_heart_picked && lr == RP.base + 0x2800b0u)) { s_rp_add_hearts_got(p, n, b); return; }
    int32_t before = *(const int32_t *)((u8 *)p + 0x16bc);
    s_rp_add_hearts_got(p, n, b);
    if (*(const int32_t *)((u8 *)p + 0x16bc) > before) s_rp_heart_picked(p);
}

/* Star of Bethlehem stat boost = Hallowed Ground (REP+ 1.9.7.11): Switch ai_north_star (0xce624) gives each
 * player in the aura AddHallowedGroundCountdown(2) and then ++player+0x245c, which EvaluateItems (0x283988) turns
 * into damage x1.5^n on top of the Hallowed x1.2. REP+ North Star (0x64e0f5) only adds the countdown. The
 * increment is inline right after the call, so the hook takes one off first (u16, wraps back to the old value). */
typedef void (*RpHallowFn)(void *, int);
static RpHallowFn s_rp_hallow_got;
HIDDEN __attribute__((used)) void rp_hallow_c(void *p, int n, uintptr_t lr) {
    if (RP.on && lr == RP.base + 0xce628u) *(uint16_t *)((u8 *)p + 0x245c) -= 1;
    s_rp_hallow_got(p, n);
}

/* The Siren no longer charms Isaac's Heart (familiar 62) or Incubus (80) (REP+ 1.9.7.11): Entity_Familiar::CanCharm
 * (Switch 0xaac0c, a per-variant table; used by the Siren and her song effect) answers no for them. */
typedef bool (*RpCanCharmFn)(void *);
static RpCanCharmFn s_rp_can_charm;
HIDDEN __attribute__((used)) bool rp_can_charm_c(void *f) {
    if (RP.on) {
        int32_t v = *(const int32_t *)((u8 *)f + 0x3c);
        if (v == 62 || v == 80) return false;
    }
    return s_rp_can_charm(f);
}

/* Necromancer (REP+ player form 15: Necronomicon 35 + Missing Page 2 262 + trinket A Missing Page 48): a
 * Necronomicon slot with VarData 1 is "powered" (Lua sets it on each new floor, REP+ TriggerNewStage
 * 0x7a5def). REP+ GetActiveMaxCharge(item, vardata) 0x79163c gives it 10 and GetActiveMinUsableCharge
 * 0x791733 gives 1, so it can be used at any charge; each use takes 1 (UseActiveItem 0x5b4977, in Lua).
 * Switch: both are reached through the GOT (GetActiveMaxCharge(slot) from HUD, control_active_item,
 * Discharge..., GetActiveMinUsableCharge(item, vardata) from control_active_item, Discharge, HasFullCharge). */
#define RP_SLOT(p, s, o) (*(int32_t *)((u8 *)(p) + 0x1964 + (s) * 0x1c + (o)))
static RpMaxChargeFn s_rp_max_charge_got;
typedef int (*RpMinUsableFn)(void *, int, int);
/* Milk! (familiar 100), REP+ Entity_Familiar::CanBlockProjectiles 0x612b80: it blocks shots until broken
 * (REP+ SubType = hits taken < Familiar+0xf38 = 10). 1.7.9b Milk does not block. On Switch the hits are counted
 * in repplus.lua and the vanilla SubType stays 0 until the 10th hit breaks it, so it blocks while SubType is 0. */
typedef bool (*RpCanBlockFn)(void *f);
static RpCanBlockFn s_rp_can_block;
static bool rp_hk_can_block(void *f) {
    if (RP.on && f && *(s32 *)((u8 *)f + 0x3c) == 100) return *(s32 *)((u8 *)f + 0x40) == 0;
    return s_rp_can_block(f);
}
/* Jacob & Esau "Better" controls (REP+ option JacobEsauControls, options.ini, default 1 = Better, Options+0x98;
 * read in Entity_Player::control_active_item 0x791e2e and control_pocket_item 0x7a411a). 0 = Classic = 1.7.9b:
 * Jacob's active and pocket use Item (9), Esau's Pill/Card (10), drop (11) held switches actives -> pockets.
 * Better: actives on Item, pockets on Pill/Card, drop held switches Jacob -> Esau. The Switch functions ask
 * IsActionPressed/Triggered(9 or 10) and then IsActionPressed(11) (active: & !drop, pocket: & drop), so while
 * one of them runs for a twin those first questions are answered for the Better layout. */
static int s_rp_je_mode;  // 0 off, 1 active slot 0, 2 active other slot, 3 pocket
static bool s_rp_je_esau;
static int s_rp_je_controls = 1;
typedef void (*RpCtrlActiveFn)(void *, int);
typedef void (*RpCtrlPocketFn)(void *);
static RpCtrlActiveFn s_rp_ctrl_active;
static RpCtrlPocketFn s_rp_ctrl_pocket;
static bool rp_je_twin(void *pl, bool *esau) {
    u8 *p = pl;
    s32 t = *(s32 *)(p + 0x1738);
    if (t != 19 && t != 20) return false;
    u8 *tw = *(u8 **)(p + 0x2580);
    if (!tw) return false;
    *esau = *(s32 *)(tw + 0x19f0) < *(s32 *)(p + 0x19f0);  // 1.7.9b: the twin with the lower index is the main one
    return true;
}
static void rp_hk_ctrl_active(void *p, int slot) {
    bool e;
    if (RP.on && s_rp_je_controls && p && rp_je_twin(p, &e)) {
        s_rp_je_mode = slot ? 2 : 1, s_rp_je_esau = e;
        s_rp_ctrl_active(p, slot);
        s_rp_je_mode = 0;
        return;
    }
    s_rp_ctrl_active(p, slot);
}
static void rp_hk_ctrl_pocket(void *p) {
    bool e;
    if (RP.on && s_rp_je_controls && p && rp_je_twin(p, &e)) {
        s_rp_je_mode = 3, s_rp_je_esau = e;
        s_rp_ctrl_pocket(p);
        s_rp_je_mode = 0;
        return;
    }
    s_rp_ctrl_pocket(p);
}
static int rp_je_action(int trig, void *im, u32 action, u32 ctrl, u32 *pp) {
    if (!s_rp_je_mode || action < 9 || action > 11 || !s_action_pressed || !s_action_triggered) return -1;
    if (action == 11) {
        bool d = s_action_pressed(im, 11, ctrl, pp);
        int m = s_rp_je_mode;
        s_rp_je_mode = 0;  // only the first questions of the function
        if (m == 3) return s_rp_je_esau ? d : !d;  // the pocket wants drop held
        return s_rp_je_esau ? !d : d;              // the active wants drop up
    }
    /* the twin the drop button does not select is not asked at all: KAGE's IsActionTriggered answers an action
     * once, so Jacob's question would take the Item press from Esau (drop + Item, Jacob also holding an active) */
    if (s_action_pressed(im, 11, ctrl, pp) != s_rp_je_esau) return 0;
    u32 a = s_rp_je_mode == 1 ? 9 : 10;
    return (trig ? s_action_triggered : s_action_pressed)(im, a, ctrl, pp);
}
static int core_rp_je(lua_State *l) {
    if (lua_gettop(l) >= 1) s_rp_je_controls = (int)luaL_checkinteger(l, 1) != 0;
    lua_pushinteger(l, s_rp_je_controls);
    return 1;
}
static RpMinUsableFn s_rp_min_usable;
static bool rp_necro_on(void *p) {
    return RP.mult && RP.has(p, 35, false) && RP.has(p, 262, false) && RP.mult(p, 48) >= 1;
}
static int rp_hk_max_charge(void *p, int slot) {
    if (RP.on && p && slot >= 0 && slot < 3 && RP_SLOT(p, slot, 0) == 35 && RP_SLOT(p, slot, 0x18) == 1 && rp_necro_on(p)) {
        RP.necro_calls++;
        return 10;
    }
    return s_rp_max_charge_got(p, slot);
}
static int rp_hk_min_usable(void *p, int item, int vardata) {
    if (RP.on && p && item == 35 && vardata == 1 && rp_necro_on(p)) return 1;
    return s_rp_min_usable(p, item, vardata);
}

/* The powered Necronomicon's charge bar is purple: REP+ HUD 0x846f2f colours the bar fill with tint 0.6 and
 * colorize (1.2, 0.6, 1.2, 1). Switch PlayerHUD::RenderActiveItem draws the fill with ANM2::Render (returns to
 * 0x3a5274) with the player in x27 and the slot at [sp+0x48]; the hook sets the sprite colour (ANM2+0xe8:
 * tint[4], colorize[4], offset[3]) for that one call and puts it back. */
#define RP_SITE_CHARGE_FILL 0x3a5274u
static __attribute__((used)) uintptr_t s_rp_fill_site = (uintptr_t)-1;
static __attribute__((used)) void *s_rp_anm_render_got;
static float s_rp_fill_saved[11];
/* x27 is reused for the item sprite before the fill, so RenderActiveItem (called through the PLT) is hooked
 * too and keeps its player and slot for the fill hook. */
static void *s_rp_hud_player; static int s_rp_hud_slot = -1;
typedef void (*rp_render_active_fn)(void *, unsigned, const void *, float, float, bool);
static rp_render_active_fn s_rp_render_active;
static void rp_hk_render_active(void *hud, unsigned slot, const void *pos, float a, float b, bool c) {
    void *prev = s_rp_hud_player; int prev_slot = s_rp_hud_slot;
    s_rp_hud_player = hud ? *(void **)hud : NULL; s_rp_hud_slot = (int)slot;
    s_rp_render_active(hud, slot, pos, a, b, c);
    s_rp_hud_player = prev; s_rp_hud_slot = prev_slot;
}
HIDDEN __attribute__((used)) bool rp_bar_color_c(u8 *anm, void *p, u64 slot) {
    p = s_rp_hud_player; slot = (u64)(s64)s_rp_hud_slot;
    if (!RP.on || !anm || !p || slot > 2 || RP_SLOT(p, slot, 0) != 35 || RP_SLOT(p, slot, 0x18) != 1 || !rp_necro_on(p))
        return false;
    float *m = (float *)(anm + 0xe8);
    memcpy(s_rp_fill_saved, m, sizeof s_rp_fill_saved);
    m[0] = m[1] = m[2] = 0.6f;
    m[4] = 1.2f, m[5] = 0.6f, m[6] = 1.2f, m[7] = 1.0f;
    m[8] = m[9] = m[10] = 0.0f;
    return true;
}
HIDDEN __attribute__((used)) void rp_bar_color_restore_c(u8 *anm, u64 changed) {
    if (changed & 1) memcpy(anm + 0xe8, s_rp_fill_saved, sizeof s_rp_fill_saved);
}


/* Hairpin (trinket 120) in Entity_Player::update_effects (Switch 0x2cbd30..0x2cbd78, REP+ 0x795309): entering a
 * boss room fully charges the first active it can; golden or with Mom's Box REP+ passes overcharge to
 * FullCharge (0x7c3c40 arg 3 -> AddActiveCharge's overcharge), so the charge also fills the battery bar. */
#define RP_SITE_HAIRPIN 0x2cbd40u
static u32 s_rp_hairpin_calls;

static int rp_hk_full_charge(void *p, int slot, bool force) {
    uintptr_t lr = (uintptr_t)__builtin_return_address(0);
    if (RP.on && slot >= 0 && slot < 4 && s_rp_max_charge && s_rp_add_charge &&
        lr >= RP.base + RP_SITE_HAIRPIN && lr <= RP.base + RP_SITE_HAIRPIN + 0x3c && ((lr - RP.base - RP_SITE_HAIRPIN) % 0x14) == 0 &&
        (RP.mult(p, 120) >= 2 || RP.has(p, 439, false))) {
        int max = s_rp_max_charge(p, slot);
        int amount = *(int32_t *)((u8 *)p + 0x1964 + slot * 0x1c) == 441 && !force ? max / 2 : 2 * max;
        s_rp_hairpin_calls++;
        return s_rp_add_charge(p, amount, slot, true, true, force);
    }
    if (RP.on && !force && slot >= 0 && slot < 4 && s_rp_max_charge && s_rp_add_charge &&
        *(int32_t *)((u8 *)p + 0x1964 + slot * 0x1c) == 441) {
        RP.megablast_calls++;
        return s_rp_add_charge(p, s_rp_max_charge(p, slot) / 2, slot, true, false, false);
    }
    return s_rp_full_charge(p, slot, force);
}

/* Evil Charm (632), REP+ GridEntity_Web::Update 0x721b31 (vtable 0xb69618 after "gfx/grid/grid_web.anm2"):
 * a player with the item (or its temporary effect) is not slowed by a cobweb. Switch GridEntity_Web::Update
 * slows every non-flying entity in range with Entity::AddSlowing (returns to 0x397390). */
#define RP_SITE_WEB_SLOW 0x397390u
typedef void (*RpAddSlowFn)(void *, void *, int, float, void *);
static RpAddSlowFn s_rp_add_slowing;
static u32 s_rp_web_skips;
/* Book of the Dead (545), wiki REP+ ("Removed in Repentance+"): 1.7.9b upgrades a summoned friendly Bony to
 * Bone Fly (25.4), Big Bony (830) or Revenant (841) only if that enemy killed Isaac once on the save file
 * (PersistentGameData::GetBestiaryDeathCount, returns to 0x309b20 / 0x309b94 / 0x309c20 in UseActiveItem).
 * REP+ drops the condition: those three calls answer 1. */
typedef int (*RpDeathCountFn)(void *, u32, u32);
static RpDeathCountFn s_rp_death_count;
static u32 s_rp_botd_calls;
static int rp_hk_death_count(void *pgd, u32 type, u32 var) {
    uintptr_t o = (uintptr_t)__builtin_return_address(0) - RP.base;
    if (RP.on && (o == 0x309b20u || o == 0x309b94u || o == 0x309c20u)) { s_rp_botd_calls++; return 1; }
    return s_rp_death_count(pgd, type, var);
}

static void rp_hk_add_slowing(void *e, void *ref, int dur, float val, void *color) {
    uintptr_t lr = (uintptr_t)__builtin_return_address(0);
    if (RP.on && lr == RP.base + RP_SITE_WEB_SLOW && rp_is_player(e) &&
        (RP.has(e, 632, false) || RP.te_has((u8 *)e + 0x18d8, 632))) {
        s_rp_web_skips++;
        return;
    }
    s_rp_add_slowing(e, ref, dur, val, color);
}

/* The stubs hand the C part the caller's SP, LR and x19 (EvaluateItems keeps the player in x19 and its
 * locals at sp) and tail-call it, so it returns straight to the game. */
#ifndef ISAACLUA_HOST_TEST
void rp_hk_te_has_effect(void);
void rp_hk_has_collectible(void);
void rp_hk_random(void);
void rp_hk_trinket_mult(void);
void rp_hk_remove_trinket(void);
void rp_hk_can_reroll(void);
void rp_hk_add_charge(void);
void rp_hk_get_coll(void);
void rp_hk_add_hearts(void);
void rp_hk_hallow(void);
void rp_hk_gfuel(void);
void rp_hk_stage_active(void);
void rp_hk_free_random(void);
void rp_hk_rng_random(void);
void rp_hk_rng_ctor(void);
void rp_hk_pm_mult(void);
void rp_hk_destroy_grid(void);
void rp_hk_blood_explode(void);
void rp_hk_forced(void);
void rp_hk_first_coll(void);
void rp_hk_add_coins(void);
void rp_hk_use_key(void);
void rp_hk_move_random(void);
void rp_hk_has_trinket(void);
void rp_hk_check_fam(void);
void rp_hk_first_trinket(void);
void rp_hk_num_coll(void);
void rp_hk_load_image(void);
void rp_hk_get_trinket(void);
void rp_hk_anm_render(void);
void rp_hk_multishot(void);
__asm__(".text\n.p2align 2\n.hidden rp_hk_te_has_effect\n.type rp_hk_te_has_effect,%function\n"
        "rp_hk_te_has_effect:\n"
        "mov x2, sp\nmov x3, x30\nmov x4, x19\nb rp_te_has_effect_c\n"
        ".hidden rp_hk_has_collectible\n.type rp_hk_has_collectible,%function\n"
        "rp_hk_has_collectible:\n"
        "adrp x16, s_rp_epi_site\nldr x16, [x16, :lo12:s_rp_epi_site]\ncmp x16, x30\nb.eq 5f\n"
        "mov x3, x30\nb rp_has_collectible_c\n"
        /* EvaluateItems' Epiphora check: s10 = shots per second there (callee-saved, only touched here) */
        "5:\nstp x29, x30, [sp, #-32]!\nmov x29, sp\nstp x0, x1, [sp, #16]\nbl rp_epi_mult_c\n"
        "ldp x0, x1, [sp, #16]\nldp x29, x30, [sp], #32\n"
        "fcmp s0, #0.0\nb.mi 6f\nb.eq 7f\nfmul s10, s10, s0\n7:\nmov w0, wzr\nret\n"
        "6:\nmov w2, wzr\nmov x3, x30\nb rp_has_collectible_c\n"
        ".hidden rp_hk_random\n.type rp_hk_random,%function\n"
        "rp_hk_random:\n"
        "mov x2, x30\nmov x3, x19\nmov x4, sp\nb rp_random_c\n"
        ".hidden rp_hk_trinket_mult\n.type rp_hk_trinket_mult,%function\n"
        "rp_hk_trinket_mult:\n"
        "mov x2, x30\nmov x3, sp\nmov x4, x20\nmov x5, x21\nmov x6, x23\nb rp_trinket_mult_c\n"
        ".hidden rp_hk_can_reroll\n.type rp_hk_can_reroll,%function\n"
        "rp_hk_can_reroll:\n"
        "mov x2, x30\nb rp_can_reroll_c\n"
        ".hidden rp_hk_add_charge\n.type rp_hk_add_charge,%function\n"
        "rp_hk_add_charge:\n"
        "mov x6, x30\nb rp_add_charge_c\n"
        ".hidden rp_hk_get_coll\n.type rp_hk_get_coll,%function\n"
        "rp_hk_get_coll:\n"
        "mov x2, x30\nmov x3, x22\nb rp_get_coll_c\n"
        ".hidden rp_hk_hallow\n.type rp_hk_hallow,%function\n"
        "rp_hk_hallow:\n"
        "mov x2, x30\nb rp_hallow_c\n"
        ".hidden rp_hk_add_hearts\n.type rp_hk_add_hearts,%function\n"
        "rp_hk_add_hearts:\n"
        "mov x3, x30\nb rp_add_hearts_c\n"
        ".hidden rp_hk_remove_trinket\n.type rp_hk_remove_trinket,%function\n"
        "rp_hk_remove_trinket:\n"
        "mov x2, x30\nb rp_remove_trinket_c\n"
        /* GetGFuelWeaponType(player): the C part returns what to add to s9 (shots per second at the
         * EvaluateItems site, 0 elsewhere); s9 is callee-saved, so it is added here, then the real one */
        ".hidden rp_hk_gfuel\n.type rp_hk_gfuel,%function\n"
        "rp_hk_gfuel:\n"
        "adrp x16, s_rp_eq_site\nldr x16, [x16, :lo12:s_rp_eq_site]\ncmp x16, x30\nb.ne 3f\n"
        /* EvaluateItems' shots per second: s8 (callee-saved, only touched here) */
        "stp x29, x30, [sp, #-32]!\nmov x29, sp\nstr x0, [sp, #16]\nmov x1, x25\nbl rp_equality_c\nfadd s8, s8, s0\n"
        "ldr x0, [sp, #16]\nldp x29, x30, [sp], #32\n"
        "adrp x16, s_rp_gfuel\nldr x16, [x16, :lo12:s_rp_gfuel]\nbr x16\n"
        "3:\nstp x29, x30, [sp, #-32]!\nmov x29, sp\nstr x0, [sp, #16]\n"
        "mov x1, x30\nadd x2, sp, #32\nmov x3, x25\nbl rp_gfuel_c\n"
        /* only at the site: elsewhere v9 may hold a double or vector that a scalar write would clear */
        "fcmp s0, #0.0\nb.eq 1f\nfadd s9, s9, s0\n1:\n"
        "ldr x0, [sp, #16]\nldp x29, x30, [sp], #32\n"
        "adrp x16, s_rp_gfuel\nldr x16, [x16, :lo12:s_rp_gfuel]\nbr x16\n"
        ".hidden rp_hk_stage_active\n.type rp_hk_stage_active,%function\n"
        "rp_hk_stage_active:\n"
        "mov x1, x30\nmov x2, x19\nb rp_stage_active_c\n"
        ".hidden rp_hk_free_random\n.type rp_hk_free_random,%function\n"
        "rp_hk_free_random:\n"
        "mov x1, x30\nmov x2, x20\nmov x3, x19\nb rp_free_random_c\n"
        ".hidden rp_hk_rng_ctor\n.type rp_hk_rng_ctor,%function\n"
        "rp_hk_rng_ctor:\n"
        "adrp x16, s_rp_myo_site\nldr x16, [x16, :lo12:s_rp_myo_site]\ncmp x16, x30\nb.ne 9f\n"
        "stp x29, x30, [sp, #-48]!\nmov x29, sp\nstp x0, x1, [sp, #16]\nstr x2, [sp, #32]\n"
        "add x0, sp, #48\nbl rp_myosotis_c\ncmn w0, #1\nb.eq 1f\nmov w22, w0\n1:\n"
        "ldp x0, x1, [sp, #16]\nldr x2, [sp, #32]\nldp x29, x30, [sp], #48\n"
        "9:\nadrp x16, s_rp_rng_ctor\nldr x16, [x16, :lo12:s_rp_rng_ctor]\nbr x16\n"
        ".hidden rp_hk_rng_random\n.type rp_hk_rng_random,%function\n"
        "rp_hk_rng_random:\n"
        "mov x1, x30\nmov x2, x29\nfmov s0, s9\nb rp_rng_random_c\n"
        ".hidden rp_hk_forced\n.type rp_hk_forced,%function\n"
        "rp_hk_forced:\n"
        "mov x2, x30\nb rp_forced_c\n"
        ".hidden rp_hk_blood_explode\n.type rp_hk_blood_explode,%function\n"
        "rp_hk_blood_explode:\n"
        "mov x1, x30\nb rp_blood_explode_c\n"
        ".hidden rp_hk_destroy_grid\n.type rp_hk_destroy_grid,%function\n"
        "rp_hk_destroy_grid:\n"
        "mov x4, x30\nb rp_destroy_grid_c\n"
        ".hidden rp_hk_add_coins\n.type rp_hk_add_coins,%function\n"
        "rp_hk_add_coins:\n"
        "mov x2, x30\nb rp_add_coins_c\n"
        ".hidden rp_hk_use_key\n.type rp_hk_use_key,%function\n"
        "rp_hk_use_key:\n"
        "mov x1, x30\nmov x2, x19\nb rp_use_key_c\n"
        ".hidden rp_hk_move_random\n.type rp_hk_move_random,%function\n"
        "rp_hk_move_random:\n"
        "mov x4, x30\nb rp_move_random_c\n"
        ".hidden rp_hk_num_coll\n.type rp_hk_num_coll,%function\n"
        "rp_hk_num_coll:\n"
        "adrp x16, s_rp_sale_site\nldr x16, [x16, :lo12:s_rp_sale_site]\ncmp x16, x30\nb.eq 5f\n"
        "adrp x16, s_rp_num_coll_got\nldr x16, [x16, :lo12:s_rp_num_coll_got]\nbr x16\n"
        /* TryGetShopDiscount: x20 = room, w19 = slot (callee-saved) */
        "5:\nstp x29, x30, [sp, #-16]!\nmov x29, sp\n"
        "adrp x16, s_rp_num_coll_got\nldr x16, [x16, :lo12:s_rp_num_coll_got]\nblr x16\n"
        "mov x1, x20\nmov w2, w19\nbl rp_keeper_sale_c\nldp x29, x30, [sp], #16\nret\n"
        ".hidden rp_hk_first_trinket\n.type rp_hk_first_trinket,%function\n"
        "rp_hk_first_trinket:\n"
        "adrp x16, s_rp_planet_site\nldr x16, [x16, :lo12:s_rp_planet_site]\ncmp x16, x30\nb.eq 5f\n"
        "adrp x16, s_rp_adopt_site\nldr x16, [x16, :lo12:s_rp_adopt_site]\ncmp x16, x30\nb.eq 6f\n"
        "adrp x16, s_rp_first_trinket_got\nldr x16, [x16, :lo12:s_rp_first_trinket_got]\nbr x16\n"
        /* TryGetShopDiscount: x20 = room, w19 = slot, w21 = price (callee-saved) */
        "6:\nstp x29, x30, [sp, #-32]!\nmov x29, sp\n"
        "adrp x16, s_rp_first_trinket_got\nldr x16, [x16, :lo12:s_rp_first_trinket_got]\nblr x16\nstr x0, [sp, #16]\n"
        "mov x1, x20\nmov w2, w19\nmov w3, w21\nbl rp_adopt_price_c\nmov w21, w0\nldr x0, [sp, #16]\nldp x29, x30, [sp], #32\nret\n"
        /* GetPlanetariumChance: w23 = raw stage (callee-saved, only compared after this call) */
        "5:\nstp x29, x30, [sp, #-32]!\nmov x29, sp\n"
        "adrp x16, s_rp_first_trinket_got\nldr x16, [x16, :lo12:s_rp_first_trinket_got]\nblr x16\nstr x0, [sp, #16]\n"
        "mov x1, x23\nbl rp_planet_stage_c\nmov x23, x0\nldr x0, [sp, #16]\nldp x29, x30, [sp], #32\nret\n"
        ".hidden rp_hk_load_image\n.type rp_hk_load_image,%function\n"
        "rp_hk_load_image:\n"
        "adrp x16, s_rp_hud_img_site\nldr x16, [x16, :lo12:s_rp_hud_img_site]\ncmp x16, x30\nb.eq 5f\n"
        "adrp x16, s_rp_load_image_got\nldr x16, [x16, :lo12:s_rp_load_image_got]\nbr x16\n"
        /* PlayerHUD::Update: x20 = player, w26 = the slot's item; x8 = the returned image (kept) */
        "5:\nstp x29, x30, [sp, #-48]!\nmov x29, sp\nstp x0, x2, [sp, #16]\nstp x3, x8, [sp, #32]\n"
        "mov x0, x1\nmov x1, x20\nmov x2, x26\nbl rp_hud_img_c\nmov x1, x0\n"
        "ldp x0, x2, [sp, #16]\nldp x3, x8, [sp, #32]\nldp x29, x30, [sp], #48\n"
        "adrp x16, s_rp_load_image_got\nldr x16, [x16, :lo12:s_rp_load_image_got]\nbr x16\n"
        ".hidden rp_hk_get_trinket\n.type rp_hk_get_trinket,%function\n"
        "rp_hk_get_trinket:\n"
        "adrp x16, s_rp_hist_site\nldr x16, [x16, :lo12:s_rp_hist_site]\ncmp x16, x30\nb.eq 5f\n"
        "adrp x16, s_rp_get_trinket_got\nldr x16, [x16, :lo12:s_rp_get_trinket_got]\nbr x16\n"
        /* HistoryHUD::Recompute: x23 = player */
        "5:\nmov x2, x23\nb rp_hist_item_c\n"
        ".hidden rp_hk_multishot\n.type rp_hk_multishot,%function\n"
        "rp_hk_multishot:\n"
        "adrp x16, s_rp_rocket_site\nldr x16, [x16, :lo12:s_rp_rocket_site]\ncmp x16, x30\nb.eq 5f\n"
        "adrp x16, s_rp_multishot_got\nldr x16, [x16, :lo12:s_rp_multishot_got]\nbr x16\n"
        /* Weapon_Rockets::Fire: x19 = weapon, x26 = owner player (callee-saved); x8 = the params out */
        "5:\nstp x29, x30, [sp, #-32]!\nmov x29, sp\nstr x8, [sp, #16]\n"
        "adrp x16, s_rp_multishot_got\nldr x16, [x16, :lo12:s_rp_multishot_got]\nblr x16\n"
        "mov x0, x19\nmov x1, x26\nldr x2, [sp, #16]\nbl rp_rocket_params_c\n"
        "ldr x8, [sp, #16]\nldp x29, x30, [sp], #32\nret\n"
        ".hidden rp_hk_anm_render\n.type rp_hk_anm_render,%function\n"
        "rp_hk_anm_render:\n"
        "adrp x16, s_rp_fill_site\nldr x16, [x16, :lo12:s_rp_fill_site]\ncmp x16, x30\nb.eq 5f\n"
        "adrp x16, s_rp_anm_render_got\nldr x16, [x16, :lo12:s_rp_anm_render_got]\nbr x16\n"
        /* RenderActiveItem's charge fill: x27 = player, [caller sp + 0x48] = slot */
        "5:\nstp x29, x30, [sp, #-64]!\nmov x29, sp\nstp x0, x1, [sp, #16]\nstp x2, x3, [sp, #32]\n"
        "mov x1, x27\nldr w2, [sp, #(64 + 0x48)]\nbl rp_bar_color_c\nstr x0, [sp, #48]\n"
        "ldp x0, x1, [sp, #16]\nldp x2, x3, [sp, #32]\n"
        "adrp x16, s_rp_anm_render_got\nldr x16, [x16, :lo12:s_rp_anm_render_got]\nblr x16\n"
        "ldr x0, [sp, #16]\nldr x1, [sp, #48]\nbl rp_bar_color_restore_c\n"
        "ldp x29, x30, [sp], #64\nret\n"
        ".hidden rp_hk_check_fam\n.type rp_hk_check_fam,%function\n"
        "rp_hk_check_fam:\n"
        "mov x6, x30\nb rp_check_fam_c\n"
        ".hidden rp_hk_has_trinket\n.type rp_hk_has_trinket,%function\n"
        "rp_hk_has_trinket:\n"
        "adrp x16, s_rp_cork1_site\nldr x16, [x16, :lo12:s_rp_cork1_site]\ncmp x16, x30\nb.eq 4f\n"
        "adrp x16, s_rp_cork2_site\nldr x16, [x16, :lo12:s_rp_cork2_site]\ncmp x16, x30\nb.eq 3f\n"
        "adrp x16, s_rp_head_site\nldr x16, [x16, :lo12:s_rp_head_site]\ncmp x16, x30\nb.ne 9f\n"
        /* fire_projectiles: s13 = the tear damage (callee-saved, only touched here) */
        "stp x29, x30, [sp, #-48]!\nmov x29, sp\nstp x0, x1, [sp, #16]\nstr x2, [sp, #32]\n"
        "mov x0, x19\nbl rp_head_dmg_c\nldp x0, x1, [sp, #16]\nldr x2, [sp, #32]\nldp x29, x30, [sp], #48\n"
        "fcmp s0, #0.0\nb.mi 9f\nfmov s13, s0\n"
        "9:\nadrp x16, s_rp_has_trinket\nldr x16, [x16, :lo12:s_rp_has_trinket]\nbr x16\n"
        /* Entity_Effect::Update, creep size: x0 = the player, x19 = the effect, s8 = the size (callee-saved) */
        "4:\nstp x29, x30, [sp, #-32]!\nmov x29, sp\nstr x0, [sp, #16]\n"
        "adrp x16, s_rp_has_trinket\nldr x16, [x16, :lo12:s_rp_has_trinket]\nblr x16\ncbz w0, 2f\n"
        "ldr x0, [sp, #16]\nmov x1, x19\nfmov s0, s8\nbl rp_cork1_c\n"
        "fcmp s0, #0.0\nb.mi 1f\nfmov s8, s0\nmov w0, wzr\nb 2f\n"
        "1:\nmov w0, #1\n"
        "2:\nldp x29, x30, [sp], #32\nret\n"
        /* Entity_Effect::Update, creep scale: x0 = the player, x19 = the effect */
        "3:\nstp x29, x30, [sp, #-32]!\nmov x29, sp\nstr x0, [sp, #16]\n"
        "adrp x16, s_rp_has_trinket\nldr x16, [x16, :lo12:s_rp_has_trinket]\nblr x16\ncbz w0, 2b\n"
        "ldr x0, [sp, #16]\nmov x1, x19\nbl rp_cork2_c\neor w0, w0, #1\nb 2b\n"
        ".hidden rp_hk_first_coll\n.type rp_hk_first_coll,%function\n"
        "rp_hk_first_coll:\n"
        "adrp x16, s_rp_deal_site\nldr x16, [x16, :lo12:s_rp_deal_site]\ncmp x16, x30\nb.eq 5f\n"
        "mov x4, x30\nb rp_first_coll_c\n"
        /* GetDevilRoomChance: s9 = the chance so far (callee-saved, only touched here) */
        "5:\nstp x29, x30, [sp, #-32]!\nmov x29, sp\n"
        "adrp x16, s_rp_first_coll\nldr x16, [x16, :lo12:s_rp_first_coll]\nblr x16\nstr x0, [sp, #16]\n"
        "bl rp_deal_bonus_c\nfadd s9, s9, s0\nldr x0, [sp, #16]\nldp x29, x30, [sp], #32\nret\n"
        ".hidden rp_hk_pm_mult\n.type rp_hk_pm_mult,%function\n"
        "rp_hk_pm_mult:\n"
        "adrp x16, s_rp_ph_site\nldr x16, [x16, :lo12:s_rp_ph_site]\ncmp x16, x30\nb.eq 5f\n"
        "adrp x16, s_rp_pm_mult\nldr x16, [x16, :lo12:s_rp_pm_mult]\nbr x16\n"
        /* load_entity_config's Purple Heart check: s8 = champion chance there (callee-saved, only touched here) */
        "5:\nstp x29, x30, [sp, #-32]!\nmov x29, sp\n"
        "adrp x16, s_rp_pm_mult\nldr x16, [x16, :lo12:s_rp_pm_mult]\nblr x16\nstr x0, [sp, #16]\n"
        "bl rp_ph_factor_c\nfmul s8, s8, s0\nldr x0, [sp, #16]\nldp x29, x30, [sp], #32\nret\n");
#endif

/* ------------------------------------------------------------ My Stuff (REP+ pause page)
 * REP+ PauseScreen::Update (0x9b6230) has 4 menu entries: 0 options, 1 My Stuff, 2 resume, 3 exit; Switch
 * 1.7.9b has 3 (ProcessInput 0x4313dc: [ps+8] 0 options, 1 resume, 2 exit, state [ps] 1 = the menu). The
 * entry is put between OPTIONS and RESUME as the main menu's MODS is: the game's cursor is taken off while
 * it is picked (its "Cursor" overlay), confirm opens the page, and while the page is open the game's
 * pause input and render are replaced by src/repplus.lua (_isaaclua_mystuff), as REP+ runs MyStuff in
 * pause state 3 instead of the menu (0x9b62b0 / 0x9b799b). */
#define kPause_State 0
#define kPause_Sel 8
#define kPause_Anm 0x30
enum { kActPause = 12, kActLeft = 17, kActRight = 18, kActLT = 21, kActRT = 22, kActTab = 23 };
static VoidFn s_ps_input, s_ps_render;
static void (*s_anm_remove_overlay)(void *);
static void (*s_anm_set_overlay)(void *, const char *, bool);
static void (*s_anm_set_overlay_frame)(void *, int);
static bool s_stuff_item, s_stuff_open;

/* _isaaclua_mystuff(ev, triggered, held) -> its boolean */
static bool rp_mystuff(const char *ev, u32 trig, u32 held) {
    if (!L) return false;
    int top = lua_gettop(L);
    bool r = false;
    if (lua_getglobal(L, "_isaaclua_mystuff") == LUA_TFUNCTION) {
        lua_pushstring(L, ev);
        lua_pushinteger(L, trig);
        lua_pushinteger(L, held);
        if (lua_pcall(L, 3, 1, 0) != LUA_OK) overlay_add(lua_tostring(L, -1), OVERLAY_ERROR);
        else r = lua_toboolean(L, -1);
    }
    lua_settop(L, top);
    return r;
}
static bool rp_act(ActionBoolFn fn, u32 a) { return fn && s_input_mgr && fn(s_input_mgr, a, (u32)-1, NULL); }
/* menu buttons as _isaaclua_mystuff gets them: left 1, right 2, up 4, down 8, confirm 16, back 32, tab 64 */
static u32 rp_buttons(ActionBoolFn fn) {
    static const u32 acts[] = {kActLeft, kActRight, kActUp, kActDown, kActConfirm, kActBack};
    u32 m = 0;
    for (u32 i = 0; i < sizeof acts / sizeof *acts; i++)
        if (rp_act(fn, acts[i])) m |= 1u << i;
    if (rp_act(fn, kActTab) || rp_act(fn, kActLT) || rp_act(fn, kActRT)) m |= 64;
    return m;
}
static void rp_stuff_close(void) {
    if (s_stuff_open) rp_mystuff("close", 0, 0);
    s_stuff_open = false;
}
static void rp_hk_ps_input(void *ps) {
    int *state = (int *)((u8 *)ps + kPause_State), *sel = (int *)((u8 *)ps + kPause_Sel);
    if (!RP.on || *state != 1) {
        s_stuff_item = false;
        rp_stuff_close();
        s_ps_input(ps);
        return;
    }
    if (s_stuff_open) {  // the page has the input (REP+ state 3); back closes it, the menu is as it was
        if (!rp_mystuff("input", rp_buttons(s_action_triggered), rp_buttons(s_action_pressed))) {
            rp_stuff_close();
            menu_sound(283);
        }
        return;
    }
    const u32 mine = 1u << kActConfirm | 1u << kActUp | 1u << kActDown;
    if (s_stuff_item) {
        if (triggered(kActConfirm)) {
            if (rp_mystuff("open", 0, 0)) s_stuff_open = true, menu_sound(283);
        } else if (triggered(kActUp) || triggered(kActDown)) {
            s_stuff_item = false;
            *sel = triggered(kActUp) ? 0 : 1;
            if (s_anm_set_overlay_frame) s_anm_set_overlay_frame((u8 *)ps + kPause_Anm, *sel);
            menu_sound(282);
        }
        s_block_actions = mine;  // pause / back still resume the game
        s_ps_input(ps);
        s_block_actions = 0;
        if (s_stuff_item) *sel = 0;
        return;
    }
    int before = *sel;
    s_ps_input(ps);
    bool down = rp_act(s_action_pressed, kActDown), up = rp_act(s_action_pressed, kActUp);
    if (*state == 1 && ((before == 0 && *sel == 1 && down) || (before == 1 && *sel == 0 && up)))
        s_stuff_item = true, *sel = 0;
}
/* REP+ compact pause: REP+ PauseScreen::Open (0x9b7b90) always sets style [ps+4] = 1, so the pause menu is
 * PauseScreen_small.anm2 at (W*0.7, H*0.975) (0x9b774e) with no stats / My Stuff papers: OPTIONS, MY STUFF!,
 * RESUME GAME, EXIT GAME, the seed paper (null Seed, 2 lines 15 apart) and the completion widget (null 0).
 * Switch keeps its menu logic and animation timing (state 1, ANM2 at +0x30 playing Appear / Dissapear);
 * src/repplus.lua draws the REP+ sprite on that frame and returns the widget spot, the widget is the game's. */
#define kPause_Widget 0x80b90      /* CompletionWidget (Switch PauseScreen::Render 0x433018) */
#define kPause_WidgetOn 0x80cf0
#define kGame_Seeds 0x25d40
#define kGame_SeedHidden 0x26fb10  /* Switch hides the seed paper while it is non-zero (0x432f94) */
static bool (*s_anm_is_playing)(void *, const char *);
static int (*s_anm_get_frame)(void *);
static void (*s_widget_render)(void *, const Vec2Arg *, const Vec2Arg *);
static bool (*s_unlocks_disallowed)(void *);
static bool (*s_anm_is_finished)(void *, const char *);
static void (*s_widget_init)(void *, bool);
static bool rp_small_pause(void *ps, int sel) {
    if (!L || !s_anm_is_playing || !s_anm_get_frame) return false;
    void *anm = (u8 *)ps + kPause_Anm;
    /* a finished Dissapear stays the current animation until the state leaves 1: the menu must not come back
     * for those frames (HW: it flashed when closing) */
    bool gone = s_anm_is_finished && s_anm_is_finished(anm, "Dissapear");
    const char *anim = s_anm_is_playing(anm, "Appear") ? "Appear"
                       : gone || s_anm_is_playing(anm, "Dissapear") ? "Dissapear" : "Idle";
    if (gone) return true;
    int frame = s_anm_get_frame(anm);
    /* the widget's pause sheet (CompletionWidget::Init(true): completion_widget_pause.png) was loaded with the
     * PauseScreen at boot, before the mods' resources: load it again once so a mod's sheet is used, as on PC */
    static void *s_widget_reinit;
    if (s_widget_reinit != ps && s_widget_init) s_widget_init((u8 *)ps + kPause_Widget, true), s_widget_reinit = ps;
    u8 *game = G.g_Game ? (u8 *)*G.g_Game : NULL;
    int top = lua_gettop(L);
    bool r = false;
    float wx = 0, wy = 0, ws = 1;
    if (lua_getglobal(L, "_isaaclua_pause_small") == LUA_TFUNCTION) {
        lua_pushstring(L, anim);
        lua_pushinteger(L, frame);
        lua_pushinteger(L, sel);
        lua_pushboolean(L, game && *(int *)(game + kGame_SeedHidden) == 0);
        if (lua_pcall(L, 4, 4, 0) != LUA_OK) overlay_add(lua_tostring(L, -1), OVERLAY_ERROR);
        else r = lua_toboolean(L, -4), wx = (float)lua_tonumber(L, -3), wy = (float)lua_tonumber(L, -2),
             ws = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 1;
    }
    lua_settop(L, top);
    if (r && wx > -100 && game && s_widget_render && *(int *)((u8 *)ps + kPause_WidgetOn) >= 1 &&
        !(s_unlocks_disallowed && s_unlocks_disallowed(game + kGame_Seeds))) {
        Vec2Arg pos = {wx, wy}, scale = {ws, ws};
        /* the widget fades in and out with the pause screen (HW: it vanished at once while the menu faded out),
         * over about the frames the menu takes to come and go (8 in, 7 out; tuned on HW); its ANM2 is at +8 */
        float *alpha = (float *)((u8 *)ps + kPause_Widget + 8 + kAnm_ColorMod + 12), was = *alpha;
        float k = anim[0] == 'A' ? frame / 8.0f : anim[0] == 'D' ? 1.0f - frame / 7.0f : 1.0f;
        *alpha = was * (k < 0 ? 0 : k > 1 ? 1 : k);
        s_widget_render((u8 *)ps + kPause_Widget, &pos, &scale);
        *alpha = was;
    }
    return r;
}
static void rp_hk_ps_render_in(void *ps);
static void rp_hk_ps_render(void *ps) {  // Lua text (Font, RenderText) only draws while g_in_render
    bool was = g_in_render;
    g_in_render = true;
    rp_hk_ps_render_in(ps);
    g_in_render = was;
}
static void rp_hk_ps_render_in(void *ps) {
    int state = *(int *)((u8 *)ps + kPause_State);
    if (RP.on && s_stuff_open && state == 1) {
        rp_mystuff("render", 0, 0);
        return;
    }
    if (RP.on && state == 1) {  // REP+ menu index: 0 options, 1 My Stuff, 2 resume, 3 exit
        int sel = *(int *)((u8 *)ps + kPause_Sel);
        if (rp_small_pause(ps, s_stuff_item ? 1 : sel == 0 ? 0 : sel + 1)) return;
    }
    bool hide = RP.on && s_stuff_item && state == 1 && s_anm_remove_overlay && s_anm_set_overlay;
    if (hide) s_anm_remove_overlay((u8 *)ps + kPause_Anm);
    s_ps_render(ps);
    if (hide) {
        s_anm_set_overlay((u8 *)ps + kPause_Anm, "Cursor", true);
        if (s_anm_set_overlay_frame) s_anm_set_overlay_frame((u8 *)ps + kPause_Anm, 0);
        rp_mystuff("menu", 0, 0);
    }
}
static void rp_mystuff_install(const ModView *v) {
#ifndef ISAACLUA_HOST_TEST
    s_anm_remove_overlay = (void (*)(void *))api_sym("_ZN15IsaacRepentance4ANM213RemoveOverlayEv");
    s_anm_set_overlay = (void (*)(void *, const char *, bool))api_sym("_ZN15IsaacRepentance4ANM219SetOverlayAnimationEPKcb");
    s_anm_set_overlay_frame = (void (*)(void *, int))api_sym("_ZN15IsaacRepentance4ANM215SetOverlayFrameEi");
    s_anm_is_playing = (bool (*)(void *, const char *))api_sym("_ZNK15IsaacRepentance4ANM29IsPlayingEPKc");
    s_anm_get_frame = (int (*)(void *))api_sym("_ZNK15IsaacRepentance4ANM28GetFrameEv");
    s_widget_render = (void (*)(void *, const Vec2Arg *, const Vec2Arg *))api_sym(
        "_ZN15IsaacRepentance16CompletionWidget6RenderERKN4KAGE4Math7Vector2ES5_");
    s_anm_is_finished = (bool (*)(void *, const char *))api_sym("_ZNK15IsaacRepentance4ANM210IsFinishedEPKc");
    s_widget_init = (void (*)(void *, bool))api_sym("_ZN15IsaacRepentance16CompletionWidget4InitEb");
    s_unlocks_disallowed = (bool (*)(void *))api_sym("_ZNK15IsaacRepentance5Seeds28AchievementUnlocksDisallowedEv");
    s_ps_input = (VoidFn)hook_got(v, "_ZN15IsaacRepentance11PauseScreen12ProcessInputEv", (void *)rp_hk_ps_input);
    s_ps_render = (VoidFn)hook_got(v, "_ZN15IsaacRepentance11PauseScreen6RenderEv", (void *)rp_hk_ps_render);
    if (!s_ps_input || !s_ps_render) plugin_log("[isaac-repplus] PauseScreen GOT slots not found, no My Stuff page");
#else
    (void)v;
#endif
}

/* C.repplus(on): turn the hooks on/off; returns the number of tears / shot speed site hits so far */
/* core.repplus_proc_item(seed) -> a new glitched (procedural) item id, as TMTRAINER pedestals get one
 * (Switch Entity_Pickup::Update 0x26e6e8: ProceduralItemManager at Game+0x345f18, CreateProceduralItem(seed, 0));
 * used by REP+ Dataminer (UseActiveItem 0x5c05b0). */
#define RP_FN_CREATE_PROC_ITEM 0x4453e8u
#define RP_GAME_PROC_ITEMS 0x345f18u
static int core_rp_proc_item(lua_State *l) {
    u32 seed = (u32)luaL_checkinteger(l, 1);
    u8 *g = game_ptr();
    if (!g || !RP.base) return 0;
    s32 (*create)(void *, u32, u32) = (s32 (*)(void *, u32, u32))(RP.base + RP_FN_CREATE_PROC_ITEM);
    lua_pushinteger(l, create(g + RP_GAME_PROC_ITEMS, seed, 0));
    return 1;
}
static int core_repplus(lua_State *l) {
    if (lua_gettop(l) >= 1) RP.on = lua_toboolean(l, 1) && RP.has && RP.te_has;
    lua_pushinteger(l, RP.tears_calls);
    lua_pushinteger(l, RP.ss_calls);
    lua_pushboolean(l, RP.on);
    lua_pushinteger(l, RP.blood_calls);
    lua_pushinteger(l, RP.linger_calls);
    lua_pushinteger(l, RP.megablast_calls);
    lua_pushinteger(l, RP.superball_calls);
    lua_pushinteger(l, RP.leash_calls);
    lua_pushinteger(l, RP.epi_calls);
    lua_pushinteger(l, RP.penny_calls);
    lua_pushinteger(l, RP.ph_calls);
    lua_pushinteger(l, RP.ithurts_calls);
    lua_pushinteger(l, RP.bridge_calls);
    lua_pushinteger(l, RP.void_calls);
    lua_pushinteger(l, RP.bum_calls);
    lua_pushinteger(l, RP.gold_calls);
    lua_pushinteger(l, RP.pocket_calls);
    lua_pushinteger(l, RP.gulp_calls);
    lua_pushinteger(l, RP.deal_calls);
    lua_pushinteger(l, RP.tear_calls);
    lua_pushinteger(l, RP.coin_calls);
    lua_pushinteger(l, RP.magnet_calls);
    lua_pushinteger(l, RP.callus_calls);
    lua_pushinteger(l, RP.clip_calls);
    lua_pushinteger(l, RP.head_calls);
    lua_pushinteger(l, RP.eq_calls);
    lua_pushinteger(l, RP.crown_calls);
    lua_pushinteger(l, RP.skull_calls);
    lua_pushinteger(l, RP.rocket_calls);
    lua_pushinteger(l, RP.bomb_calls);
    lua_pushinteger(l, RP.necro_calls);
    lua_pushinteger(l, RP.infamy_calls);
    return 32;
}

static void rp_bosses_install(const ModView *v);  // repplus_bosses.inc
static void repplus_install(const ModView *v, const struct nnro_Module *m) {
#ifndef ISAACLUA_HOST_TEST
    RP.base = v->base;
    if (!lookup(m, "_ZNK15IsaacRepentance13Entity_Player18NumCollectibleHeldENS_16eCollectibleTypeEb", &RP.num) ||
        !lookup(m, "_ZNK15IsaacRepentance16TemporaryEffects12GetEffectNumENS_16eCollectibleTypeE", &RP.te_num) ||
        !lookup(m, "_ZNK15IsaacRepentance16TemporaryEffects9HasEffectENS_12eTrinketTypeE", &RP.te_has_trinket) ||
        !lookup(m, "_ZNK15IsaacRepentance13Entity_Player20GetTrinketMultiplierENS_12eTrinketTypeE", &RP.mult)) {
        plugin_log("[isaac-repplus] symbols missing, native changes off");
        return;
    }
    RP.te_has = (RpTeHasFn)hook_got(v, "_ZNK15IsaacRepentance16TemporaryEffects9HasEffectENS_16eCollectibleTypeE",
                                    (void *)rp_hk_te_has_effect);
    RP.has = (RpHasFn)hook_got(v, "_ZNK15IsaacRepentance13Entity_Player14HasCollectibleENS_16eCollectibleTypeEb",
                               (void *)rp_hk_has_collectible);
    if (!RP.te_has || !RP.has) plugin_log("[isaac-repplus] GOT slots not found, native changes off");
    s_rp_eval_items = (VoidFn)api_sym("_ZN15IsaacRepentance13Entity_Player13EvaluateItemsEv");
    s_rp_epi_site = RP.base + RP_SITE_EPI_EVAL;
    s_rp_random = (RpRandFn)hook_got(v, "_ZN15IsaacRepentance3RNG6RandomEj", (void *)rp_hk_random);
    s_rp_mult_got = (RpMultFn)hook_got(v, "_ZNK15IsaacRepentance13Entity_Player20GetTrinketMultiplierENS_12eTrinketTypeE",
                                       (void *)rp_hk_trinket_mult);
    s_rp_remove_trinket = (RpRemoveTrinketFn)hook_got(
        v, "_ZN15IsaacRepentance13Entity_Player16TryRemoveTrinketENS_12eTrinketTypeE", (void *)rp_hk_remove_trinket);
    s_rp_gfuel = (RpIntFn)hook_got(v, "_ZNK15IsaacRepentance13Entity_Player18GetGFuelWeaponTypeEv", (void *)rp_hk_gfuel);
    s_rp_random_u32 = (RpU32Fn)api_sym("_ZN15IsaacRepentance9RandomU32Ev");
    s_rp_clicker_u32 = (u32 (*)(void))s_rp_random_u32;
    s_rp_rm_gold_key = api_sym("_ZN15IsaacRepentance13Entity_Player15RemoveGoldenKeyEv");
    s_rp_anm_get_layer = (void *(*)(void *, int))api_sym("_ZN15IsaacRepentance4ANM28GetLayerEi");
    s_rp_rm_gold_bomb = api_sym("_ZN15IsaacRepentance13Entity_Player16RemoveGoldenBombEv");
    s_rp_stage_active = (RpBoolFn)hook_got(v, "_ZNK15IsaacRepentance15StageTransition8IsActiveEv", (void *)rp_hk_stage_active);
    if (!s_rp_stage_active) plugin_log("[isaac-repplus] StageTransition::IsActive GOT slot not found, no Linger Bean change");
    s_rp_max_charge = (RpMaxChargeFn)api_sym("_ZNK15IsaacRepentance13Entity_Player18GetActiveMaxChargeEi");
    s_rp_add_charge = (RpAddChargeFn)api_sym("_ZN15IsaacRepentance13Entity_Player15AddActiveChargeEiibbb");
    s_rp_add_charge_got = (RpAddChargeFn)hook_got(v, "_ZN15IsaacRepentance13Entity_Player15AddActiveChargeEiibbb", (void *)rp_hk_add_charge);
    s_rp_can_charm = (RpCanCharmFn)hook_got(v, "_ZNK15IsaacRepentance15Entity_Familiar8CanCharmEv", (void *)rp_can_charm_c);
    s_rp_get_coll_got = (RpGetCollFn)hook_got(v, "_ZNK15IsaacRepentance10ItemConfig14GetCollectibleEi", (void *)rp_hk_get_coll);
    s_rp_heart_picked = (void (*)(void *))api_sym("_ZN15IsaacRepentance13Entity_Player20TriggerHeartPickedUpEv");
    s_rp_add_hearts_got = (RpAddHeartsFn)hook_got(v, "_ZN15IsaacRepentance13Entity_Player9AddHeartsEib", (void *)rp_hk_add_hearts);
    s_rp_hallow_got = (RpHallowFn)hook_got(v, "_ZN15IsaacRepentance13Entity_Player26AddHallowedGroundCountdownEi", (void *)rp_hk_hallow);
    if (!s_rp_hallow_got) plugin_log("[isaac-repplus] AddHallowedGroundCountdown GOT slot not found, Star of Bethlehem keeps x1.8");
    if (!s_rp_add_hearts_got) plugin_log("[isaac-repplus] AddHearts GOT slot not found, no Keeper Immaculate Conception");
    if (!s_rp_get_coll_got) plugin_log("[isaac-repplus] ItemConfig::GetCollectible GOT slot not found, no Battery Bum pocket charge");
    if (!s_rp_add_charge_got) { s_rp_add_charge_got = s_rp_add_charge; plugin_log("[isaac-repplus] AddActiveCharge GOT slot not found, no Battery Bum change"); }
    s_rp_can_reroll = (RpCanRerollFn)hook_got(
        v, "_ZN15IsaacRepentance13Entity_Player20CanRerollCollectibleENS_16eCollectibleTypeE", (void *)rp_hk_can_reroll);
    s_rp_full_charge = (RpFullChargeFn)hook_got(v, "_ZN15IsaacRepentance13Entity_Player10FullChargeEib", (void *)rp_hk_full_charge);
    s_rp_max_charge_got = (RpMaxChargeFn)hook_got(v, "_ZNK15IsaacRepentance13Entity_Player18GetActiveMaxChargeEi", (void *)rp_hk_max_charge);
    s_rp_min_usable = (RpMinUsableFn)hook_got(v, "_ZNK15IsaacRepentance13Entity_Player24GetActiveMinUsableChargeENS_16eCollectibleTypeEi", (void *)rp_hk_min_usable);
    if (!s_rp_max_charge_got || !s_rp_min_usable) plugin_log("[isaac-repplus] charge GOT slots not found, no powered Necronomicon");
    s_rp_can_block = (RpCanBlockFn)hook_got(v, "_ZNK15IsaacRepentance15Entity_Familiar19CanBlockProjectilesEv", (void *)rp_hk_can_block);
    s_rp_ctrl_active = (RpCtrlActiveFn)hook_got(v, "_ZN15IsaacRepentance13Entity_Player19control_active_itemEi", (void *)rp_hk_ctrl_active);
    s_rp_ctrl_pocket = (RpCtrlPocketFn)hook_got(v, "_ZN15IsaacRepentance13Entity_Player19control_pocket_itemEv", (void *)rp_hk_ctrl_pocket);
    if (!s_rp_ctrl_active || !s_rp_ctrl_pocket) plugin_log("[isaac-repplus] control_*_item GOT slots not found, no J&E Better controls");
    if (!s_rp_can_block) plugin_log("[isaac-repplus] CanBlockProjectiles GOT slot not found, Milk! does not block");
    s_rp_fill_site = RP.base + RP_SITE_CHARGE_FILL;
    s_rp_render_active = (rp_render_active_fn)hook_got(v, "_ZN15IsaacRepentance3HUD9PlayerHUD16RenderActiveItemEjRKN4KAGE4Math7Vector2Effb", (void *)rp_hk_render_active);
    if (s_rp_render_active) s_rp_anm_render_got = hook_got(v, "_ZN15IsaacRepentance4ANM26RenderERKN4KAGE4Math7Vector2ES5_S5_", (void *)rp_hk_anm_render);
    if (!s_rp_anm_render_got) plugin_log("[isaac-repplus] ANM2::Render GOT slot not found, no purple Necronomicon bar");
    s_rp_ent_frames = (int (*)(void *))api_sym("_ZNK15IsaacRepentance6Entity13GetFrameCountEv");
    s_rp_free_random = (RpFreeRandFn)hook_got(v, "_ZN15IsaacRepentance6RandomEj", (void *)rp_hk_free_random);
    s_rp_free_random_fn = s_rp_free_random;
    if (!s_rp_free_random) plugin_log("[isaac-repplus] Random GOT slot not found, no Super Ball change");
    s_rp_rng_random = (RpRngFloatFn)hook_got(v, "_ZN15IsaacRepentance3RNG6RandomEv", (void *)rp_hk_rng_random);
    if (!s_rp_rng_random) plugin_log("[isaac-repplus] RNG::Random GOT slot not found, no Blessed Penny change");
    s_rp_ph_site = RP.base + RP_SITE_PURPLE_HEART;
    s_rp_first_trinket = (void *(*)(void *, int, void **, bool))api_sym("_ZN15IsaacRepentance13PlayerManager17FirstTrinketOwnerENS_12eTrinketTypeEPPNS_3RNGEb");
    s_rp_deal_site = RP.base + RP_SITE_DEAL_CHANCE;
    s_rp_adopt_site = RP.base + RP_SITE_ADOPT;
    s_rp_planet_site = RP.base + RP_SITE_PLANET_LENS;
    s_rp_hud_img_site = RP.base + RP_SITE_HUD_ACTIVE_IMG;
    s_rp_load_image_got = hook_got(v, "_ZN15IsaacRepentance7Manager9LoadImageEPKcPN4KAGE8Graphics25VertexAttributeDescriptorEb",
                                   (void *)rp_hk_load_image);
    if (!s_rp_load_image_got) plugin_log("[isaac-repplus] Manager::LoadImage GOT slot not found, no powered Necronomicon icon");
    s_rp_hist_site = RP.base + RP_SITE_HISTORY_TRINKET;
    s_rp_get_collectible = (void *(*)(void *, int))api_sym("_ZNK15IsaacRepentance10ItemConfig14GetCollectibleEi");
    s_rp_get_trinket_got = hook_got(v, "_ZNK15IsaacRepentance10ItemConfig10GetTrinketEi", (void *)rp_hk_get_trinket);
    if (!s_rp_get_trinket_got) plugin_log("[isaac-repplus] ItemConfig::GetTrinket GOT slot not found, no Modeling Clay / Error in the Extra HUD");
    s_rp_rocket_site = RP.base + RP_SITE_ROCKET_PARAMS;
    s_rp_multishot_got = hook_got(v, "_ZNK15IsaacRepentance6Weapon18GetMultiShotParamsENS_11eWeaponTypeE",
                                  (void *)rp_hk_multishot);
    if (!s_rp_multishot_got) plugin_log("[isaac-repplus] Weapon::GetMultiShotParams GOT slot not found, no Doctor's Remote change");
    s_rp_sale_site = RP.base + RP_SITE_KEEPER_SALE;
    s_rp_num_coll_got = hook_got(v, "_ZN15IsaacRepentance13PlayerManager18GetNumCollectiblesENS_16eCollectibleTypeE",
                                 (void *)rp_hk_num_coll);
    if (!s_rp_num_coll_got) plugin_log("[isaac-repplus] PlayerManager::GetNumCollectibles GOT slot not found, no Keeper's Bargain sale");
    s_rp_first_trinket_got = hook_got(v, "_ZN15IsaacRepentance13PlayerManager17FirstTrinketOwnerENS_12eTrinketTypeEPPNS_3RNGEb",
                                      (void *)rp_hk_first_trinket);
    if (!s_rp_first_trinket_got) plugin_log("[isaac-repplus] FirstTrinketOwner GOT slot not found, no Womb planetariums");
    s_rp_add_coins = (RpAddCoinsFn)hook_got(v, "_ZN15IsaacRepentance13Entity_Player8AddCoinsEi", (void *)rp_hk_add_coins);
    if (!s_rp_add_coins) plugin_log("[isaac-repplus] Entity_Player::AddCoins GOT slot not found, no Counterfeit Penny change");
    s_rp_has_trinket = hook_got(v, "_ZNK15IsaacRepentance13Entity_Player10HasTrinketENS_12eTrinketTypeEb", (void *)rp_hk_has_trinket);
    if (!s_rp_has_trinket) plugin_log("[isaac-repplus] Entity_Player::HasTrinket GOT slot not found, no Isaac's Head change");
    s_rp_head_site = RP.base + RP_SITE_HEAD_DMG;
    s_rp_cork1_site = RP.base + RP_SITE_CORK_SIZE;
    s_rp_cork2_site = RP.base + RP_SITE_CORK_SCALE;
    s_rp_eq_site = RP.base + RP_SITE_EQUALITY;
    s_rp_rng_ctor = hook_got(v, "_ZN15IsaacRepentance3RNGC1Ejj", (void *)rp_hk_rng_ctor);
    if (s_rp_rng_ctor) s_rp_myo_site = RP.base + RP_SITE_MYOSOTIS;
    else plugin_log("[isaac-repplus] RNG::RNG GOT slot not found, no Myosotis change");
    s_rp_check_fam = (RpCheckFamFn)hook_got(v, "_ZN15IsaacRepentance13Entity_Player14check_familiarEjiRNS_3RNGEPKNS_10ItemConfig4ItemEi", (void *)rp_hk_check_fam);
    if (!s_rp_check_fam) plugin_log("[isaac-repplus] Entity_Player::check_familiar GOT slot not found, no Soul change");
    s_rp_move_random = (RpMoveRandFn)hook_got(v, "_ZN15IsaacRepentance4Game16MoveToRandomRoomEbjPNS_13Entity_PlayerE", (void *)rp_hk_move_random);
    s_rp_transition = (RpTransFn)api_sym("_ZN15IsaacRepentance4Game19StartRoomTransitionEiNS_10eDirectionENS_14RoomTransition10eAnimationEPNS_13Entity_PlayerEi");
    if (!s_rp_move_random) plugin_log("[isaac-repplus] Game::MoveToRandomRoom GOT slot not found, no Cursed Skull change");
    s_rp_use_key = (RpUseKeyFn)hook_got(v, "_ZN15IsaacRepentance13Entity_Player9TryUseKeyEv", (void *)rp_hk_use_key);
    if (!s_rp_use_key) plugin_log("[isaac-repplus] Entity_Player::TryUseKey GOT slot not found, no Paper Clip change");
    s_rp_first_coll = hook_got(v, "_ZN15IsaacRepentance13PlayerManager21FirstCollectibleOwnerENS_16eCollectibleTypeEPPNS_3RNGEb", (void *)rp_hk_first_coll);
    if (!s_rp_first_coll) plugin_log("[isaac-repplus] PlayerManager::FirstCollectibleOwner GOT slot not found, no deal chance change");
    s_rp_pm_mult = hook_got(v, "_ZN15IsaacRepentance13PlayerManager20GetTrinketMultiplierENS_12eTrinketTypeE", (void *)rp_hk_pm_mult);
    if (!s_rp_pm_mult) plugin_log("[isaac-repplus] PlayerManager::GetTrinketMultiplier GOT slot not found, no Purple Heart change");
    s_rp_blood_explode = (RpVoidFn)hook_got(v, "_ZN15IsaacRepentance6Entity12BloodExplodeEv", (void *)rp_hk_blood_explode);
    if (!s_rp_blood_explode) plugin_log("[isaac-repplus] Entity::BloodExplode GOT slot not found, no beggar deal chance");
    s_rp_add_pocket = (RpAddPocketFn)hook_got(v, "_ZN15IsaacRepentance13Entity_Player14add_pocketitemEiNS_16PlayerPocketItem11ePocketTypeE", (void *)rp_add_pocket_c);
    if (!s_rp_add_pocket) plugin_log("[isaac-repplus] Entity_Player::add_pocketitem GOT slot not found, cards go before pocket actives");
    s_rp_forced = (RpForcedFn)hook_got(v, "_ZN15IsaacRepentance6Entity17IsForcedCollisionEv", (void *)rp_hk_forced);
    if (!s_rp_forced) plugin_log("[isaac-repplus] Entity::IsForcedCollision GOT slot not found, golden key/bomb still cost one");
    s_rp_destroy_grid = (RpDestroyGridFn)hook_got(v, "_ZN15IsaacRepentance4Room11DestroyGridEibb", (void *)rp_hk_destroy_grid);
    if (!s_rp_destroy_grid) plugin_log("[isaac-repplus] Room::DestroyGrid GOT slot not found, no bomb rock bridges");
    s_rp_is_hologram = (RpPlayerBoolFn)api_sym("_ZNK15IsaacRepentance13Entity_Player10IsHologramEv");
    s_rp_fam_mult = (RpFamMultFn)hook_got(v, "_ZNK15IsaacRepentance15Entity_Familiar13GetMultiplierEv", (void *)rp_hk_fam_mult);
    if (!s_rp_fam_mult) plugin_log("[isaac-repplus] Familiar::GetMultiplier GOT slot not found, no Child Leash change");
    s_rp_try_hold_trinket = (RpHoldTrinketFn)hook_got(v, "_ZN15IsaacRepentance13Entity_Player14TryHoldTrinketENS_12eTrinketTypeE",
                                                       (void *)rp_hk_try_hold_trinket);
    if (!s_rp_try_hold_trinket) plugin_log("[isaac-repplus] TryHoldTrinket GOT slot not found, no Tick change");
    s_rp_slot_max_charge =
        (RpSlotChargeFn)hook_got(v, "_ZNK15IsaacRepentance13Entity_Player18GetActiveMaxChargeEi", (void *)rp_hk_slot_max_charge);
    if (!s_rp_slot_max_charge) plugin_log("[isaac-repplus] GetActiveMaxCharge GOT slot not found, no Flip change");
    if (!s_rp_full_charge) plugin_log("[isaac-repplus] FullCharge GOT slot not found, no Mega Blast change");
    if (!s_rp_gfuel) plugin_log("[isaac-repplus] GetGFuelWeaponType GOT slot not found, no Blood Rights tears");
    if (!s_rp_random || !s_rp_mult_got || !s_rp_remove_trinket)
        plugin_log("[isaac-repplus] Tonsil/Bag Lunch GOT slots not found");
    s_rp_add_slowing = (RpAddSlowFn)hook_got(v, "_ZN15IsaacRepentance6Entity10AddSlowingERKNS0_9EntityRefEifNS_8ColorModE",
                                             (void *)rp_hk_add_slowing);
    if (!s_rp_add_slowing) plugin_log("[isaac-repplus] Entity::AddSlowing GOT slot not found, no Evil Charm web immunity");
    s_rp_death_count = (RpDeathCountFn)hook_got(
        v, "_ZNK15IsaacRepentance18PersistentGameData21GetBestiaryDeathCountENS_11eEntityTypeEj", (void *)rp_hk_death_count);
    if (!s_rp_death_count) plugin_log("[isaac-repplus] GetBestiaryDeathCount GOT slot not found, Book of the Dead as 1.7.9b");
    rp_mystuff_install(v);
    rp_bosses_install(v);  // last: its hooks chain to the ones above
#else
    (void)v; (void)m;
#endif
}

#include "repplus_bosses.inc"
