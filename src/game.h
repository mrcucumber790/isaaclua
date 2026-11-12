#pragma once
/* Repentance.nro (Switch 1.7.9b, build id 91c73fdd57506131) layout and the game functions we call.
 * Offsets come from isaacqol (NOTES.md there); every function is looked up by its exported symbol. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint64_t u64;
typedef int64_t s64;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint8_t u8;

typedef struct { float x, y; } Vector2;
typedef struct { _Alignas(16) u8 raw[64]; } ColorStorage;  /* KAGE::Graphics::Color */

/* Game */
#define kGame_Room 0x21550        /* Room* */
#define kGame_FrameCount 0x24f99c /* u32 */
#define kGame_Stage 0x0           /* Level is at offset 0 of Game; its first u32 is the stage */
/* Room */
#define kRoom_EnterFrame 0x191c   /* game frame the room was entered */
#define kRoom_EntityList 0x1950
#define kList_Data 0x78
#define kList_Count 0x84
/* Manager */
#define kManager_HudFont 0xd7080  /* KAGE::Graphics::Font */
#define kManager_ItemConfig 0x36538 /* ItemConfig (Entity_Player::GetActiveChargeType) */
/* ItemConfig::Item (fields from its destructor and GetDisplayName) */
#define kItem_Type 0x0
#define kItem_ID 0x4
#define kItem_Name 0x8            /* std::string, a "#..._NAME" string table key for vanilla items */
#define kItem_Description 0x20
#define kItem_GfxFileName 0x38
/* ANM2 (isaacqol NOTES.md) */
#define kAnmSize 0x158
#define kAnm_CurData 0x38         /* AnimationData*, name std::string at +0 */
#define kAnm_OverlayFirst 0x90    /* bool: the overlay renders before the animation (ANM2::Reset) */
#define kAnm_Offset 0xd4          /* Vector2 (ANM2::Reset: Zero) */
#define kAnm_Scale 0xdc           /* Vector2 */
#define kAnm_Rotation 0xe4        /* float (ANM2::Reset: 0) */
#define kAnm_FlipX 0x140          /* bool, FlipY at +0x141 (ANM2::Reset clears both) */
#define kAnm_Speed 0x144          /* float PlaybackSpeed (ANM2::Reset: 1) */
#define kAnm_ColorMod 0xe8        /* tint[4], colorize[4], offset[3] */
/* Entity */
#define kEnt_Type 0x38
#define kEnt_Variant 0x3c
#define kEnt_SubType 0x40
#define kEnt_Position 0x310
#define kEnt_Velocity 0x334
#define kEnt_InitSeed 0x3e8
#define kEnt_HitPoints 0x354
#define kEnt_MaxHitPoints 0x358
#define kEnt_Dead 0x1c3
#define kEnt_Sprite 0x50          /* ANM2 */
/* Entity_NPC */
#define kNpc_IsBoss 0xcc9
/* Entity_Player (hearts / pickups from Add* functions, stats from HUD::StatHUD::RecomputeStats) */
#define kPlayer_MaxHearts 0x16b8
#define kPlayer_Hearts 0x16bc
#define kPlayer_EternalHearts 0x16c0
#define kPlayer_SoulHearts 0x16c4
#define kPlayer_Keys 0x16d4
#define kPlayer_ControllerIndex 0x19ec  // EntityPlayer.ControllerIndex (api.lua reads the same field)
#define kPlayer_Bombs 0x16dc
#define kPlayer_SubPlayer 0x2460     // the Forgotten/Soul partner, not in the PlayerManager list (re/player.md)
#define kPlayer_BackupPlayer 0x2588  // not in the list either
#define kPlayer_Coins 0x16e0
#define kPlayer_BoneHearts 0x2450
#define kPlayer_ShotSpeed 0x1838
#define kPlayer_Damage 0x1844
#define kPlayer_TearHeight 0x1848  /* -23.75 at the CACHE_RANGE reset in EvaluateItems */
#define kPlayer_TearFallingSpeed 0x184c
#define kPlayer_TearFallingAccel 0x1850
#define kPlayer_TearRange 0x1854
#define kPlayer_MoveSpeed 0x194c
#define kPlayer_Type 0x1738
#define kPlayer_MaxFireDelay 0x1834
#define kPlayer_Luck 0x1950
#define kPlayer_RockBottom 0x25d8 /* float[6] best speed, tears, damage, range, shot speed, luck (EvaluateItems) */
#define kPlayer_CacheFlags 0x1958
#define kPlayer_ActiveItems 0x1964 /* 4 x 28 bytes, item id first (CanOverrideActiveItem) */
#define kPlayer_CollectibleNum 0x1ab8 /* std::vector<u32>: begin, end */

typedef struct { u64 w[3]; } StdString; /* libc++ std::string */

typedef struct {
    void **g_Game;
    void **g_Manager;
    float *width, *height;
    void (*color_ctor)(ColorStorage *, float, float, float, float);
    void (*draw_utf8)(const void *font, const char *s, float x, float y, float sx, float sy, ColorStorage *c,
                      u32 box_width, bool center);
    int (*string_width_utf8)(const void *font, const char *s);
    bool (*is_action_pressed)(void *mgr, u32 action, u32 controller, void *entity);
    bool (*is_action_triggered)(void *mgr, u32 action, u32 controller, void *entity);
    int (*ent_frame_count)(const void *e);
    void (*ent_kill)(void *e);
    void (*ent_remove)(void *e);
    bool (*is_greed)(const void *game);
    /* Entity_Player */
    void (*add_coins)(void *p, int n);
    void (*add_bombs)(void *p, int n);
    void (*add_keys)(void *p, int n);
    void (*add_hearts)(void *p, int n, bool unk);
    void (*add_max_hearts)(void *p, int n, bool ignore_keeper);
    void (*add_soul_hearts)(void *p, int n);
    void (*add_black_hearts)(void *p, int n);
    void (*add_eternal_hearts)(void *p, int n);
    void (*add_bone_hearts)(void *p, int n);
    void (*remove_collectible)(void *p, int id, bool ignore_modifiers, int slot, bool remove_from_form);
    void (*add_card)(void *p, int card);
    void (*add_pill)(void *p, int color);
    void (*animate_happy)(void *p);
    void (*animate_sad)(void *p);
    void (*change_player_type)(void *p, int type, bool unk);
    void (*add_collectible)(void *p, int id, int charge, bool first_time, int slot, int var_data);
    bool (*has_collectible)(const void *p, int id, bool ignore_modifiers);
    bool (*has_trinket)(const void *p, int id, bool ignore_modifiers);
    void (*add_trinket)(void *p, int id, bool first_time);
    void (*use_active_item)(void *p, int id, u32 flags, int slot, int var_data);
    void (*evaluate_items)(void *p);
    /* Room / ItemConfig */
    Vector2 (*world_to_screen)(const void *room, Vector2 pos);
    const void *(*get_collectible)(const void *item_config, int id);
    const void *(*get_trinket)(const void *item_config, int id);
    /* ANM2 */
    void (*anm_ctor)(void *);
    void (*anm_dtor)(void *);
    void (*anm_reset)(void *);
    void (*anm_load)(void *, const StdString *, bool);
    void (*anm_replace_sheet)(void *, int, const StdString *);
    void (*anm_load_graphics)(void *);
    void (*anm_set_frame)(void *, const char *, int);
    void (*anm_set_frame_i)(void *, int);
    bool (*anm_set_animation)(void *, const char *, bool);
    void (*anm_play)(void *, const char *, bool);
    void (*anm_stop)(void *);
    void (*anm_update)(void *);
    void (*anm_render)(void *, const Vector2 *, const Vector2 *, const Vector2 *);
    void (*anm_render_layer)(void *, int, const Vector2 *, const Vector2 *, const Vector2 *);
    int (*anm_get_frame)(const void *);
    bool (*anm_is_finished)(const void *, const char *);
    bool (*anm_is_playing)(const void *, const char *);
    bool (*anm_is_event_triggered)(const void *, const char *);
    /* EntityList::QueryRadius(pos, radius, partitions): the list Isaac.FindInRadius returns on PC. Returned in
     * memory (x8): {owned, data, capacity, count}; data sits in the list's per-frame scratch, nothing to free. */
    struct QueryResult { u8 owned, pad_[7]; u8 *const *data; u32 capacity, count; } (*query_radius)(void *, const Vector2 *, float, u32);
} GameApi;

extern GameApi G;

static inline u8 *game_ptr(void) { return G.g_Game ? (u8 *)*G.g_Game : NULL; }
static inline u8 *manager_ptr(void) { return G.g_Manager ? (u8 *)*G.g_Manager : NULL; }
static inline u8 *room_ptr(void) {
    u8 *g = game_ptr();
    return g ? *(u8 **)(g + kGame_Room) : NULL;
}

/* Entities of the current room (the list isaacqol walks). */
typedef struct { u8 *const *data; u32 count; } EntList;
static inline EntList room_entities(void) {
    EntList L = {NULL, 0};
    u8 *r = room_ptr();
    if (!r) return L;
    L.data = *(u8 *const *const *)(r + kRoom_EntityList + kList_Data);
    L.count = L.data ? *(const u32 *)(r + kRoom_EntityList + kList_Count) : 0;
    return L;
}

/* libc++ std::string: byte0 bit0 = long {cap|1, size, data}; short = size << 1, chars from byte 1 */
static inline const char *std_str(const void *s) {
    const u8 *b = (const u8 *)s;
    return (b[0] & 1) ? *(const char *const *)(b + 16) : (const char *)b + 1;
}

#define ENT_I32(e, off) (*(s32 *)((u8 *)(e) + (off)))
#define ENT_F32(e, off) (*(float *)((u8 *)(e) + (off)))
