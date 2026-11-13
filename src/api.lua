---------------------------------------------------------------- the PC API written in Lua
-- Same chunk as prelude.lua (its locals game, room, core are visible here). Engine data is read
-- with core.peek at the offsets below (Switch 1.7.9b; sources in game.h / README) and exported game
-- functions are called with core.call. Anything not found on Switch yet reports itself once
-- ("not on Switch yet: ...") and returns a neutral value, so a mod keeps running.
local C = core
local peek, poke, call, offset = C.peek, C.poke, C.call, C.offset
-- EntityFactory::Create: types 1-9 have their own classes, 1000 is an effect, 9001 text; every other
-- type, mod types above 1000 included (Samael's scythe 6617), is an Entity_NPC
local function npc_type(t) return t ~= nil and t >= 10 and t ~= 1000 and t ~= 9001 end

REPENTANCE = true
function StartDebug() end  -- PC: attaches the Lua debugger (--luadebug); nothing to attach to here

---------------------------------------------------------------- enums whose vanilla names differ from the generated ones
for k, v in pairs({COLLECTIBLE_1UP = 11, COLLECTIBLE_20_20 = 245, COLLECTIBLE_120_VOLT = 559}) do
  rawset(CollectibleType, k, v)
end
PlayerForm = {
  PLAYERFORM_GUPPY = 0, PLAYERFORM_LORD_OF_THE_FLIES = 1, PLAYERFORM_MUSHROOM = 2, PLAYERFORM_ANGEL = 3,
  PLAYERFORM_BOB = 4, PLAYERFORM_DRUGS = 5, PLAYERFORM_MOM = 6, PLAYERFORM_BABY = 7, PLAYERFORM_EVIL_ANGEL = 8,
  PLAYERFORM_POOP = 9, PLAYERFORM_BOOK_WORM = 10, PLAYERFORM_ADULTHOOD = 11, PLAYERFORM_SPIDERBABY = 12,
  PLAYERFORM_STOMPY = 13, NUM_PLAYER_FORMS = 14,
}
Controller = {
  DPAD_LEFT = 0, DPAD_RIGHT = 1, DPAD_UP = 2, DPAD_DOWN = 3, BUTTON_A = 4, BUTTON_B = 5, BUTTON_X = 6,
  BUTTON_Y = 7, BUMPER_LEFT = 8, TRIGGER_LEFT = 9, STICK_LEFT = 10, BUMPER_RIGHT = 11, TRIGGER_RIGHT = 12,
  STICK_RIGHT = 13, BUTTON_BACK = 14, BUTTON_START = 15,
}
InputHook = {IS_ACTION_PRESSED = 0, IS_ACTION_TRIGGERED = 1, GET_ACTION_VALUE = 2}
EntityPartition = {FAMILIAR = 1, BULLET = 2, TEAR = 4, ENEMY = 8, PICKUP = 16, PLAYER = 32, EFFECT = 64}
UseFlag = {
  USE_NOANIM = 1, USE_NOCOSTUME = 2, USE_OWNED = 4, USE_ALLOWNONMAIN = 8, USE_REMOVEACTIVE = 16,
  USE_CARBATTERY = 32, USE_VOID = 64, USE_MIMIC = 128, USE_NOANNOUNCER = 256, USE_ALLOWWISPSPAWN = 512,
  USE_CUSTOMVARDATA = 1024, USE_NOHUD = 2048,
}
LevelCurse = {
  CURSE_NONE = 0, CURSE_OF_DARKNESS = 1, CURSE_OF_LABYRINTH = 2, CURSE_OF_THE_LOST = 4, CURSE_OF_THE_UNKNOWN = 8,
  CURSE_OF_THE_CURSED = 16, CURSE_OF_MAZE = 32, CURSE_OF_BLIND = 64, CURSE_OF_GIANT = 128, NUM_CURSES = 9,
}
LevelStage = {
  STAGE_NULL = 0, STAGE1_1 = 1, STAGE1_2 = 2, STAGE2_1 = 3, STAGE2_2 = 4, STAGE3_1 = 5, STAGE3_2 = 6,
  STAGE4_1 = 7, STAGE4_2 = 8, STAGE4_3 = 9, STAGE5 = 10, STAGE6 = 11, STAGE7 = 12, STAGE8 = 13, NUM_STAGES = 14,
  STAGE1_GREED = 1, STAGE2_GREED = 2, STAGE3_GREED = 3, STAGE4_GREED = 4, STAGE5_GREED = 5, STAGE6_GREED = 6,
  STAGE7_GREED = 7,
}
StageType = {
  STAGETYPE_ORIGINAL = 0, STAGETYPE_WOTL = 1, STAGETYPE_AFTERBIRTH = 2, STAGETYPE_GREEDMODE = 3,
  STAGETYPE_REPENTANCE = 4, STAGETYPE_REPENTANCE_B = 5,
}
Difficulty = {DIFFICULTY_NORMAL = 0, DIFFICULTY_HARD = 1, DIFFICULTY_GREED = 2, DIFFICULTY_GREEDIER = 3}
Direction = {NO_DIRECTION = -1, LEFT = 0, UP = 1, RIGHT = 2, DOWN = 3}
Mouse = {MOUSE_BUTTON_1 = 0, MOUSE_BUTTON_2 = 1, MOUSE_BUTTON_3 = 2, MOUSE_BUTTON_LEFT = 0, MOUSE_BUTTON_RIGHT = 1}
GridCollisionClass = {COLLISION_NONE = 0, COLLISION_PIT = 1, COLLISION_OBJECT = 2, COLLISION_SOLID = 3,
  COLLISION_WALL = 4, COLLISION_WALL_EXCEPT_PLAYER = 5}
RoomShape = {ROOMSHAPE_1x1 = 1, ROOMSHAPE_IH = 2, ROOMSHAPE_IV = 3, ROOMSHAPE_1x2 = 4, ROOMSHAPE_IIV = 5,
  ROOMSHAPE_2x1 = 6, ROOMSHAPE_IIH = 7, ROOMSHAPE_2x2 = 8, ROOMSHAPE_LTL = 9, ROOMSHAPE_LTR = 10,
  ROOMSHAPE_LBL = 11, ROOMSHAPE_LBR = 12, NUM_ROOMSHAPES = 13}
NullItemID = setmetatable({}, {__index = function() return -1 end})
CallbackPriority = {IMPORTANT = -200, EARLY = -100, DEFAULT = 0, LATE = 100}

local symcache = {}
local function sym(name)
  local f = symcache[name]
  if f == nil then f = C.sym(name) or false; symcache[name] = f end
  return f
end
-- a Lua function calling an exported game function, or reporting `what` once and returning `default`
-- A method (sig "?p...") called with no object returns `default`: mods load at boot, before the Game
-- exists, and on PC the same calls see an empty game
local function native(mangled, sig, what, default)
  local method = sig:sub(2, 2) == "p"
  -- a bad argument (a nil Vector...) is reported at the mod's line, with the API name
  local function fail(e) error(what .. ": " .. (tostring(e):gsub("^[^:]*:%d+: ", "")), 3) end
  local f  -- resolved on the first call (the symbol table is ready once the game runs)
  return function(this, ...)
    if not f then
      f = sym(mangled)
      if not f then C.missing(what); return default end
    end
    if method and this == nil then return default end
    local ok, r = pcall(call, f, sig, this, ...)
    if not ok then fail(r) end
    return r
  end
end
-- the exported function if this build has it, else the Lua stand-in (same arguments)
local function native_or(mangled, sig, fallback)
  return function(...)
    local f = sym(mangled)
    if not f then return fallback(...) end
    return call(f, sig, ...)
  end
end
-- stand-ins for names the loader does not have; listed in the registry (weak) so test/apichk can tell them apart
debug.getregistry()["isaaclua.stubs"] = setmetatable({}, {__mode = "k"})
local function stub(what, default)
  local f = function() C.missing(what); return default end
  debug.getregistry()["isaaclua.stubs"][f] = what
  return f
end
local NS = "_ZN15IsaacRepentance"
local NSK = "_ZNK15IsaacRepentance"

---------------------------------------------------------------- layout (Switch 1.7.9b)
local O = {
  Game_Room = 0x21550, Game_RoomIdx = 0x21558, Game_ItemPool = 0x242c0, Game_PlayerManager = 0x25c40,
  Game_Seeds = 0x25d40, Game_HUD = 0xe8468, Game_FrameCount = 0x24f99c, Game_TimeCounter = 0x24f9a0,
  Game_BossRushParTime = 0x24f9a4, Game_BlueWombParTime = 0x24f9a8, Game_StateFlags = 0x26fa50,
  Game_Challenge = 0x26fa88, Game_VictoryLap = 0x26fd50,
  Level_StageType = 0x4, Level_Rooms = 0x18, Level_RoomCount = 0x21510, Level_StartRoom = 0x21514,
  Level_LastRoomIdx = 0x2155c, Level_Dimension = 0x21560, Level_StateFlags = 0x215f0,
  Room_Desc = 0x8, Room_FirstVisit = 0x1, Room_GridPath = 0xe98, Room_ScrollOffset = 0x1938,
  Room_Enemies = 0x1a58, Room_Bosses = 0x1a60, Pool_LastPool = 0x8c0,
  Desc_DecorationSeed = 0x64, Desc_SpawnSeed = 0x68, Desc_AwardSeed = 0x6c,
  Manager_Options = 0x364d0,
  Game_Difficulty = 0x2f0028,
  Room_Type = 0x10, Room_Width = 0x14, Room_Height = 0x18, Room_Grid = 0x30, Room_EnterFrame = 0x191c,
  Room_BossId = 0x1fc0,
  Grid_Type = 0x8, Grid_VarData = 0x1c,
  Desc_Grid = 0x0, Desc_SafeGrid = 0x4, Desc_List = 0x8, Desc_Data = 0x10, Desc_Display = 0x48,
  Desc_Visited = 0x4c, Desc_Flags = 0x50, RoomData_Type = 0x8, RoomData_Shape = 0x5c,
  Pool_PillEffects = 0xa2c, Pool_PillIdentified = 0xa68,
  Pickup_Charge = 0x558, Pickup_OptionsIndex = 0x55c, Pickup_Touched = 0x560, Pickup_Price = 0x564,
  Pickup_AutoUpdatePrice = 0x568, Pickup_ShopItemId = 0x56c, Pickup_Timeout = 0x570, Pickup_Wait = 0x574,
  Ent_Index = 0x30, Ent_SpawnerType = 0x44, Ent_SpawnerVariant = 0x48, Ent_Visible = 0x1c1,
  Ent_GridCollision = 0x1e0, Ent_EntityCollision = 0x1e4, Ent_PositionOffset = 0x320, Ent_Velocity = 0x334,
  Ent_Size = 0x344, Ent_SizeMulti = 0x348, Ent_DropRNG = 0x3d8,
  Player_JarHearts = 0x16cc, Player_GoldenKey = 0x16d8, Player_GoldenBomb = 0x16d9,
  Player_ControllerIndex = 0x19ec, Player_ListIndex = 0x19f0, Player_GoldenHearts = 0x1dc4,
  Player_BabySkin = 0x20e8, Player_SubPlayer = 0x2460, Player_BrokenHearts = 0x2470,
  Player_RottenHearts = 0x2474, Player_SoulCharge = 0x2478, Player_BloodCharge = 0x247c,
  Player_GigaBombs = 0x24a4, Player_Twin = 0x2580, Player_PoopMana = 0x269c, Player_PoopSpells = 0x26a0,
  Player_RNGs = 0x1bb0,  -- std::vector<RNG> (16 bytes each) per collectible, trinket +0x18, pill +0x30, card +0x48
  Manager_EntityConfig = 0x36758,
  Player_Type = 0x1738, Player_Effects = 0x18d8, Player_Actives = 0x1964, Player_Trinkets = 0x1ab0,
  Player_Pockets = 0x1c10, Player_FormCounters = 0x1c54,
  Manager_ItemConfig = 0x36538, Manager_FoundHUD = 0x3650a,
  Item_Type = 0x0, Item_ID = 0x4, Item_Name = 0x8, Item_Description = 0x20, Item_Gfx = 0x38,
}

local wrap_item  -- ItemConfig::Item* -> proxy (below)
local function gp() return C.gameptr() end
local function roomp() return peek(C.gameptr(), O.Game_Room, "p") end
if C.roomptr then roomp = C.roomptr end

---------------------------------------------------------------- entities: keys implemented here
-- EM: methods, EG: field getters, ES: field setters (looked up by api.c after its own keys)
local EM, EG, ES = {}, {}, {}
C.entity_ext(EM, EG, ES)
-- the classes as globals (PC: EntityPlayer.AddHearts(player, 2), getmetatable lookups): a method is
-- looked up on the entity it is called with.
-- getmetatable(EntityPlayer).__class is the class table, which getmetatable(player) returns too (__type is the
-- class name; save managers test it). As on PC it gives the methods (__class.HasInvincibility) and has an
-- __index that mods replace (rawset) to add or override methods for that class (Samael's api_override,
-- MinimapAPI's APIOverride): entities then index through the replaced __index of their class, then Entity's.
do
  local ent_mt = debug.getregistry()["isaaclua.Entity"]
  local base = ent_mt.__index  -- api.c ent_index, then EM / EG
  local by_type = {[1] = "EntityPlayer", [2] = "EntityTear", [3] = "EntityFamiliar", [4] = "EntityBomb",
    [5] = "EntityPickup", [7] = "EntityLaser", [8] = "EntityKnife", [9] = "EntityProjectile", [1000] = "EntityEffect"}
  local function class_name(t) return by_type[t] or (npc_type(t) and "EntityNPC") or "Entity" end
  local classes, initial = {}, {}
  for _, name in ipairs({"Entity", "EntityPlayer", "EntityNPC", "EntityPickup", "EntityEffect", "EntityTear",
                         "EntityFamiliar", "EntityBomb", "EntityLaser", "EntityKnife", "EntityProjectile"}) do
    local idx = base  -- (a mod's override calls it as its old __index: C directly, no Lua frame)
    local cls = setmetatable({__type = name, __index = idx}, {__index = function(t, k)
      local v = ent_mt[k]
      if v ~= nil then return v end
      local f = function(e, ...) return base(e, k)(e, ...) end  -- the class's own method, not an override
      rawset(t, k, f)
      return f
    end})
    classes[name], initial[name] = cls, idx
    _G[name] = setmetatable({}, {__class = cls, __index = function(t, k)
      local f = function(e, ...) return e[k](e, ...) end
      rawset(t, k, f)
      return f
    end})
  end
  -- after each mod loads: a replaced __index puts the entities' lookup through the class tables
  local overridden = false
  local function scan()
    if overridden then return end
    for name, cls in pairs(classes) do
      if rawget(cls, "__index") ~= initial[name] then overridden = true end
    end
    if not overridden then return end
    local ent_cls = classes.Entity
    local cls_of = {}  -- entity type -> class table
    if C.ent_dispatch then
      ent_mt.__index = C.ent_dispatch(cls_of, function(t) return classes[class_name(t)] end, ent_cls)
      return
    end
    ent_mt.__index = function(e, k)
      local t = base(e, "Type")
      local cls = cls_of[t]
      if not cls then cls = classes[class_name(t)]; cls_of[t] = cls end
      local f = rawget(cls, "__index")
      if f == initial[cls.__type] and cls ~= ent_cls then cls = ent_cls; f = rawget(cls, "__index") end
      if f == initial[cls.__type] then return base(e, k) end
      if type(f) == "table" then return f[k] end
      return f(e, k)
    end
  end
  local load_mod = exports.LoadMod
  exports.LoadMod = function(...)
    local r = load_mod(...)
    scan()
    return r
  end
  local raw_getmetatable = getmetatable
  function getmetatable(v)
    local mt = raw_getmetatable(v)
    if mt == nil or not rawequal(mt, ent_mt) then return mt end
    return classes[class_name(v.Type)]
  end
end

local ptr = C.ptr   -- engine pointer of a live entity, nil if gone

-- GetData(): a table per entity, kept while the entity lives (same pointer and seed)
local entdata = {}
function EM.GetData(e)
  local h = GetPtrHash(e)
  local d = entdata[h]
  if not d or d.seed ~= e.InitSeed then d = {seed = e.InitSeed, t = {}}; entdata[h] = d end
  return d.t
end

EG.Price = function(e) if e.Type == 5 then return peek(ptr(e), O.Pickup_Price, "i32") end end
ES.Price = function(e, v) if e.Type == 5 then poke(ptr(e), O.Pickup_Price, "i32", v) end end
EG.OptionsPickupIndex = function(e) if e.Type == 5 then return peek(ptr(e), O.Pickup_OptionsIndex, "i32") end end
function EM.IsShopItem(e) return e.Type == 5 and (e.Price or 0) ~= 0 end

---------------------------------------------------------------- EntityPlayer
local function player_i32(e, o) return peek(ptr(e), o, "i32") or 0 end
function EM.GetTrinket(e, slot) return player_i32(e, O.Player_Trinkets + 4 * (slot or 0)) end
-- pockets: {id, kind} x 4, kind 1 card, 0 pill
local function pocket(e, slot, kind)
  local p = ptr(e)
  if not p then return 0 end
  local base = O.Player_Pockets + 8 * (slot or 0)
  if peek(p, base + 4, "i32") ~= kind then return 0 end
  return peek(p, base, "i32")
end
function EM.GetCard(e, slot) return pocket(e, slot, 1) end
function EM.GetPill(e, slot) return pocket(e, slot, 0) end
function EM.GetPlayerFormCounter(e, form) return player_i32(e, O.Player_FormCounters + 4 * form) end
EG.QueuedItem = function(e)
  local it = peek(ptr(e), 0x1c38, "p")
  return {Item = it and wrap_item(it) or nil, Charge = 0, Touched = false}
end
function EM.IsItemQueueEmpty(e) return peek(ptr(e), 0x1c38, "p") == nil end

---------------------------------------------------------------- ItemConfig
-- ItemConfig (Manager+0x36538): std::vector<T*> of collectibles +0x0, trinkets +0x18, null items +0x30,
-- cards +0x48, pill effects +0x60 (from ItemConfig::Load / LoadPocketItems). Names are string-table
-- keys ("#SAD_ONION_NAME") on Switch; Name / Description give the English text, as on PC Repentance.
local IC = "10ItemConfig"
local function itemconfig_p() return offset(C.managerptr(), O.Manager_ItemConfig) end
local function vec_size(p, o)
  local b, e = peek(p, o, "i64"), peek(p, o + 8, "i64")
  if not b or not e then return 0 end
  return (e - b) // 8
end
local getters = {
  collectible = native(NSK .. IC .. "14GetCollectibleEi", "ppi", "ItemConfig.GetCollectible"),
  trinket = native(NSK .. IC .. "10GetTrinketEi", "ppi", "ItemConfig.GetTrinket"),
  null = native(NSK .. IC .. "11GetNullItemEi", "ppi", "ItemConfig.GetNullItem"),
  card = native(NSK .. IC .. "7GetCardEi", "ppi", "ItemConfig.GetCard"),
  pill = native(NSK .. IC .. "13GetPillEffectENS_11ePillEffectE", "ppi", "ItemConfig.GetPillEffect"),
}
local LANG_EN = 0
local function display(cls, what, p)
  return native(NSK .. IC .. cls .. what .. "EN4KAGE9eLanguageE", "Spi", "ItemConfig." .. what)(p, LANG_EN)
end

-- one metatable per config class: fields = {name = {offset, type} or function(p)}
local function config_class(cls, fields, methods)
  local mt = {}
  function mt.__index(t, k)
    local m = methods[k]
    if m then return m end
    local f = fields[k]
    local p = rawget(t, "_p")
    if type(f) == "table" then return peek(p, f[1], f[2]) end
    if f then
      local v = f(p)
      rawset(t, k, v)  -- names do not change: cache
      return v
    end
    C.missing(cls .. "." .. tostring(k))
  end
  local cache = {}
  return function(p)
    if not p then return nil end
    local w = cache[p]
    if not w then w = setmetatable({_p = p}, mt); cache[p] = w end
    return w
  end
end

local item_methods = {}
function item_methods:IsCollectible() return self.Type ~= 2 and self.Type ~= 0 end
function item_methods:IsTrinket() return self.Type == 2 end
function item_methods:IsNull() return self.Type == 0 end
function item_methods:HasTags(tags) return (self.Tags & tags) == tags end
local available_ex = native(NSK .. IC .. "4Item13IsAvailableExEj", "bpu", "ItemConfigItem.IsAvailable", true)
function item_methods:IsAvailable() return available_ex(rawget(self, "_p"), 0xffffffff) end
wrap_item = config_class("ItemConfigItem", {
  Type = {0x0, "i32"}, ID = {0x4, "i32"},
  Name = function(p) return display("4Item", "14GetDisplayName", p) end,
  Description = function(p) return display("4Item", "21GetDisplayDescription", p) end,
  GfxFileName = {0x38, "str"}, AchievementID = {0x50, "i32"}, CacheFlags = {0x54, "u32"},
  AddMaxHearts = {0x58, "i32"}, AddHearts = {0x5c, "i32"}, AddSoulHearts = {0x60, "i32"},
  AddBlackHearts = {0x64, "i32"}, AddBombs = {0x68, "i32"}, AddKeys = {0x6c, "i32"}, AddCoins = {0x70, "i32"},
  MaxCharges = {0x74, "i32"}, MaxCooldown = {0x78, "i32"}, DevilPrice = {0x7c, "i16"}, ShopPrice = {0x7e, "i16"},
  ChargeType = {0xb0, "i32"}, Special = {0xb4, "b"}, PassiveCache = {0xb5, "b"}, AddCostumeOnPickup = {0xb6, "b"},
  Hidden = {0xb7, "b"}, Persistent = {0xb8, "b"}, PersistentEffect = {0xb8, "b"}, ClearEffectsOnRemove = {0xb9, "b"}, Tags = {0xc0, "i64"},
  Quality = {0xc8, "i32"}, CraftingQuality = {0xcc, "i32"}, InitCharge = {0xd0, "i32"},
  Costume = function(p) return {ID = peek(p, 0x80, "i32"), Anm2Path = peek(p, 0x88, "str")} end,
  Discharged = function() return false end,
}, item_methods)

local card_avail = native(NSK .. IC .. "4Card11IsAvailableEv", "bp", "ItemConfigCard.IsAvailable", true)
local wrap_card = config_class("ItemConfigCard", {
  ID = {0x0, "i32"}, AchievementID = {0x4, "i32"},
  Name = function(p) return display("4Card", "14GetDisplayName", p) end,
  Description = function(p) return display("4Card", "21GetDisplayDescription", p) end,
  HudAnim = {0x38, "str"}, GreedModeAllowed = {0x50, "b"}, PickupSubtype = {0x54, "i32"},
  CardType = {0x58, "i32"}, AnnouncerVoice = {0x5c, "i32"}, AnnouncerDelay = {0x60, "i32"},
  MimicCharge = {0x64, "i32"},
}, {IsAvailable = function(self) return card_avail(rawget(self, "_p")) end,
    IsCard = function(self) return self.CardType ~= 2 end, IsRune = function(self) return self.CardType == 2 end})

local pill_avail = native(NSK .. IC .. "10PillEffect11IsAvailableEv", "bp", "ItemConfigPillEffect.IsAvailable", true)
local wrap_pill = config_class("ItemConfigPillEffect", {
  ID = {0x0, "i32"}, AchievementID = {0x4, "i32"},
  Name = function(p) return display("10PillEffect", "14GetDisplayName", p) end,
  Description = function(p) return display("10PillEffect", "21GetDisplayDescription", p) end,
  GreedModeAllowed = {0x38, "b"}, AnnouncerVoice = {0x3c, "i32"}, AnnouncerVoiceSuper = {0x40, "i32"},
  AnnouncerDelay = {0x44, "i32"}, MimicCharge = {0x48, "i32"}, EffectClass = {0x4c, "u8"},
  EffectSubClass = {0x4d, "u8"},
}, {IsAvailable = function(self) return pill_avail(rawget(self, "_p")) end})

local config = {}
local function list(o, wrap, get)
  local n = vec_size(itemconfig_p(), o)
  return setmetatable({Size = n, Get = function(_, i) return wrap(get(itemconfig_p(), i)) end}, {__len = function() return n end})
end
function config:GetCollectible(id) return wrap_item(getters.collectible(itemconfig_p(), id)) end
function config:GetTrinket(id) return wrap_item(getters.trinket(itemconfig_p(), id & 0x7fff)) end
function config:GetNullItem(id) return wrap_item(getters.null(itemconfig_p(), id)) end
function config:GetCard(id) return wrap_card(getters.card(itemconfig_p(), id)) end
function config:GetPillEffect(id) return wrap_pill(getters.pill(itemconfig_p(), id)) end
function config:GetCollectibles() return list(0x0, wrap_item, getters.collectible) end
function config:GetTrinkets() return list(0x18, wrap_item, getters.trinket) end
function config:GetNullItems() return list(0x30, wrap_item, getters.null) end
function config:GetCards() return list(0x48, wrap_card, getters.card) end
function config:GetPillEffects() return list(0x60, wrap_pill, getters.pill) end
function config.IsValidCollectible(id) return config:GetCollectible(id) ~= nil end

ItemConfig = {
  CHARGE_NORMAL = 0, CHARGE_TIMED = 1, CHARGE_SPECIAL = 2,
  CARDTYPE_TAROT = 0, CARDTYPE_SUIT = 1, CARDTYPE_RUNE = 2, CARDTYPE_SPECIAL = 3, CARDTYPE_SPECIAL_OBJECT = 4,
  CARDTYPE_TAROT_REVERSE = 5,
  Config = config,
}
do
  local tags = {"DEAD", "SYRINGE", "MOM", "TECH", "BATTERY", "GUPPY", "FLY", "BOB", "MUSHROOM", "BABY", "ANGEL",
    "DEVIL", "POOP", "BOOK", "SPIDER", "QUEST", "MONSTER_MANUAL", "NO_GREED", "FOOD", "TEARS_UP", "OFFENSIVE",
    "NO_KEEPER", "NO_LOST_BR", "STARS", "SUMMONABLE", "NO_CANTRIP", "WISP", "UNIQUE_FAMILIAR", "NO_CHALLENGE",
    "NO_DAILY", "LAZ_SHARED", "LAZ_SHARED_GLOBAL", "NO_EDEN"}
  for i, t in ipairs(tags) do ItemConfig["TAG_" .. t] = 1 << (i - 1) end
end
function Isaac.GetItemConfig() return config end
-- PC (isaac-ng.exe 0x80be40 trinkets, 0x80bfc0 pills, 0x80bf50 cards): walk the list from its end, so the last
-- entry with that name wins (a mod's card over a vanilla one). Cards are found by the hud="" string (+0x38,
-- HudAnim), not by name=""
local function find_by_name(list_fn, name, off)
  local l = list_fn(config)
  for i = l.Size - 1, 1, -1 do
    local it = l:Get(i)
    if it then
      local p = rawget(it, "_p")
      if off then
        if peek(p, off, "str") == name then return it.ID end
      elseif it.Name == name or peek(p, 0x8, "str") == name then return it.ID end
    end
  end
  return -1
end
function Isaac.GetItemIdByName(n) return find_by_name(config.GetCollectibles, n) end
function Isaac.GetTrinketIdByName(n) return find_by_name(config.GetTrinkets, n) end
function Isaac.GetCardIdByName(n) return find_by_name(config.GetCards, n, 0x38) end
function Isaac.GetPillEffectByName(n) return find_by_name(config.GetPillEffects, n) end
-- PC (isaac-ng.exe 0x80c030): the index of the first ChallengeParam with that name, else -1. Switch keeps the
-- same vector (vanilla 0..45, then the mods' challenges.xml) at Manager+0x36740, 0xf0 bytes each, name std::string +0
-- (Menu_Custom_Challenge::Render)
function Isaac.GetChallengeIdByName(name)
  local m = C.managerptr()
  if type(name) ~= "string" or not m then return -1 end
  local b, e, p = peek(m, 0x36740, "i64"), peek(m, 0x36748, "i64"), peek(m, 0x36740, "p")
  if not b or not e or not p then return -1 end
  for i = 0, (e - b) // 0xf0 - 1 do
    if peek(p, i * 0xf0, "str") == name then return i end
  end
  return -1
end
-- GetCostumeIdByPath(anm2): PC walks the null items, compares Costume.Anm2Path exactly, returns the item's id. On Switch
-- ItemConfig::LoadCostumes turns each null costume (costumes2.xml, the mods' too) into a null item whose
-- Costume has the id at Item+0x80 and the anm2 path at Item+0x88: the null item's id is the NullItemID
function Isaac.GetCostumeIdByPath(path)
  if type(path) ~= "string" then return -1 end
  local ic = itemconfig_p()
  local b, e = peek(ic, 0x30, "i64"), peek(ic, 0x38, "i64")
  if not b or not e then return -1 end
  for i = (e - b) // 8 - 1, 0, -1 do  -- from the end, as PC (isaac-ng.exe 0x80c0b0): a mod's costume wins
    local it = getters.null(ic, i)
    if it and peek(it, 0x88, "str") == path then return peek(it, 4, "i32") end
  end
  return -1
end
function Isaac.GetChallenge() return peek(gp(), O.Game_Challenge, "i32") or 0 end
Isaac.Explode = stub("Isaac.Explode")

---------------------------------------------------------------- Level / Room / Game
local level = {}
function level:GetStage() return peek(gp(), 0, "i32") end
level.GetCurses = native(NSK .. "5Level9GetCursesEv", "up", "Level.GetCurses", 0)
local get_curses = level.GetCurses
function level:GetCurses() return get_curses(gp()) end
local abs_stage = native(NSK .. "5Level16GetAbsoluteStageEv", "ip", "Level.GetAbsoluteStage", 1)
function level:GetAbsoluteStage() return abs_stage(gp()) end
function level:GetCurrentRoomIndex() return peek(gp(), O.Game_RoomIdx, "i32") end
function level:GetStageType() return peek(gp(), O.Level_StageType, "i32") or 0 end
function level:GetDimension() return peek(gp(), O.Level_Dimension, "i32") or 0 end
function level:GetStartingRoomIndex() return peek(gp(), O.Level_StartRoom, "i32") or 0 end
function level:GetPreviousRoomIndex() return peek(gp(), O.Level_LastRoomIdx, "i32") or 0 end
function level:GetStateFlag(flag)
  return ((peek(gp(), O.Level_StateFlags, "i64") or 0) >> flag) & 1 == 1
end
for name, mangled in pairs({IsAltStage = "9IsAltPathEv", IsAscent = "15IsBackwardsPathEv",
                             IsPreAscent = "23IsBackwardsPathEntranceEv"}) do
  local f = native(NSK .. "5Level" .. mangled, "bp", "Level." .. name, false)
  level[name] = function() return f(gp()) end
end
local level_name = native(NS .. "5Level7GetNameEv", "Sp", "Level.GetName", "")
function level:GetName() return level_name(gp()) end
do
  local init_deal_room = native(NS .. "5Level24InitializeDevilAngelRoomEbb", "_pbb", "Level.InitializeDevilAngelRoom")
  function level:InitializeDevilAngelRoom(angel, devil) init_deal_room(gp(), angel and true or false, devil and true or false) end
end
do
  local curse_name = native(NS .. "5Level12GetCurseNameEv", "Sp", "Level.GetCurseName", "")
  function level:GetCurseName() return curse_name(gp()) end
  local angel = native(NS .. "4Room18GetAngelRoomChanceEv", "fp", "Room.GetAngelRoomChance", 0)
  local planetarium = native(NS .. "5Level20GetPlanetariumChanceEv", "fp", "Level.GetPlanetariumChance", 0)
  function level:GetAngelRoomChance() return angel(roomp()) end
  function level:GetPlanetariumChance() return planetarium(gp()) end
end

local desc_mt = {}
-- RoomConfig::Room (desc.Data) with its spawns, offsets from RoomConfig::parse_room_node / Room::Init
-- (PC isaac-ng.exe order: StageID, Mode, Type, Variant, Subtype, Name, Difficulty, weights, Doors, Spawns,
-- SpawnCount, Width, Height, Shape, OriginalVariant; std::string is 24 bytes here, pointers 8)
do
  local ROOM = {StageID = {0, "i32"}, Mode = {4, "i32"}, Type = {O.RoomData_Type, "i32"}, Variant = {0xc, "i32"},
    Subtype = {0x10, "i32"}, Name = {0x18, "str"}, Difficulty = {0x30, "i32"}, InitialWeight = {0x34, "f32"},
    Weight = {0x38, "f32"}, Doors = {0x40, "i32"}, SpawnCount = {0x58, "u16"}, Width = {0x5a, "u8"},
    Height = {0x5b, "u8"}, Shape = {O.RoomData_Shape, "i32"}, OriginalVariant = {0x78, "i32"}}
  -- RoomConfig::Spawn, 24 bytes: X i16 +0, Y i16 +2, Entry* +8, EntryCount u8 +16, SumWeights f32 +20
  -- RoomConfig::Entry, 12 bytes: Type u32 +0, Variant u16 +4, Subtype u16 +6, Weight f32 +8
  local SPAWN = {X = {0, "i16"}, Y = {2, "i16"}, EntryCount = {16, "u8"}, SumWeights = {20, "f32"}}
  local ENTRY = {Type = {0, "i32"}, Variant = {4, "u16"}, Subtype = {6, "u16"}, Weight = {8, "f32"}}
  local function struct(name, fields, extra)
    local mt = {__type = name}
    mt.__index = function(t, k)
      local f = fields[k]
      if f then return peek(rawget(t, "_p"), f[1], f[2]) end
      local e = extra and extra[k]
      if e then return e(t) end
      C.missing(name .. "." .. tostring(k))
    end
    mt.__newindex = function(t, k, v)
      local f = fields[k]
      if f and f[2] ~= "str" and type(v) == "number" then
        return poke(rawget(t, "_p"), f[1], f[2], f[2] == "f32" and v or math.tointeger(v) or 0)
      end
      rawset(t, k, v)
    end
    return function(p) return p and setmetatable({_p = p}, mt) or nil end
  end
  -- CppContainer::ArrayProxy: Get(i) from 0, Size, #
  local function array(name, base, count, stride, wrap)
    local mt = {__type = name}
    mt.__len = function() return count end
    mt.__index = function(_, k)
      if k == "Size" then return count end
      if k == "Get" then
        return function(_, i)
          i = math.tointeger(i)
          if base and i and i >= 0 and i < count then return wrap(offset(base, i * stride)) end
        end
      end
      C.missing(name .. "." .. tostring(k))
    end
    return setmetatable({}, mt)
  end
  local wrap_entry = struct("RoomConfig_Entry", ENTRY)
  local wrap_spawn
  wrap_spawn = struct("RoomConfig_Spawn", SPAWN, {
    Entries = function(t)
      local p = rawget(t, "_p")
      return array("RoomConfigEntries", peek(p, 8, "p"), peek(p, 16, "u8") or 0, 12, wrap_entry)
    end,
    -- PickEntry(r): the entry where the running weight sum passes r * SumWeights, else the first
    -- (RoomConfig::Spawn::PickEntry)
    PickEntry = function(t)
      return function(self, r)
        local p = rawget(self, "_p")
        local e, n = peek(p, 8, "p"), peek(p, 16, "u8") or 0
        if not e or n == 0 then return nil end
        local target, sum = (tonumber(r) or 0) * (peek(p, 20, "f32") or 0), 0
        for i = 0, n - 1 do
          sum = sum + (peek(offset(e, i * 12), 8, "f32") or 0)
          if sum > target then return wrap_entry(offset(e, i * 12)) end
        end
        return wrap_entry(e)
      end
    end})
  desc_mt.wrap_data = struct("RoomConfig_Room", ROOM, {
    Spawns = function(t)
      local p = rawget(t, "_p")
      return array("RoomConfigSpawns", peek(p, 0x50, "p"), peek(p, 0x58, "u16") or 0, 24, wrap_spawn)
    end})
end
local empty_desc = C.buf(0x100)  -- what the room calls see before a run (all zero, as an unused slot)
local DESC_FLAG_FIELDS = {Clear = 1, PressurePlatesTriggered = 2, SacrificeDone = 4, ChallengeDone = 8,
  SurpriseMiniboss = 16, HasWater = 32, AltBossMusic = 64, NoReward = 128}
local DESC_FIELDS = {GridIndex = {O.Desc_Grid, "i32"}, SafeGridIndex = {O.Desc_SafeGrid, "i32"},
  DecorationSeed = {O.Desc_DecorationSeed, "u32"}, SpawnSeed = {O.Desc_SpawnSeed, "u32"},
  -- after Flags +0x50 as on PC (PC + 0xc, like the seeds): money spent, clear count, greed subtype, poop/pits
  TotalMoneySpent = {0x54, "i16"}, ClearCount = {0x56, "u16"}, GreedSubType = {0x58, "i32"}, PoopCount = {0x5c, "i16"},
  PitsCount = {0x60, "i32"}, AllowedDoors = {0x24, "i32"},  -- AllowedDoors: just before Doors[8] (inferred)
  -- the last four, as at the end of PC's descriptor (0x100 bytes here; inferred)
  ShopItemIdx = {0xf4, "i16"}, ShopItemDiscountIdx = {0xf6, "i16"}, DeliriumDistance = {0xf8, "i32"},
  AwardSeed = {O.Desc_AwardSeed, "u32"},
  ListIndex = {O.Desc_List, "i32"}, DisplayFlags = {O.Desc_Display, "i32"},
  VisitedCount = {O.Desc_Visited, "i32"}, Flags = {O.Desc_Flags, "i32"}}
function desc_mt.__index(t, k)
  local p = rawget(t, "_p")
  if not C.mem_ok(p, 0x100) then p = empty_desc end  -- a descriptor a mod kept from a level that is gone
  local f = DESC_FIELDS[k]
  if f then return peek(p, f[1], f[2]) end
  if k == "Data" or k == "OverrideData" then  -- OverrideData: the RoomConfig::Room* after Data
    local d = peek(p, O.Desc_Data + (k == "Data" and 0 or 8), "p")
    return desc_mt.wrap_data(d)
  end
  local bit = DESC_FLAG_FIELDS[k]  -- RoomDescriptor flags (Room::TriggerClear sets bit 0)
  if bit then return ((peek(p, O.Desc_Flags, "u32") or 0) & bit) ~= 0 end
  C.missing("RoomDescriptor." .. tostring(k))
end
-- writes reach the descriptor (each GetRoomByIdx call makes a new wrapper, so a stored value would be lost):
-- Data / OverrideData take another descriptor's Data (desc.Data = other.Data, as PC mods swap layouts)
function desc_mt.__newindex(t, k, v)
  local p = rawget(t, "_p")
  if p == empty_desc then return rawset(t, k, v) end
  local f = DESC_FIELDS[k]
  if f then return poke(p, f[1], f[2], math.tointeger(v) or 0) end
  if k == "Data" or k == "OverrideData" then
    return poke(p, O.Desc_Data + (k == "Data" and 0 or 8), "p", type(v) == "table" and rawget(v, "_p") or nil)
  end
  local bit = DESC_FLAG_FIELDS[k]
  if bit then
    local fl = peek(p, O.Desc_Flags, "u32") or 0
    return poke(p, O.Desc_Flags, "u32", v and (fl | bit) or (fl & ~bit))
  end
  rawset(t, k, v)
end
-- a descriptor the engine hands back before the level exists (mod load) can point at unmapped memory: use the empty one
local function wrap_desc(p)
  if not p then return nil end
  if not C.mem_ok(p, 0x100) then p = empty_desc end
  return setmetatable({_p = p}, desc_mt)
end
local get_room_by_idx = native(NSK .. "5Level12GetRoomByIdxEii", "ppii", "Level.GetRoomByIdx")
function level:GetRoomByIdx(idx, dim) return wrap_desc(get_room_by_idx(gp(), idx, dim or -1) or empty_desc) end
local cur_desc = native(NS .. "5Level18GetCurrentRoomDescEv", "pp", "Level.GetCurrentRoomDesc")
function level:GetCurrentRoomDesc() return wrap_desc(cur_desc(gp()) or empty_desc) end
local last_desc = native(NS .. "5Level15GetLastRoomDescEv", "pp", "Level.GetLastRoomDesc")
function level:GetLastRoomDesc() return wrap_desc(last_desc(gp())) end
function level:GetRooms()  -- RoomDescriptor[...] at Level+0x18, 0x100 bytes each
  local n = peek(gp(), O.Level_RoomCount, "i32") or 0
  -- #rooms is the size, as on PC (MinimapAPI:LoadDefaultMap loops "for i = 0, #rooms - 1": an empty map before)
  return setmetatable({Size = n, Get = function(_, i)
    if i < 0 or i >= n then return nil end
    return wrap_desc(offset(gp(), O.Level_Rooms + 0x100 * i))
  end}, {__len = function() return n end})
end
function game:GetLevel() return level end
function level:GetCurrentRoom() return room end

-- PC: the room is the mirrored Downpour / Dross II one (dimension 1 of a level that has it)
local has_mirror = native(NSK .. "5Level18HasMirrorDimensionEv", "bp", "Level.HasMirrorDimension", false)
function room:IsMirrorWorld() return level:GetDimension() == 1 and has_mirror(gp()) end
function room:GetType() return peek(roomp(), O.Room_Type, "i32") or 0 end
-- Repentance+ API: 1.7.9b has no water reflection pass, every render is a normal one
function room:GetRenderMode() return RenderMode.RENDER_NORMAL end
function room:GetGridWidth() return peek(roomp(), O.Room_Width, "i32") or 0 end
function room:GetGridHeight() return peek(roomp(), O.Room_Height, "i32") or 0 end
function room:GetGridSize() local r = roomp(); return r and peek(r, O.Room_Width, "i32") * peek(r, O.Room_Height, "i32") or 0 end
do
  local devil = native(NS .. "4Room18GetDevilRoomChanceEv", "fp", "Room.GetDevilRoomChance", 0)
  function room:GetDevilRoomChance() return devil(roomp()) end
end
do
  local in_room = native(NSK .. "4Room16IsPositionInRoomERKN4KAGE4Math7Vector2Ef", "bpVf", "Room.IsPositionInRoom", true)
  function room:IsPositionInRoom(pos, margin) return in_room(roomp(), pos, margin or 0) end
  -- Room:Update(): one more Room::Update (mods call it to refresh the room after grid changes)
  local upd, busy = native(NS .. "4Room6UpdateEv", "_p", "Room.Update", true), false
  function room:Update()
    if busy or not roomp() then return end
    busy = true
    local ok, e = pcall(upd, roomp())
    busy = false
    if not ok then error(e, 2) end
  end
end
function room:GetBackdropType() return peek(roomp(), 0x1fe0 + 0x1d0, "i32") or 0 end  -- Backdrop at Room+0x1fe0, type +0x1d0
-- inner corners of the room (PC: 60,140 and 580,420 in a 1x1 room)
function room:GetTopLeftPos() return self:GetGridPosition(self:GetGridWidth() + 1) - Vector(20, 20) end
function room:GetBottomRightPos() return self:GetGridPosition(self:GetGridSize() - self:GetGridWidth() - 2) + Vector(20, 20) end
function room:GetBossID() return peek(roomp(), O.Room_BossId, "i32") or 0 end
-- Room:SetRedHeartDamage: PC Room _redHeartDamage +0x120c; Switch Entity_Player::TakeDamage sets Room+0x1940 = 1
-- right after Level::SetRedHeartDamage (0x2a7ec4)
function room:SetRedHeartDamage() poke(roomp(), 0x1940, "u8", 1) end
-- GridEntity::Desc (32 bytes at +8): Type, Variant, State, SpawnCount, SpawnSeed, VarData, Initialized, VariableSeed
-- CollisionClass: PC +0x3c (isaac-ng.exe 0x807fc5), Switch +0x40 (GridEntity_Rock::update_collision stores 3/4 there)
local GRID_FIELDS = {State = {0x10, "i32"}, VarData = {0x1c, "i32"}, CollisionClass = {0x40, "i32"}}
local grid_mt = {}
local grid_cache = setmetatable({}, {__mode = "v"})  -- one proxy per engine GridEntity (== and GetPtrHash)
function grid_mt.__index(t, k)
  local m = rawget(grid_mt, k)
  if m then return m end
  if k == "Desc" then return C.grid_desc and C.grid_desc(rawget(t, "_p")) or t:GetSaveState() end
  local f = GRID_FIELDS[k]
  if f then return peek(rawget(t, "_p"), f[1], f[2]) end
  if k == "Position" then return t:GetPosition() end
  -- anything else reads as nil, as on PC (EID tells grid entities apart by Type == nil: not a missing field)
  if k ~= "Type" then C.missing("GridEntity." .. tostring(k)) end
end
do
local grid_pos = native(NSK .. "10GridEntity11GetPositionEv", "vp", "GridEntity.GetPosition")
function grid_mt:GetPosition() return grid_pos(rawget(self, "_p")) or Vector(0, 0) end
-- GridEntity::PostInit is virtual: its vtable slot is found once, from the classes that override it
local postinit_slot
local POSTINIT = {"10GridEntity", "18GridEntity_Gravity", "14GridEntity_Pit", "15GridEntity_Poop",
                  "15GridEntity_Rock", "17GridEntity_Statue", "15GridEntity_Wall"}
function grid_mt:PostInit()
  local p = rawget(self, "_p")
  local vt = peek(p, 0, "p")
  if not vt then return end
  if not postinit_slot then
    local fns = {}
    for _, c in ipairs(POSTINIT) do local f = sym(NS .. c .. "8PostInitEv"); if f then fns[#fns + 1] = f end end
    for i = 0, 63 do
      local f = peek(vt, 8 * i, "p")
      for _, g in ipairs(fns) do if f == g then postinit_slot = i end end
      if postinit_slot then break end
    end
    if not postinit_slot then C.missing("GridEntity.PostInit"); postinit_slot = false end
  end
  if postinit_slot then call(peek(vt, 8 * postinit_slot, "p"), "_p", p) end
end
-- GridEntity::Update is virtual too, overridden by every subclass at one slot (Fiend Folio reroll_grids:
-- SetType, State, then Update)
local update_slot
local UPDATE = {"21GridEntity_Decoration", "15GridEntity_Door", "15GridEntity_Fire", "18GridEntity_Gravity",
  "15GridEntity_Lock", "14GridEntity_Pit", "15GridEntity_Poop", "24GridEntity_PressurePlate", "15GridEntity_Rock",
  "17GridEntity_Spikes", "17GridEntity_Stairs", "17GridEntity_Statue", "21GridEntity_Teleporter", "14GridEntity_TNT",
  "19GridEntity_TrapDoor", "15GridEntity_Wall", "14GridEntity_Web"}
function grid_mt:Update()
  local p = rawget(self, "_p")
  local vt = peek(p, 0, "p")
  if not vt then return end
  if update_slot == nil then
    local fns = {}
    for _, c in ipairs(UPDATE) do local f = sym(NS .. c .. "6UpdateEv"); if f then fns[tostring(f)] = true end end
    for i = 0, 63 do
      if fns[tostring(peek(vt, 8 * i, "p"))] then update_slot = i; break end
    end
    if not update_slot then C.missing("GridEntity.Update"); update_slot = false end
  end
  if update_slot then call(peek(vt, 8 * update_slot, "p"), "_p", p) end
end
end
function grid_mt:GetType() return peek(rawget(self, "_p"), O.Grid_Type, "i32") end
function grid_mt:GetVariant() return peek(rawget(self, "_p"), 0xc, "i32") end
-- PC GridEntity::SetType (0x6dad50) / SetVariant (0x705cd0) only store the field (PC +4/+8, Switch +8/+0xc)
function grid_mt:SetType(t) poke(rawget(self, "_p"), O.Grid_Type, "i32", math.floor(t)) end
function grid_mt:SetVariant(v) poke(rawget(self, "_p"), 0xc, "i32", math.floor(v)) end
function grid_mt:GetSaveState()
  local p = rawget(self, "_p")
  return {Type = peek(p, 0x8, "i32"), Variant = peek(p, 0xc, "i32"), State = peek(p, 0x10, "i32"),
    SpawnCount = peek(p, 0x14, "i32"), SpawnSeed = peek(p, 0x18, "u32"), VarData = peek(p, 0x1c, "i32"),
    Initialized = peek(p, 0x20, "b"), VariableSeed = peek(p, 0x24, "u32")}
end
function grid_mt:GetGridIndex() return rawget(self, "_i") end
function grid_mt:ToPressurePlate() return nil end
do
  local X = {sprite = debug.getregistry()["isaaclua.grid_sprite"]}
  function grid_mt:GetSprite() return X.sprite(rawget(self, "_p")) end
  -- To<class>: the grid itself when it is of that type (one proxy class for all of them)
  X.to = {ToPit = "GRID_PIT", ToRock = "GRID_ROCK", ToPoop = "GRID_POOP", ToSpikes = "GRID_SPIKES",
    ToTNT = "GRID_TNT", ToWall = "GRID_WALL", ToWeb = "GRID_SPIDERWEB", ToStatue = "GRID_STATUE"}
  for m, t in pairs(X.to) do
    grid_mt[m] = function(self) return self:GetType() == GridEntityType[t] and self or nil end
  end
  function grid_mt:ToRock()
    local t = self:GetType()
    for _, n in ipairs({"GRID_ROCK", "GRID_ROCKT", "GRID_ROCK_BOMB", "GRID_ROCK_ALT", "GRID_ROCK_SS", "GRID_ROCK_SPIKED",
                        "GRID_ROCK_ALT2", "GRID_ROCK_GOLD", "GRID_ROCKB", "GRID_PILLAR"}) do
      if GridEntityType[n] == t then return self end
    end
  end
  -- GridEntityRock big-rock frame (Switch GridEntity_Rock Get/SetBigRockFrame, update_anim_frame = PC UpdateAnimFrame)
  X.get_brf = native(NSK .. "15GridEntity_Rock15GetBigRockFrameEv", "ip", "GridEntityRock.GetBigRockFrame", -1)
  X.set_brf = native(NS .. "15GridEntity_Rock15SetBigRockFrameEi", "_pi", "GridEntityRock.SetBigRockFrame")
  X.upd_anim = native(NS .. "15GridEntity_Rock17update_anim_frameEv", "_p", "GridEntityRock.UpdateAnimFrame")
  function grid_mt:GetBigRockFrame() if self:ToRock() then return X.get_brf(rawget(self, "_p")) end return -1 end
  function grid_mt:SetBigRockFrame(n) if self:ToRock() then X.set_brf(rawget(self, "_p"), math.floor(n)) end end
  function grid_mt:UpdateAnimFrame() if self:ToRock() then X.upd_anim(rawget(self, "_p")) end end
  -- GridEntityPit:MakeBridge(source): Switch GridEntity_Pit::MakeBridge(GridEntity*) (Fiend Folio's pet rock)
  X.bridge = native(NS .. "14GridEntity_Pit10MakeBridgeEPNS_10GridEntityE", "_pp", "GridEntityPit.MakeBridge")
  function grid_mt:MakeBridge(src)
    if self:ToPit() then X.bridge(rawget(self, "_p"), src and rawget(src, "_p") or nil) end
  end
end
function room:GetGridEntity(i)
  local g
  if C.grid_at then  -- (FF and StageAPI walk the whole grid every update)
    g = C.grid_at(i)
  else
    local r = roomp()
    if not r or i < 0 or i >= self:GetGridSize() then return nil end
    g = peek(r, O.Room_Grid + 8 * i, "p")
  end
  if not g then return nil end
  local w = grid_cache[g]
  if not w or rawget(w, "_i") ~= i then w = setmetatable({_p = g, _i = i}, grid_mt); grid_cache[g] = w end
  return w
end
function room:GetGridEntityFromPos(pos) return self:GetGridEntity(self:GetGridIndex(pos)) end
local spawn_grid = native(NS .. "4Room15SpawnGridEntityEiNS_15eGridEntityTypeEjji", "bpiiuui", "Room.SpawnGridEntity", false)
function room:SpawnGridEntity(i, gtype, variant, seed, vardata)
  return spawn_grid(roomp(), i, gtype, variant or 0, seed or Random(), vardata or 0)
end
local remove_grid = native(NS .. "4Room16RemoveGridEntityEiib", "_piib", "Room.RemoveGridEntity")
function room:RemoveGridEntity(i, path_trail, keep_decoration) remove_grid(roomp(), i, path_trail or 0, keep_decoration or false) end
function Isaac.GridSpawn(gtype, variant, pos, forced)
  local i = room:GetGridIndex(pos)
  if i < 0 then return nil end
  if forced and room:GetGridEntity(i) then room:RemoveGridEntity(i, 0, false) end
  room:SpawnGridEntity(i, gtype, variant, Random(), 0)
  return room:GetGridEntity(i)
end
local grid_position = native_or(NSK .. "4Room15GetGridPositionEi", "vpi", function(_, i)
  local w = room:GetGridWidth()
  if w == 0 then return Vector(0, 0) end
  return Vector(40 * (i % w) + 40, 40 * (i // w) + 120)
end)
function room:GetGridPosition(i) return grid_position(roomp(), i) end
local grid_index = native_or(NSK .. "4Room12GetGridIndexERKN4KAGE4Math7Vector2E", "ipV", function(_, pos)
  local w = room:GetGridWidth()
  local x, y = math.floor((pos.X - 20) / 40), math.floor((pos.Y - 100) / 40)
  if w == 0 or x < 0 or y < 0 or x >= w or y >= room:GetGridHeight() then return -1 end
  return y * w + x
end)
function room:GetGridIndex(pos) return grid_index(roomp(), pos) end
function room:GetGridPath(i) return peek(roomp(), O.Room_GridPath + 4 * i, "u32") or 0 end
function room:WorldToScreenPosition(pos) return Isaac.WorldToScreen(pos) end
local mirrored = native(NSK .. "4Room19UseMirroredGraphicsEv", "bp", "Room.IsMirrorWorld", false)
function room:IsMirrorWorld() return mirrored(roomp()) end
local ambush = native(NSK .. "4Room14IsAmbushActiveEv", "bp", "Room.IsAmbushActive", false)
function room:IsAmbushActive() return ambush(roomp()) end
-- not exported on Switch: the middle of the room's inner area (Vector(320, 280) in a 1x1 room)
local center = native_or(NSK .. "4Room12GetCenterPosEv", "vp", function()
  return (room:GetTopLeftPos() + room:GetBottomRightPos()) / 2
end)
function room:GetCenterPos() return center(roomp()) end
local free_tile = native(NSK .. "4Room20FindFreeTilePositionERKN4KAGE4Math7Vector2Ef", "vpVf", "Room.FindFreeTilePosition")
function room:FindFreeTilePosition(pos, dist) return free_tile(roomp(), pos, dist or 0) or pos end
local random_pos = native(NS .. "4Room17GetRandomPositionEf", "vpf", "Room.GetRandomPosition")
function room:GetRandomPosition(margin) return random_pos(roomp(), margin or 0) or room:GetCenterPos() end
local last_boss = native(NSK .. "4Room21IsCurrentRoomLastBossEv", "bp", "Room.IsCurrentRoomLastBoss", false)
function room:IsCurrentRoomLastBoss() return last_boss(roomp()) end
local function room_desc() return peek(roomp(), O.Room_Desc, "p") end
-- Ambush::Update sets RoomDescriptor flag 8 (FLAG_CHALLENGE_DONE) when the waves are over
do
  local mist = native(NSK .. "4Room12HasCurseMistEv", "bp", "Room.HasCurseMist", false)
  function room:HasCurseMist() return mist(roomp()) end
end
function room:IsAmbushDone() return ((peek(room_desc(), O.Desc_Flags, "u32") or 0) & 8) ~= 0 end
function room:IsClear() return ((peek(room_desc(), O.Desc_Flags, "u32") or 0) & 1) ~= 0 end
function room:IsInitialized() return room_desc() ~= nil end  -- Room::Init sets the descriptor
-- EntityList (0-based Get, Size) over the room's entities, as returned when called
function room:GetEntities()
  local t = Isaac.GetRoomEntities()
  return setmetatable({Size = #t, Get = function(_, i) return t[i + 1] end}, {__len = function() return #t end})
end
function room:IsFirstVisit() return (peek(roomp(), O.Room_FirstVisit, "u8") or 0) ~= 0 end
function room:GetDecorationSeed() return peek(room_desc(), O.Desc_DecorationSeed, "u32") or 0 end
function room:GetSpawnSeed() return peek(room_desc(), O.Desc_SpawnSeed, "u32") or 0 end
function room:GetAwardSeed() return peek(room_desc(), O.Desc_AwardSeed, "u32") or 0 end
function room:GetRoomShape()
  local d = peek(room_desc(), O.Desc_Data, "p")
  return d and peek(d, O.RoomData_Shape, "i32") or 1
end
function room:GetRenderScrollOffset() return peek(roomp(), O.Room_ScrollOffset, "vec") or Vector(0, 0) end
local function room_count(o)
  local r = roomp()
  return math.max(0, (peek(r, o, "i32") or 0) + (peek(r, o + 4, "i32") or 0))
end
function room:GetAliveEnemiesCount() return room_count(O.Room_Enemies) end
function room:GetAliveBossesCount() return room_count(O.Room_Bosses) end

local game_fields = {
  Difficulty = function() return peek(gp(), O.Game_Difficulty, "i32") or 0 end,
  Challenge = function() return peek(gp(), O.Game_Challenge, "i32") or 0 end,
  TimeCounter = function() return peek(gp(), O.Game_TimeCounter, "i32") or 0 end,
  BossRushParTime = function() return peek(gp(), O.Game_BossRushParTime, "i32") or 0 end,
  BlueWombParTime = function() return peek(gp(), O.Game_BlueWombParTime, "i32") or 0 end,
  -- Game+0x24f9b0: reset by Game::Update when the shake timer (+0x24f9ac) ends, added by WorldToScreenPosition
  ScreenShakeOffset = function() return peek(gp(), 0x24f9b0, "vec") or Vector(0, 0) end,
}
setmetatable(game, {__index = function(_, k)
  local f = game_fields[k]
  if f then return f() end
  -- Game has no other fields on PC: an unknown name is a method, which reports itself and returns nil
  return stub("Game." .. tostring(k))
end})
do
local shake = native(NS .. "4Game11ShakeScreenEi", "_pi", "Game.ShakeScreen")
function game:ShakeScreen(frames) shake(gp(), frames or 0) end
end
local paused = native(NSK .. "4Game8IsPausedEv", "bp", "Game.IsPaused", false)
function game:IsPaused() return paused(gp()) end
local hard = native(NSK .. "4Game10IsHardModeEv", "bp", "Game.IsHardMode", false)
function game:IsHardMode() return hard(gp()) end
function game:GetVictoryLap() return peek(gp(), O.Game_VictoryLap, "i32") or 0 end
function game:GetStateFlag(flag) return ((peek(gp(), O.Game_StateFlags, "i64") or 0) >> flag) & 1 == 1 end
function game:SetStateFlag(flag, v)
  local f = peek(gp(), O.Game_StateFlags, "i64")
  if f then poke(gp(), O.Game_StateFlags, "i64", v and f | (1 << flag) or f & ~(1 << flag)) end
end
-- HUD = Game+0xe8468 (Game::Render passes it to HUD::Render), which draws nothing while the byte at
-- HUD+0xc53fa is 0. SetVisible(true) puts back what SetVisible(false) found.
local HUD_VISIBLE = 0xe8468 + 0xc53fa
local hud = setmetatable({}, {__index = function(_, k) return stub("HUD." .. tostring(k)) end})
local hud_saved
function hud:IsVisible() return (peek(gp(), HUD_VISIBLE, "u8") or 1) ~= 0 end
function hud:SetVisible(v)
  local g = gp()
  if not g then return end
  if not v then
    local cur = peek(g, HUD_VISIBLE, "u8")
    if cur ~= 0 then hud_saved = cur end
    poke(g, HUD_VISIBLE, "u8", 0)
  elseif hud_saved then
    poke(g, HUD_VISIBLE, "u8", hud_saved)
    hud_saved = nil
  end
end
function game:GetHUD() return hud end
-- HUD methods on the game's HUD (Game+0xe8468)
do
  local H = NS .. "3HUD"
  local function hp() local g = gp(); return g and offset(g, 0xe8468) end
  local function hfn(name, sig, label)
    local f = native(H .. name, sig, "HUD." .. label)
    return function(...) local h = hp(); if h then return f(h, ...) end end
  end
  local flash, inval = hfn("14FlashChargeBarEPNS_13Entity_PlayerEi", "_ppi", "FlashChargeBar"),
    hfn("20InvalidateActiveItemEPNS_13Entity_PlayerEi", "_ppi", "InvalidateActiveItem")
  function hud:FlashChargeBar(player, slot) if player then flash(ptr(player), slot or 0) end end
  function hud:InvalidateActiveItem(player, slot) if player then inval(ptr(player), slot or 0) end end
  local craft = hfn("22InvalidateCraftingItemEPNS_13Entity_PlayerE", "_pp", "InvalidateCraftingItem")
  function hud:InvalidateCraftingItem(player) if player then craft(ptr(player)) end end
  local assign, post, upd = hfn("16AssignPlayerHUDsEv", "_p", "AssignPlayerHUDs"), hfn("10PostUpdateEv", "_p", "PostUpdate"),
    hfn("6UpdateEv", "_p", "Update")
  function hud:AssignPlayerHUDs() assign() end
  function hud:PostUpdate() post() end
  function hud:Update() upd() end
  -- ShowItemText(player, ItemConfigItem) or (main, secondary = "", isCurse = false, autoHide = true)
  local text = hfn("12ShowItemTextEPKcS2_bb", "_pssbb", "ShowItemText")
  local item_text = hfn("12ShowItemTextEPNS_13Entity_PlayerEPKNS_10ItemConfig4ItemE", "_ppp", "ShowItemText")
  function hud:ShowItemText(a, b, curse, auto_hide)
    if type(a) == "string" then
      text(a, b or "", curse or false, auto_hide ~= false)
    elseif a and type(b) == "table" and rawget(b, "_p") then
      item_text(ptr(a), rawget(b, "_p"))
    end
  end
  local fortune = hfn("15ShowFortuneTextEPKcS2_S2_", "_psss", "ShowFortuneText")
  function hud:ShowFortuneText(a, b, c) fortune(a or "", b or "", c or "") end
end
do
  local show_halluc = native(NS .. "4Game17ShowHallucinationEiNS_8Backdrop9eBackdropE", "_pii", "Game.ShowHallucination")
  function game:ShowHallucination(frames, backdrop) show_halluc(gp(), frames or 0, backdrop or 0) end
end

local seeds = {}
function seeds:GetStartSeed() return peek(gp(), O.Game_Seeds + 4, "u32") or 0 end
function seeds:IsCustomRun() return (peek(gp(), O.Game_Seeds, "u8") or 0) ~= 0 end
function seeds:GetStageSeed(stage) return peek(gp(), O.Game_Seeds + 0x18 + 4 * stage, "u32") or 0 end
-- std::set<eSeedEffect> at Seeds+0x58: red-black tree, root at +0x60, node {left, right, parent, color, value +0x1c}
function seeds:HasSeedEffect(effect)
  local n = peek(gp(), O.Game_Seeds + 0x60, "p")
  for _ = 1, 64 do
    if not n then return false end
    local v = peek(n, 0x1c, "i32")
    if v == effect then return true end
    n = peek(n, effect < v and 0 or 8, "p")
  end
  return false
end
function seeds:AddSeedEffect(effect) if gp() then C.seed_effect(offset(gp(), O.Game_Seeds), effect, true) end end
function seeds:RemoveSeedEffect(effect) if gp() then C.seed_effect(offset(gp(), O.Game_Seeds), effect, false) end end
function seeds:ClearSeedEffects()
  if not gp() then return end
  for e = 1, 80 do if seeds:HasSeedEffect(e) then C.seed_effect(offset(gp(), O.Game_Seeds), e, false) end end
end
function seeds:CountSeedEffects() return peek(gp(), O.Game_Seeds + 0x68, "i32") or 0 end
local seed2string = native_or(NS .. "5Seeds11Seed2StringEj", "Su", function(seed)
  return (string.format("%08X", seed):gsub("^(....)", "%1 "))
end)
function seeds:GetStartSeedString() return seed2string(self:GetStartSeed()) end
seeds.Seed2String = function(seed) return seed2string(seed) end
Seeds = {Seed2String = seeds.Seed2String}
function game:GetSeeds() return seeds end

local itempool = {}
local function poolp() return offset(gp(), O.Game_ItemPool) end
function itempool:GetPillEffect(color)
  local c = (color or 0) & 0x7ff
  if c <= 0 or c >= 15 then return -1 end
  return peek(poolp(), O.Pool_PillEffects + 4 * c, "i32")
end
function itempool:IsPillIdentified(color)
  local c = (color or 0) & 0x7ff
  if c <= 0 or c >= 15 then return false end
  return (peek(poolp(), O.Pool_PillIdentified + c, "u8") or 0) ~= 0
end
function itempool:GetLastPool() return peek(poolp(), O.Pool_LastPool, "i32") or 0 end
function itempool:GetNumItemPools() return 31 end
local pool_for_room = native(NSK .. "8ItemPool14GetPoolForRoomENS_9eRoomTypeEj", "ipiu", "ItemPool.GetPoolForRoom", 0)
function itempool:GetPoolForRoom(rtype, seed) return pool_for_room(poolp(), rtype, seed or 1) end
do
local IP = NS .. "8ItemPool"
-- the game's own call: GetCollectible(pool, seed, flags, default), flag 1 = keep the item in the pool
local pool_get_coll = native(IP .. "14GetCollectibleENS0_13eItemPoolTypeEjjNS_16eCollectibleTypeE", "ipiuui",
                             "ItemPool.GetCollectible", 0)
function itempool:GetCollectible(t, decrease, seed, def)
  return pool_get_coll(poolp(), t or 0, seed or math.random(1, 0x7fffffff), decrease and 0 or 1, def or 0)
end
local pool_get_trinket = native(IP .. "10GetTrinketEb", "ipb", "ItemPool.GetTrinket", 0)
function itempool:GetTrinket(dont_advance) return pool_get_trinket(poolp(), dont_advance) end
local pool_remove_trinket = native(IP .. "13RemoveTrinketENS_12eTrinketTypeE", "bpi", "ItemPool.RemoveTrinket", false)
function itempool:RemoveTrinket(t) return pool_remove_trinket(poolp(), t) end
local pool_remove_coll = native(IP .. "17RemoveCollectibleENS_16eCollectibleTypeEbb", "bpibb", "ItemPool.RemoveCollectible", false)
function itempool:RemoveCollectible(t) return pool_remove_coll(poolp(), t, false, false) end
local pool_reset_trinkets = native(IP .. "13ResetTrinketsEv", "_p", "ItemPool.ResetTrinkets")
function itempool:ResetTrinkets() pool_reset_trinkets(poolp()) end
local pool_get_card = native(NSK .. "8ItemPool7GetCardEjbbb", "ipubbb", "ItemPool.GetCard", 0)
function itempool:GetCard(seed, playing, rune, only_runes) return pool_get_card(poolp(), seed, playing, rune, only_runes) end
local pool_get_pill = native(NSK .. "8ItemPool7GetPillEj", "ipu", "ItemPool.GetPill", 0)
function itempool:GetPill(seed) return pool_get_pill(poolp(), seed) end
local pool_force_pill = native(IP .. "18ForceAddPillEffectENS_11ePillEffectE", "ipi", "ItemPool.ForceAddPillEffect", 0)
function itempool:ForceAddPillEffect(effect) return pool_force_pill(poolp(), effect) end
end
setmetatable(itempool, {__index = function(_, k) return stub("ItemPool." .. tostring(k)) end})
function game:GetItemPool() return itempool end

---------------------------------------------------------------- Isaac.*
function Isaac.GetTime() return C.ms() end
function Isaac.IsInGame() return C.gameptr() ~= nil end
-- ItemOverlay.Show(giantbook, delay = 3, player) (PC global): the Game's ItemOverlay is Game+0x25db0 on Switch
-- (Switch callers 0xb9670, 0x10c358); its table holds giantbook.xml ids 0-46 (Show 0x3c5034 rejects higher)
ItemOverlay = ItemOverlay or {}
function ItemOverlay.Show(id, delay, player)
  local g, f = C.gameptr(), sym(NS .. "11ItemOverlay4ShowENS0_10eOverlayIDEiPNS_13Entity_PlayerE")
  if not g then return end
  if not f then C.missing("ItemOverlay.Show"); return end
  id = math.tointeger(id) or 0
  if id < 0 or id > 46 then return end
  call(f, "_piip", offset(g, 0x25db0), id, math.tointeger(delay) or 3, player and ptr(player) or nil)
end
local PARTITION = {[1] = {[3] = true}, [2] = {[9] = true}, [4] = {[2] = true}, [16] = {[5] = true},
  [32] = {[1] = true}, [64] = {[1000] = true}}
local function in_partition(e, mask)
  if not mask or mask < 0 or mask == 0xffffffff then return true end
  local t = e.Type
  for bit, types in pairs(PARTITION) do
    if mask & bit ~= 0 and types[t] then return true end
  end
  return mask & 8 ~= 0 and npc_type(t)
end
function Isaac.FindInRadius(pos, radius, partitions)
  local out = {}
  for _, e in ipairs(Isaac.GetRoomEntities()) do
    if in_partition(e, partitions) and e.Position:Distance(pos) <= radius then out[#out + 1] = e end
  end
  return out
end
-- the engine's EntityList::QueryRadius when the game has it (Switch): PC's own query, and no Lua loop
if C.has_query_radius and C.has_query_radius() then
  local engine = Isaac.FindInRadius_engine
  function Isaac.FindInRadius(pos, radius, partitions) return engine(pos, radius, partitions) end
end
Isaac.FindInRadius_engine = nil
local function count_enemies(boss_only)
  local n = 0
  for _, e in ipairs(Isaac.GetRoomEntities()) do
    if npc_type(e.Type) and not e:IsDead() and (not boss_only or e:IsBoss()) then n = n + 1 end
  end
  return n
end
function Isaac.CountEnemies() return count_enemies(false) end
function Isaac.CountBosses() return count_enemies(true) end
-- Room::WorldToScreenPosition = GetRenderPosition(pos) + Room render scroll offset + screen shake; the
-- render position is without the last two (rooms bigger than the Switch's screen scroll, greed mode)
function Isaac.WorldToRenderPosition(pos)
  local off = room:GetRenderScrollOffset() + game.ScreenShakeOffset
  if C.ui_pc and C.ui_pc() then off = Vector(C.ui_unscale(off.X, off.Y)) end
  return Isaac.WorldToScreen(pos) - off
end
local moddata_file = {}
-- the Mod methods themselves: a mod table may have its own SaveData field (Doki Doki Repentance)
function Isaac.LoadModData(mod) return Mod_mt.LoadData(mod) end
function Isaac.SaveModData(mod, s) Mod_mt.SaveData(mod, s) end
function Isaac.HasModData(mod) return Mod_mt.HasData(mod) end
function Isaac.RemoveModData(mod) Mod_mt.RemoveData(mod) end

local action_value = native_or(NS .. "7Manager14GetActionValueEjjPNS_6EntityE", "fpuup", function(_, action, ctrl)
  return Input.IsActionPressed(action, ctrl) and 1 or 0
end)
-- a PC controller index (0-4) no player has on Switch means "any controller" (-1), as in api.c
local function switch_ctrl(ctrl)
  ctrl = ctrl or -1
  if ctrl == -1 then return -1 end
  for i = 0, Game():GetNumPlayers() - 1 do
    local p = Isaac.GetPlayer(i)
    if p and p.ControllerIndex == ctrl then return ctrl end
  end
  return -1
end
function Input.GetActionValue(action, ctrl) return action_value(C.managerptr(), action, switch_ctrl(ctrl), nil) end

-- Move actions (left stick). The Switch stick is analog and the game reports a move action "pressed"
-- from a small push, so a stick that drifts a little reads as walking: GoodTrip then hides its map
-- cursor every other frame. For mods a move action is pressed from half a push (the PC gamepad dead
-- zone), and "triggered" is the frame it becomes pressed.
-- Mod Config Menu also steps through its pages with them: on PC from a d-pad or keys; on the Switch
-- stick, "down" a little sideways would also read as left/right and change the option under the cursor,
-- so for MCM a move action counts only along the stick's main axis.
local FOUR_WAY = {["Mod Config Menu"] = true, ["Mod Config Menu (Standalone)"] = true}
local MOVE_AXIS = {[0] = {0, 1, 2, 3}, [1] = {0, 1, 2, 3}, [2] = {2, 3, 0, 1}, [3] = {2, 3, 0, 1}}
local MOVE_DEADZONE = 0.5
local native_pressed, native_triggered = Input.IsActionPressed, Input.IsActionTriggered
local function move_pressed(action, ctrl)
  if not native_pressed(action, ctrl) then return false end
  local v = Input.GetActionValue
  if v(action, ctrl) < MOVE_DEADZONE then return false end
  if running_mod and FOUR_WAY[running_mod.Name] then
    local ax = MOVE_AXIS[action]
    return math.max(v(ax[1], ctrl), v(ax[2], ctrl)) >= math.max(v(ax[3], ctrl), v(ax[4], ctrl))
  end
  return true
end
local move_state = {}  -- [ctrl * 4 + action] = {frame, pressed now, pressed the frame before}
local function move_edge(action, ctrl)
  local key = (switch_ctrl(ctrl) + 1) * 4 + action
  local st = move_state[key]
  if not st then st = {-1, false, false}; move_state[key] = st end
  local frame = Isaac.GetFrameCount()
  if st[1] ~= frame then
    st[3] = frame - st[1] <= 4 and st[2] or false  -- update callbacks see every second render frame
    st[1] = frame
    st[2] = move_pressed(action, ctrl)
  end
  return st[2] and not st[3]
end
function Input.IsActionPressed(action, ctrl)
  if MOVE_AXIS[action] then return move_pressed(action, ctrl) end
  return native_pressed(action, ctrl)
end
function Input.IsActionTriggered(action, ctrl)
  if MOVE_AXIS[action] then return move_edge(action, ctrl) end
  return native_triggered(action, ctrl)
end

-- The mouse is the touchscreen: one finger is the left button, two fingers the right one (Da Rules: edit
-- or delete), and the position (the first finger) stays where it lifted.
-- GoodTrip: tap a room on the minimap (map held) to travel, drag its menu by the bar on top.
local touch_pos = Vector(-10000, -10000)  -- off screen until the first touch
local function touch()
  local x, y, n = C.touch()
  if x then touch_pos = Vector(x, y) end
  return n or 0
end
local function screen_to_world(p)
  local o = Isaac.WorldToScreen(Vector(0, 0))
  local ex, ey = Isaac.WorldToScreen(Vector(100, 0)) - o, Isaac.WorldToScreen(Vector(0, 100)) - o
  local det = ex.X * ey.Y - ex.Y * ey.X
  if det == 0 then return p end
  local d = p - o
  return Vector((d.X * ey.Y - d.Y * ey.X) / det * 100, (ex.X * d.Y - ex.Y * d.X) / det * 100)
end
function Input.IsMouseBtnPressed(button)
  local n = touch()
  if button == 0 then return n == 1 end
  return button == 1 and n >= 2
end
function Input.GetMousePosition(gameCoords)
  touch()
  local p = Vector(touch_pos.X, touch_pos.Y)
  -- isaaclua.ini pc_screen: the touch position in the mod's 480x270, as WorldToScreen gives it
  if C.ui_pc and C.ui_pc() then p = Vector(C.ui_unscale(p.X, p.Y)) end
  if gameCoords then return screen_to_world(p) end
  return p
end
function Isaac.ScreenToWorld(p) return screen_to_world(p) end

---------------------------------------------------------------- Options, sound, misc
-- OptionsConfig (Manager+0x364d0), offsets from OptionsConfig::Save: {offset, type, default}
local OPTIONS = {MusicVolume = {0x18, "f32", 1}, SFXVolume = {0x1c, "f32", 1}, MapOpacity = {0x20, "f32", 1},
  Fullscreen = {0x24, "b", true}, Filter = {0x25, "b", false}, Gamma = {0x28, "f32", 1},
  Exposure = {0x2c, "f32", 1}, ControllerHotplug = {0x38, "b", true}, PopUps = {0x39, "b", true},
  FoundHUD = {0x3a, "b", true}, CameraStyle = {0x3c, "i32", 1}, ShowRecentItems = {0x40, "i32", 1},
  HUDOffset = {0x44, "f32", 1}, TryImportSave = {0x48, "b", false}, EnableMods = {0x49, "b", true},
  RumbleEnabled = {0x4a, "b", true}, ChargeBars = {0x4b, "b", true}, BulletVisibility = {0x4c, "b", false},
  AimLock = {0x4d, "b", false}, TouchMode = {0x50, "i32", 0}}
-- options the Switch build has no field for: kept here, so mods can read back what they set
local OPTIONS_LUA = {AnnouncerVoiceMode = 0, ConsoleFont = 0, FadedConsoleDisplay = false, SaveCommandHistory = false,
  MouseControl = false, PauseOnFocusLost = true, VSync = true, UseBorderlessFullscreen = false, WindowWidth = 1280,
  WindowHeight = 720, MaxScale = 99, MaxRenderScale = 99, ExtraHUDStyle = 0, JacobEsauControls = 0,
  BossHpOnBottom = false, StreamerMode = false, EnableColorCorrection = true, EnableLighting = true,
  EnableShockwave = true, EnableCausticEffects = true, EnableWaterSurfaceRendering = true, EnableFilter = false,
  EnableInterpolation = true, FullScreen = true, DebugConsoleEnabled = true, DisplayPopups = true}
Options = setmetatable({}, {__index = function(_, k)
  if k == "Language" then return C.language() end
  if OPTIONS_LUA[k] ~= nil then return OPTIONS_LUA[k] end
  local f = OPTIONS[k]
  if f then
    local v = peek(C.managerptr(), O.Manager_Options + f[1], f[2])
    if v == nil then return f[3] end
    return v
  end
  C.missing("Options." .. tostring(k))
end, __newindex = function(t, k, v)  -- PC lets mods change options (MCM sets HUDOffset)
  local f = OPTIONS[k]
  if f then
    local cast = f[2] == "i32" and math.tointeger(v) or v
    if cast ~= nil then poke(C.managerptr(), O.Manager_Options + f[1], f[2], cast) end
  elseif OPTIONS_LUA[k] ~= nil then
    OPTIONS_LUA[k] = v
    if k == "JacobEsauControls" and C.repplus_je then C.repplus_je(math.tointeger(v) or 0) end  -- REP+ option
  else rawset(t, k, v) end
end})

local function null_object(name)
  return setmetatable({}, {__index = function(_, k) return function() end end})
end
-- SFXManager = the Manager's SoundEffects (+0x364a8, Manager::PlaySound), MusicManager = its Music (+0x36068,
-- Ambush::Update). Methods not found here do nothing, as before.
local sfx, music = null_object("SFXManager"), null_object("MusicManager")
do
  local function manager_part(off) local m = C.managerptr(); return m and offset(m, off) or nil end
  local function sfxp() return manager_part(0x364a8) end
  local function musicp() return manager_part(0x36068) end
  local SE, MU = NS .. "12SoundEffects", NS .. "5Music"
  local play = native(SE .. "4PlayENS_12eSoundEffectEfibff", "_pifibff", "SFXManager.Play")
  local stop = native(SE .. "4StopENS_12eSoundEffectE", "_pi", "SFXManager.Stop")
  local playing = native(SE .. "9IsPlayingENS_12eSoundEffectE", "bpi", "SFXManager.IsPlaying", false)
  local volume = native(SE .. "12AdjustVolumeENS_12eSoundEffectEf", "_pif", "SFXManager.AdjustVolume")
  local pitch = native(SE .. "11AdjustPitchENS_12eSoundEffectEf", "_pif", "SFXManager.AdjustPitch")
  local pan = native(SE .. "9AdjustPanENS_12eSoundEffectEf", "_pif", "SFXManager.AdjustPanning")
  local stop_loops = native(SE .. "17StopLoopingSoundsEv", "_p", "SFXManager.StopLoopingSounds")
  local preload = native(SE .. "7PreloadENS_12eSoundEffectE", "_pi", "SFXManager.Preload")
  local ambient = native(SE .. "15SetAmbientSoundENS_12eSoundEffectEff", "_piff", "SFXManager.SetAmbientSound")
  local ambient_vol = native(SE .. "21GetAmbientSoundVolumeENS_12eSoundEffectE", "fpi", "SFXManager.GetAmbientSoundVolume", 0)
  local m = getmetatable(sfx)
  local methods = {
    Play = function(_, id, vol, delay, loop, p, pn)
      if id and id >= 0 then play(sfxp(), id, vol or 1, delay or 2, loop or false, p or 1, pn or 0) end
    end,
    Stop = function(_, id) stop(sfxp(), id) end,
    IsPlaying = function(_, id) return playing(sfxp(), id) end,
    AdjustVolume = function(_, id, v) volume(sfxp(), id, v) end,
    AdjustPitch = function(_, id, v) pitch(sfxp(), id, v) end,
    AdjustPanning = function(_, id, v) pan(sfxp(), id, v) end,
    StopLoopingSounds = function() stop_loops(sfxp()) end,
    Preload = function(_, id) preload(sfxp(), id) end,
    SetAmbientSound = function(_, id, v, p) ambient(sfxp(), id, v, p or 1) end,
    GetAmbientSoundVolume = function(_, id) return ambient_vol(sfxp(), id) end,
  }
  local fallback = m.__index
  m.__index = function(t, k) return methods[k] or fallback(t, k) end

  local mplay = native(MU .. "4PlayENS_6eMusicE", "_pi", "MusicManager.Play")
  local mplay_vol = native(MU .. "4PlayENS_6eMusicEf", "_pif", "MusicManager.Play")
  local crossfade = native(MU .. "9CrossfadeENS_6eMusicEf", "_pif", "MusicManager.Crossfade")
  local fadeout = native(MU .. "7FadeoutEf", "_pf", "MusicManager.Fadeout")
  local pause = native(MU .. "5PauseEv", "_p", "MusicManager.Pause")
  local resume = native(MU .. "6ResumeEv", "_p", "MusicManager.Resume")
  local enable_layer = native(MU .. "11EnableLayerEib", "_pib", "MusicManager.EnableLayer")
  local disable_layer = native(MU .. "12DisableLayerEi", "_pi", "MusicManager.DisableLayer")
  local queue = native(MU .. "5QueueENS_6eMusicE", "_pi", "MusicManager.Queue")
  local slide = native(MU .. "11VolumeSlideEff", "_pff", "MusicManager.VolumeSlide")
  local update_vol = native(MU .. "12UpdateVolumeEv", "_p", "MusicManager.UpdateVolume")
  local current = native(NSK .. "5Music17GetCurrentMusicIDEv", "ip", "MusicManager.GetCurrentMusicID", 0)
  local mm = getmetatable(music)
  local mmethods = {
    Play = function(_, id, vol)
      if not id or id < 0 then return end
      if vol then mplay_vol(musicp(), id, vol) else mplay(musicp(), id) end
    end,
    Crossfade = function(_, id, rate) if id and id >= 0 then crossfade(musicp(), id, rate or 0.08) end end,
    Fadeout = function(_, rate) fadeout(musicp(), rate or 0.08) end,
    Pause = function() pause(musicp()) end,
    Resume = function() resume(musicp()) end,
    EnableLayer = function(_, id, instant) enable_layer(musicp(), id or 0, instant or false) end,
    DisableLayer = function(_, id) disable_layer(musicp(), id or 0) end,
    Queue = function(_, id) if id and id >= 0 then queue(musicp(), id) end end,
    VolumeSlide = function(_, v, rate) slide(musicp(), v, rate or 0.08) end,
    UpdateVolume = function() update_vol(musicp()) end,
    GetCurrentMusicID = function() return current(musicp()) end,
  }
  local mfallback = mm.__index
  mm.__index = function(t, k) return mmethods[k] or mfallback(t, k) end
function SFXManager() return sfx end
function MusicManager() return music end
  local play_room_music = native(NS .. "4Room9PlayMusicEv", "_p", "Room.PlayMusic")
function room:PlayMusic() play_room_music(roomp()) end

-- Isaac.GetSoundIdByName / GetMusicIdByName: the game numbers the sounds and tracks of the mods' content/
-- after its own, in the order it read the mods (_isaaclua_content_dirs, set before the mods' Lua runs);
-- an entry with its own id= keeps it. Counted back from the size of the game's tables
-- (SoundEffects: vector at +0 of 496-byte entries; Music: vector at +984 of 112-byte entries).
local function content_names(file, tag)
  local out = {}
  for _, dir in ipairs(rawget(_G, "_isaaclua_content_dirs") or {}) do
    local xml = C.readfile("rom:/mods/" .. dir .. "/content/" .. file)
    if xml then
      xml = xml:gsub("<!%-%-.-%-%->", "")
      for attrs in xml:gmatch("<" .. tag .. "%s([^>]*)>") do
        local name = attrs:match('name%s*=%s*"([^"]*)"')
        local id = tonumber(attrs:match('id%s*=%s*"([^"]*)"') or "")
        if name then out[#out + 1] = {name = name, id = id} end
      end
    end
  end
  return out
end
local function id_table(file, tag, vec_ptr, vec_off, size)
  local names = content_names(file, tag)
  local p = vec_ptr()
  local b, e = p and peek(p, vec_off, "i64"), p and peek(p, vec_off + 8, "i64")
  local total = b and e and (e - b) // size or 0
  local new = 0
  for _, n in ipairs(names) do if not n.id then new = new + 1 end end
  local ids, k = {}, total - new
  for _, n in ipairs(names) do
    if n.id then ids[n.name] = n.id else ids[n.name] = k; k = k + 1 end
  end
  return ids
end
local sound_ids, music_ids
function Isaac.GetSoundIdByName(name)
  sound_ids = sound_ids or id_table("sounds.xml", "sound", sfxp, 0, 496)
  return sound_ids[name] or -1
end
function Isaac.GetMusicIdByName(name)
  music_ids = music_ids or id_table("music.xml", "track", musicp, 984, 112)
  return music_ids[name] or -1
end
end

-- RNG: the game's xorshift; shift table = IsaacRepentance::RNG::s_Shifts (81 triples)
local RNG_SHIFTS = {
  {1, 3, 10}, {1, 5, 16}, {1, 5, 19}, {1, 9, 29}, {1, 11, 6}, {1, 11, 16}, {1, 19, 3}, {1, 21, 20}, {1, 27, 27},
  {2, 5, 15}, {2, 5, 21}, {2, 7, 7}, {2, 7, 9}, {2, 7, 25}, {2, 9, 15}, {2, 15, 17}, {2, 15, 25}, {2, 21, 9},
  {3, 1, 14}, {3, 3, 26}, {3, 3, 28}, {3, 3, 29}, {3, 5, 20}, {3, 5, 22}, {3, 5, 25}, {3, 7, 29}, {3, 13, 7},
  {3, 23, 25}, {3, 25, 24}, {3, 27, 11}, {4, 3, 17}, {4, 3, 27}, {4, 5, 15}, {5, 3, 21}, {5, 7, 22}, {5, 9, 7},
  {5, 9, 28}, {5, 9, 31}, {5, 13, 6}, {5, 15, 17}, {5, 17, 13}, {5, 21, 12}, {5, 27, 8}, {5, 27, 21}, {5, 27, 25},
  {5, 27, 28}, {6, 1, 11}, {6, 3, 17}, {6, 17, 9}, {6, 21, 7}, {6, 21, 13}, {7, 1, 9}, {7, 1, 18}, {7, 1, 25},
  {7, 13, 25}, {7, 17, 21}, {7, 25, 12}, {7, 25, 20}, {8, 7, 23}, {8, 9, 23}, {9, 5, 14}, {9, 5, 25}, {9, 11, 19},
  {9, 21, 16}, {10, 9, 21}, {10, 9, 25}, {11, 7, 12}, {11, 7, 16}, {11, 17, 13}, {11, 21, 13}, {12, 9, 23},
  {13, 3, 17}, {13, 3, 27}, {13, 5, 19}, {13, 17, 15}, {14, 1, 15}, {14, 13, 15}, {15, 1, 29}, {17, 15, 20},
  {17, 15, 23}, {17, 15, 26},
}
local RNG_mt = {}
RNG_mt.__index = RNG_mt
RNG = setmetatable({}, {__class = RNG_mt, __call = function(_, seed, shift)  -- a class table, as on PC
  local r = setmetatable({seed = 2853650767, shift = 35}, RNG_mt)
  if seed then r:SetSeed(seed, shift or 35) end
  return r
end})
function RNG_mt:SetSeed(seed, shift)
  self.seed = seed & 0xffffffff
  if self.seed == 0 then self.seed = 1 end
  self.shift = shift or 35
end
function RNG_mt:GetSeed() return self.seed end
function RNG_mt:Next()
  local s = RNG_SHIFTS[(self.shift % #RNG_SHIFTS) + 1]
  local x = self.seed
  x = x ~ (x >> s[1])
  x = (x ~ (x << s[2])) & 0xffffffff
  x = x ~ (x >> s[3])
  self.seed = x
  return x
end
function RNG_mt:RandomInt(max) return max > 0 and self:Next() % max or 0 end
function RNG_mt:RandomFloat() return self:Next() * 2.3283061589829401E-10 end

-- os.date over os.time (UTC): "*t", "!*t" and the common strftime fields
function os.date(fmt, t)
  fmt = fmt or "%c"
  t = t or os.time()
  if fmt:sub(1, 1) == "!" then fmt = fmt:sub(2) end
  local days, rem = t // 86400, t % 86400
  local z = days + 719468
  local era = z // 146097
  local doe = z - era * 146097
  local yoe = (doe - doe // 1460 + doe // 36524 - doe // 146096) // 365
  local doy = doe - (365 * yoe + yoe // 4 - yoe // 100)
  local mp = (5 * doy + 2) // 153
  local d = doy - (153 * mp + 2) // 5 + 1
  local m = mp < 10 and mp + 3 or mp - 9
  local y = yoe + era * 400 + (m <= 2 and 1 or 0)
  local tt = {year = y, month = m, day = d, hour = rem // 3600, min = rem % 3600 // 60, sec = rem % 60,
    wday = (days + 4) % 7 + 1, yday = 0, isdst = false}
  if fmt == "*t" then return tt end
  local f = {Y = "%04d", m = "%02d", d = "%02d", H = "%02d", M = "%02d", S = "%02d"}
  local v = {Y = tt.year, m = tt.month, d = tt.day, H = tt.hour, M = tt.min, S = tt.sec}
  return (fmt:gsub("%%(.)", function(c)
    if c == "c" then return string.format("%04d-%02d-%02d %02d:%02d:%02d", y, m, d, tt.hour, tt.min, tt.sec) end
    if c == "%" then return "%" end
    return f[c] and string.format(f[c], v[c]) or ""
  end))
end

-- require("json"): the game ships scripts/json.lua on PC (json.encode / json.decode)
package.preload.json = function()
  local json = {}
  local esc = {['"'] = '\\"', ["\\"] = "\\\\", ["\b"] = "\\b", ["\f"] = "\\f", ["\n"] = "\\n", ["\r"] = "\\r", ["\t"] = "\\t"}
  local encodable = {["nil"] = true, boolean = true, number = true, string = true, table = true}
  local function enc(v, out)
    local t = type(v)
    if t == "nil" then out[#out + 1] = "null"
    elseif t == "boolean" then out[#out + 1] = tostring(v)
    elseif t == "number" then
      if v ~= v or v == math.huge or v == -math.huge then out[#out + 1] = "null"
      elseif math.type(v) == "integer" or v == math.floor(v) and math.abs(v) < 1e15 then out[#out + 1] = string.format("%d", v)
      else out[#out + 1] = string.format("%.14g", v) end
    elseif t == "string" then
      out[#out + 1] = '"' .. v:gsub('[%c"\\]', function(c) return esc[c] or string.format("\\u%04x", c:byte()) end) .. '"'
    elseif t == "table" then
      -- JSON4Lua isArray: positive integer keys with encodable values (a userdata among them makes it an
      -- object, where it is left out); other keys only count when their value is encodable (Fiend Folio's save)
      local n, is_array = 0, true
      for k, x in pairs(v) do
        if math.type(k) == "integer" and k >= 1 or type(k) == "number" and k >= 1 and k == math.floor(k) then
          if not encodable[type(x)] then is_array = false; break end
          if k > n then n = math.floor(k) end
        elseif k == "n" then
          if x ~= #v then is_array = false; break end
        elseif encodable[type(x)] then is_array = false; break end
      end
      if is_array then
        out[#out + 1] = "["
        for i = 1, n do if i > 1 then out[#out + 1] = "," end; enc(v[i], out) end
        out[#out + 1] = "]"
      else
        out[#out + 1] = "{"
        local first = true
        for k, x in pairs(v) do
          -- as the PC's json.lua (JSON4Lua): entries with a function, userdata or thread are left out
          if encodable[type(k)] and encodable[type(x)] then
            if not first then out[#out + 1] = "," end
            first = false
            enc(tostring(k), out); out[#out + 1] = ":"; enc(x, out)
          end
        end
        out[#out + 1] = "}"
      end
    else error("json.encode: cannot encode " .. t) end
  end
  function json.encode(v) local out = {}; enc(v, out); return table.concat(out) end

  local function ws(s, i) return s:find("[^ \t\r\n]", i) or #s + 1 end
  local dec
  local function str(s, i)  -- s:sub(i, i) == '"'
    local out, j = {}, i + 1
    while true do
      local c = s:sub(j, j)
      if c == "" then error("json.decode: unterminated string") end
      if c == '"' then return table.concat(out), j + 1 end
      if c == "\\" then
        local e = s:sub(j + 1, j + 1)
        if e == "u" then
          local cp = tonumber(s:sub(j + 2, j + 5), 16) or 63
          out[#out + 1] = utf8.char(cp); j = j + 6
        else
          out[#out + 1] = ({b = "\b", f = "\f", n = "\n", r = "\r", t = "\t"})[e] or e; j = j + 2
        end
      else out[#out + 1] = c; j = j + 1 end
    end
  end
  function dec(s, i)
    i = ws(s, i)
    local c = s:sub(i, i)
    if c == "{" then
      local t = {}
      i = ws(s, i + 1)
      if s:sub(i, i) == "}" then return t, i + 1 end
      while true do
        local k; k, i = str(s, ws(s, i))
        i = ws(s, i) + 1  -- ':'
        t[k], i = dec(s, i)
        i = ws(s, i)
        local d = s:sub(i, i)
        i = i + 1
        if d == "}" then return t, i end
      end
    elseif c == "[" then
      local t = {}
      i = ws(s, i + 1)
      if s:sub(i, i) == "]" then return t, i + 1 end
      while true do
        t[#t + 1], i = dec(s, i)
        i = ws(s, i)
        local d = s:sub(i, i)
        i = i + 1
        if d == "]" then return t, i end
      end
    elseif c == '"' then return str(s, i)
    elseif s:sub(i, i + 3) == "true" then return true, i + 4
    elseif s:sub(i, i + 4) == "false" then return false, i + 5
    elseif s:sub(i, i + 3) == "null" then return nil, i + 4
    else
      local num = s:match("^-?%d+%.?%d*[eE]?[-+]?%d*", i)
      if not num or num == "" then error("json.decode: unexpected '" .. c .. "' at " .. i) end
      return math.tointeger(tonumber(num)) or tonumber(num), i + #num
    end
  end
  function json.decode(s) return (dec(s, 1)) end
  return json
end

---------------------------------------------------------------- more entity fields (offsets from Repentance.nro disassembly)
local function field(name, off, ty, kind)  -- kind: nil = any entity, 5 = pickups, 1 = players
  EG[name] = function(e)
    if kind and e.Type ~= kind then return nil end
    return peek(ptr(e), off, ty)
  end
  ES[name] = function(e, v)
    if kind and e.Type ~= kind then return end
    poke(ptr(e), off, ty, v)
  end
end
field("Velocity", O.Ent_Velocity, "vec")
field("PositionOffset", O.Ent_PositionOffset, "vec")
field("SizeMulti", O.Ent_SizeMulti, "vec")
field("Size", O.Ent_Size, "f32")
field("SpawnerType", O.Ent_SpawnerType, "i32")
field("SpawnerVariant", O.Ent_SpawnerVariant, "i32")
field("Visible", O.Ent_Visible, "b")
field("GridCollisionClass", O.Ent_GridCollision, "i32")
field("EntityCollisionClass", O.Ent_EntityCollision, "i32")
field("Charge", O.Pickup_Charge, "i32", 5)
field("Touched", O.Pickup_Touched, "b", 5)
field("AutoUpdatePrice", O.Pickup_AutoUpdatePrice, "b", 5)
field("ShopItemId", O.Pickup_ShopItemId, "i32", 5)
field("Timeout", O.Pickup_Timeout, "i32", 5)
field("Wait", O.Pickup_Wait, "i32", 5)
EG.DropSeed = function(e) return peek(ptr(e), O.Ent_DropRNG, "u32") end
EG.ControllerIndex = function(e)
  if e.Type ~= 1 then return nil end
  return peek(ptr(e), O.Player_ControllerIndex, "i32")
end
-- EntityFlag bits: u64 at Entity+0x1b8 (Entity::IsVulnerableEnemy tests FLAG_NO_TARGET and FLAG_FRIENDLY there)
local ENT_FLAGS = 0x1b8
function EM.GetEntityFlags(e) return peek(ptr(e), ENT_FLAGS, "i64") or 0 end
function EM.HasEntityFlags(e, f) return (EM.GetEntityFlags(e) & f) == f end
function EM.AddEntityFlags(e, f) poke(ptr(e), ENT_FLAGS, "i64", EM.GetEntityFlags(e) | f) end
function EM.ClearEntityFlags(e, f) poke(ptr(e), ENT_FLAGS, "i64", EM.GetEntityFlags(e) & ~f) end
-- Parent / Child / SpawnerEntity: Entity* at +0x390 / +0x398 / +0x3a8 (Entity::GetLastParent / GetLastChild)
for name, off in pairs({Parent = 0x390, Child = 0x398, SpawnerEntity = 0x3a8}) do
  EG[name] = function(e) return C.entity(peek(ptr(e), off, "p")) end
end
-- their setters: Entity::SetParent / SetChild / SetSpawnerEntity (they keep the engine's entity references)
for name, fn in pairs({Parent = "9SetParentEPS0_", Child = "8SetChildEPS0_", SpawnerEntity = "16SetSpawnerEntityEPS0_"}) do
  local set = native(NS .. "6Entity" .. fn, "_pp", "Entity." .. name .. " (write)")
  if not rawget(ES, name) then ES[name] = function(e, v) set(ptr(e), v and ptr(v) or nil) end end
end
ES.OptionsPickupIndex = function(e, v) if e.Type == 5 then poke(ptr(e), O.Pickup_OptionsIndex, "i32", v) end end
-- EntityNPC (isaacqol NOTES): State +0xb70, I1 +0xbf0, I2 +0xbf4, champion u8 +0xd0f / colour +0xd10
-- (Entity_NPC::MakeChampion), boss colour +0xd24 (Entity_NPC::SetBossColorIdx)
local function npc_field(name, off, ty)
  EG[name] = function(e) if npc_type(e.Type) then return peek(ptr(e), off, ty) end end
  ES[name] = function(e, v) if npc_type(e.Type) then poke(ptr(e), off, ty, v) end end
end
npc_field("State", 0xb70, "i32")
npc_field("I1", 0xbf0, "i32")
npc_field("I2", 0xbf4, "i32")
function EM.IsChampion(e) return npc_type(e.Type) and (peek(ptr(e), 0xd0f, "u8") or 0) ~= 0 end
function EM.GetChampionColorIdx(e) return EM.IsChampion(e) and peek(ptr(e), 0xd10, "i32") or -1 end
function EM.GetBossColorIdx(e)
  if e.Type < 10 or e.Type >= 1000 then return -1 end
  return peek(ptr(e), 0xd24, "i32") or -1
end
local is_enemy = native(NSK .. "6Entity7IsEnemyEv", "bp", "Entity.IsEnemy", false)
function EM.IsEnemy(e) return is_enemy(ptr(e)) end
local is_vulnerable = native_or(NSK .. "6Entity17IsVulnerableEnemyEPS0_", "bpp", function(p)
  local e = C.entity(p)
  return e ~= nil and npc_type(e.Type) and not e:HasEntityFlags(EntityFlag.FLAG_FRIENDLY)
end)
function EM.IsVulnerableEnemy(e) return is_vulnerable(ptr(e), nil) end
local is_active = native_or(NSK .. "6Entity13IsActiveEnemyEb", "bpb", function(p, dead)
  local e = C.entity(p)
  return e ~= nil and npc_type(e.Type) and (dead or not e:IsDead())
end)
function EM.IsActiveEnemy(e, includeDead) return is_active(ptr(e), includeDead == true) end
-- PC: IsInvincible = the entity takes no damage (EntityFlag.FLAG_NO_TARGET marks such NPCs too)
function EM.IsInvincible(e) return e:HasEntityFlags(EntityFlag.FLAG_NO_TARGET) end
-- Entity colour: the sprite's ColorMod (Entity+0x50+0xe8): tint RGBA, colorize RGBA, offset RGB
function EM.GetColor(e)
  local p = offset(ptr(e), 0x138)
  local c = Color(peek(p, 0, "f32"), peek(p, 4, "f32"), peek(p, 8, "f32"), peek(p, 12, "f32"),
    peek(p, 32, "f32"), peek(p, 36, "f32"), peek(p, 40, "f32"))
  if c.SetColorize then c:SetColorize(peek(p, 16, "f32"), peek(p, 20, "f32"), peek(p, 24, "f32"), peek(p, 28, "f32")) end
  return c
end
function EM.IsVisible(e) return e.Visible and e:Exists() end
-- coverage batch 1 (api_coverage.md): Entity fields and virtual methods
do
  local X = {}
  -- ColorMod: tint RGBA +0, colorize RGBA +16, offset RGB +32
  function X.colormod(c)
    local b = C.buf(0x40)
    poke(b, 0, "f32", c.R or 1); poke(b, 4, "f32", c.G or 1); poke(b, 8, "f32", c.B or 1); poke(b, 12, "f32", c.A or 1)
    poke(b, 16, "f32", c.CR or 0); poke(b, 20, "f32", c.CG or 0); poke(b, 24, "f32", c.CB or 0); poke(b, 28, "f32", c.CA or 0)
    poke(b, 32, "f32", c.RO or 0); poke(b, 36, "f32", c.GO or 0); poke(b, 40, "f32", c.BO or 0)
    return b
  end
  X.set_color = native(NS .. "6Entity8SetColorERKNS_8ColorModEiibb", "_ppiibb", "Entity.SetColor")
  -- SetColor(color, duration, priority, fadeout, share)
  function EM.SetColor(e, c, dur, prio, fade, share)
    if type(c) ~= "table" then return end
    X.set_color(ptr(e), X.colormod(c), math.tointeger(dur) or 0, math.tointeger(prio) or 0, fade == true, share ~= false)
  end
  EG.Color = function(e) return EM.GetColor(e) end
  ES.Color = function(e, c)  -- the sprite's ColorMod itself (Entity+0x138), as PC's Entity.Color = x
    if type(c) ~= "table" then return end
    local b, p = X.colormod(c), offset(ptr(e), 0x138)
    for i = 0, 40, 4 do poke(p, i, "f32", peek(b, i, "f32")) end
  end
  -- Entity::SetCollisionDamage writes +0x35c; Entity::GetRenderZ reads RenderZOffset +0x328, SortingLayer +0x330
  for name, d in pairs({CollisionDamage = {0x35c, "f32"}, RenderZOffset = {0x328, "i32"}, SortingLayer = {0x330, "i32"}}) do
    EG[name] = function(e) return peek(ptr(e), d[1], d[2]) end
    ES[name] = function(e, v) poke(ptr(e), d[1], d[2], v) end
  end
  -- Target: Entity* +0x3a0 (between Child +0x398 and SpawnerEntity +0x3a8); set through Entity::SetTarget
  X.set_target = native(NS .. "6Entity9SetTargetEPS0_", "_pp", "Entity.Target (write)")
  EG.Target = function(e) return C.entity(peek(ptr(e), 0x3a0, "p")) end
  ES.Target = function(e, t) X.set_target(ptr(e), t and ptr(t) or nil) end
  -- virtual methods through the entity's own vtable (Entity_NPC::Update for an NPC, ...)
  X.slots = {}
  function X.vcall(e, base, sig, ...)
    local slot = X.slots[base]
    if slot == nil then
      slot = false
      local vt, f = sym("_ZTVN15IsaacRepentance6EntityE"), sym(base)
      if vt and f then
        for i = 2, 120 do if peek(vt, i * 8, "p") == f then slot = i - 2; break end end
      end
      X.slots[base] = slot
    end
    if not slot then return C.missing(base) end
    return call(peek(peek(ptr(e), 0, "p"), slot * 8, "p"), sig, ptr(e), ...)
  end
  function EM.Update(e) X.vcall(e, NS .. "6Entity6UpdateEv", "_p") end
  function EM.Render(e, off) X.vcall(e, NS .. "6Entity6RenderERKN4KAGE4Math7Vector2E", "_pV", off or Vector(0, 0)) end
  function EM.RenderShadowLayer(e, off)
    X.vcall(e, NS .. "6Entity17RenderShadowLayerERKN4KAGE4Math7Vector2E", "_pV", off or Vector(0, 0))
  end
  function EM.PostRender(e) X.vcall(e, NS .. "6Entity10PostRenderEv", "_p") end
  function EM.CanShutDoors(e) return X.vcall(e, NSK .. "6Entity12CanShutDoorsEv", "bp") == true end
  -- EntityNPC.CanShutDoors (write): u8 +0xcc8, what Entity_NPC::CanShutDoors reads (PC +0xb8c, next to
  -- Scale +0xb88 / Switch +0xcc4)
  ES.CanShutDoors = function(e, v) if npc_type(e.Type) then poke(ptr(e), 0xcc8, "u8", v and 1 or 0) end end
end

---------------------------------------------------------------- more EntityPlayer
local EP = NS .. "13Entity_Player"
local EPK = NSK .. "13Entity_Player"
local function pnative(mangled, sig, what, default)
  local f = native(mangled, sig, "EntityPlayer." .. what, default)
  return function(e, ...) return f(ptr(e), ...) end
end
-- m_CanFly: Entity_Player::GetFlyingOffset only lifts the player when this byte is set
-- Entity_Player+0x400: PlayerManager::SetControlsEnabled writes it for every player
EG.ControlsEnabled = function(e) if e.Type == 1 then return peek(ptr(e), 0x400, "b") end end
ES.ControlsEnabled = function(e, v) if e.Type == 1 then poke(ptr(e), 0x400, "b", v) end end
EG.CanFly = function(e) if e.Type == 1 then return peek(ptr(e), 0x1954, "b") end end
ES.CanFly = function(e, v) if e.Type == 1 then poke(ptr(e), 0x1954, "b", v) end end  -- writable on PC (MC_EVALUATE_CACHE flying)
-- the active weapon's fire delay (Entity_Player::lua_GetFireDelay / lua_SetFireDelay, as the PC binding)
do
  local get_fd = native(EPK .. "16lua_GetFireDelayEv", "fp", "EntityPlayer.FireDelay", 0)
  local set_fd = native(EP .. "16lua_SetFireDelayEf", "_pf", "EntityPlayer.FireDelay")
  EG.FireDelay = function(e) if e.Type == 1 then return get_fd(ptr(e)) end end
  ES.FireDelay = function(e, v) if e.Type == 1 then set_fd(ptr(e), v) end end
end
-- Entity_Player+0x1780: one byte per WeaponType the player fires (Entity_Familiar::HasWeaponType reads it)
function EM.HasWeaponType(e, t)
  if e.Type ~= 1 or t < 0 or t > 16 then return false end
  return (peek(ptr(e), 0x1780 + t, "u8") or 0) ~= 0
end
local function pfield(name, off, ty)
  EM[name] = function(e) return peek(ptr(e), off, ty or "i32") or 0 end
end
pfield("GetBrokenHearts", O.Player_BrokenHearts)
pfield("GetRottenHearts", O.Player_RottenHearts)
pfield("GetGoldenHearts", O.Player_GoldenHearts)
pfield("GetSoulCharge", O.Player_SoulCharge)
pfield("GetBloodCharge", O.Player_BloodCharge)
pfield("GetJarHearts", O.Player_JarHearts)
pfield("GetNumGigaBombs", O.Player_GigaBombs)
pfield("GetBabySkin", O.Player_BabySkin)
pfield("GetPoopMana", O.Player_PoopMana)
function EM.HasGoldenKey(e) return (peek(ptr(e), O.Player_GoldenKey, "u8") or 0) ~= 0 end
function EM.HasGoldenBomb(e) return (peek(ptr(e), O.Player_GoldenBomb, "u8") or 0) ~= 0 end
function EM.GetPoopSpell(e, i) return peek(ptr(e), O.Player_PoopSpells + (i or 0), "u8") or 0 end
EM.GetEffectiveSoulCharge = pnative(EPK .. "22GetEffectiveSoulChargeEv", "ip", "GetEffectiveSoulCharge", 0)
EM.GetEffectiveBloodCharge = pnative(EPK .. "23GetEffectiveBloodChargeEv", "ip", "GetEffectiveBloodCharge", 0)
EM.GetEffectiveMaxHearts = pnative(EPK .. "21GetEffectiveMaxHeartsEv", "ip", "GetEffectiveMaxHearts", 0)
local heart_limit = pnative(EPK .. "13GetHeartLimitEb", "ipb", "GetHeartLimit", 24)
function EM.GetHeartLimit(e) return heart_limit(e, false) end
EM.GetMaxTrinkets = pnative(EPK .. "14GetMaxTrinketsEv", "ip", "GetMaxTrinkets", 1)
EM.GetTrinketMultiplier = pnative(EPK .. "20GetTrinketMultiplierENS_12eTrinketTypeE", "ipi", "GetTrinketMultiplier", 0)
EM.GetZodiacEffect = pnative(EPK .. "15GetZodiacEffectEv", "ip", "GetZodiacEffect", 0)
EM.GetMaxPoopMana = pnative(EPK .. "14GetMaxPoopManaEv", "ip", "GetMaxPoopMana", 0)
EM.CanPickRedHearts = pnative(EP .. "16CanPickRedHeartsEv", "bp", "CanPickRedHearts", true)
EM.GetCollectibleCount = pnative(EPK .. "19GetCollectibleCountEv", "ip", "GetCollectibleCount", 0)
EM.HasPlayerForm = pnative(EPK .. "13HasPlayerFormENS_11ePlayerFormE", "bpi", "HasPlayerForm", false)
EM.GetMainPlayer = pnative(EPK .. "13GetMainPlayerEv", "ep", "GetMainPlayer")
function EM.GetSubPlayer(e) return C.entity(peek(ptr(e), O.Player_SubPlayer, "p")) end
function EM.GetOtherTwin(e) return C.entity(peek(ptr(e), O.Player_Twin, "p")) end
-- the twin listed first in the PlayerManager is the main one (Entity_Player::IsHologram does the same)
function EM.GetMainTwin(e)
  local t = EM.GetOtherTwin(e)
  if t and peek(ptr(t), O.Player_ListIndex, "i32") < peek(ptr(e), O.Player_ListIndex, "i32") then return t end
  return e
end
-- Forgotten's soul (and the like): has a partner, but is not in the player list
function EM.IsSubPlayer(e)
  return peek(ptr(e), O.Player_SubPlayer, "p") ~= nil and peek(ptr(e), O.Player_ListIndex, "i32") < 0
end
local entity_config_player = native(NSK .. "12EntityConfig9GetPlayerENS_11ePlayerTypeE", "ppi", "EntityPlayer.GetName")
do
  local add_null = native(NS .. "13Entity_Player14AddNullCostumeENS_10ItemConfig11eNullItemIDE", "_pi", "EntityPlayer.AddNullCostume")
  local rm_null = native(NS .. "13Entity_Player16TryRemoveCostumeENS_10ItemConfig11eNullItemIDE", "_pi", "EntityPlayer.TryRemoveNullCostume")
  -- an unknown id (Isaac.GetCostumeIdByPath gives -1 here) has no null item: the game would crash on it
  function EM.AddNullCostume(e, id) if e.Type == 1 and id and id >= 0 and config:GetNullItem(id) then add_null(ptr(e), id) end end
  function EM.TryRemoveNullCostume(e, id) if e.Type == 1 then rm_null(ptr(e), id) end end
end
-- firing: Entity_Player::Fire* (FireTear flags: bit0 not an Evil Eye tear, bit1 no Tractor Beam,
-- bit2 does not end a streak; velocity is passed by value (S3_ = Vector2, S5_ = const Vector2&); FireKnife(parent, variant, rotation, cantOverwrite, subtype))
do
  local V2 = "ERKN4KAGE4Math7Vector2E"
  local fire_tear = native(EP .. "8FireTear" .. V2 .. "S3_jPNS_6EntityEf", "epVvupf", "EntityPlayer.FireTear")
  function EM.FireTear(e, pos, vel, can_eye, no_tractor, can_streak, src, mul)
    if e.Type ~= 1 then return nil end
    local fl = (can_eye == false and 1 or 0) | (no_tractor and 2 or 0) | (can_streak == false and 4 or 0)
    return fire_tear(ptr(e), pos, vel, fl, src, mul or 1)
  end
  local fire_brim = native(EP .. "13FireBrimstone" .. V2 .. "PNS_6EntityEf", "epVpf", "EntityPlayer.FireBrimstone")
  function EM.FireBrimstone(e, dir, src, mul) if e.Type == 1 then return fire_brim(ptr(e), dir, src, mul or 1) end end
  local fire_tech = native(EP .. "13FireTechLaser" .. V2 .. "NS0_12eLaserOffsetES5_bbPNS_6EntityEf", "epViVbbpf",
    "EntityPlayer.FireTechLaser")
  function EM.FireTechLaser(e, pos, off, dir, left, one_hit, src, mul)
    if e.Type == 1 then return fire_tech(ptr(e), pos, off or 0, dir, left or false, one_hit or false, src, mul or 1) end
  end
  local fire_techx = native(EP .. "14FireTechXLaser" .. V2 .. "S5_fPNS_6EntityEf", "epVVfpf", "EntityPlayer.FireTechXLaser")
  function EM.FireTechXLaser(e, pos, dir, radius, src, mul)
    if e.Type == 1 then return fire_techx(ptr(e), pos, dir, radius, src, mul or 1) end
  end
  local fire_bomb = native(EP .. "8FireBomb" .. V2 .. "S5_PNS_6EntityE", "epVVp", "EntityPlayer.FireBomb")
  function EM.FireBomb(e, pos, vel, src) if e.Type == 1 then return fire_bomb(ptr(e), pos, vel, src) end end
  local fire_knife = native(EP .. "9FireKnifeEPNS_6EntityEjfbj", "eppufbu", "EntityPlayer.FireKnife")
  function EM.FireKnife(e, parent, rot, cant_overwrite, subtype, variant)
    if e.Type == 1 then return fire_knife(ptr(e), parent, variant or 0, rot or 0, cant_overwrite or false, subtype or 0) end
  end
  local EK = NS .. "12Entity_Knife"
  local knife_shoot = native(EK .. "5ShootEff", "_pff", "EntityKnife.Shoot")
  local knife_reset = native(EK .. "5ResetEv", "_p", "EntityKnife.Reset")
  function EM.Shoot(e, charge, range) if e.Type == 8 then knife_shoot(ptr(e), charge, range) end end
  function EM.Reset(e) if e.Type == 8 then knife_reset(ptr(e)) end end
  local revive = native(EP .. "16WillPlayerReviveEv", "bp", "EntityPlayer.WillPlayerRevive", false)
  function EM.WillPlayerRevive(e) return e.Type == 1 and revive(ptr(e)) or false end
  local tear_variant = native(NS .. "11Entity_Tear13ChangeVariantEi", "_pi", "EntityTear.ChangeVariant")
  function EM.ChangeVariant(e, v) if e.Type == 2 then tear_variant(ptr(e), v) end end
end
-- SpriteScale: the Scale of the entity's sprite (ANM2 at Entity+0x50, Scale +0xdc)
field("SpriteScale", 0x50 + 0xdc, "vec")
do
  local weapon_ent = native(EP .. "25lua_GetActiveWeaponEntityEv", "pp", "EntityPlayer.GetActiveWeaponEntity")
  function EM.GetActiveWeaponEntity(e)
    if e.Type ~= 1 then return nil end
    local p = weapon_ent(ptr(e))
    return p and C.entity(p) or nil
  end
end
-- active slots: 4 x 28 bytes at Player+0x1964 {Item, Charge, BatteryCharge, SubCharge, TimedRechargeCooldown,
-- PartialCharge f32, VarData} (GetTotalActiveCharge adds +4 and +8)
do
  local function slot(e, s)
    s = s or 0
    if e.Type == 1 and s >= 0 and s < 4 then return O.Player_Actives + 28 * s end
  end
  function EM.GetActiveCharge(e, s) local o = slot(e, s); return o and peek(ptr(e), o + 4, "i32") or 0 end
  function EM.GetBatteryCharge(e, s) local o = slot(e, s); return o and peek(ptr(e), o + 8, "i32") or 0 end
  function EM.GetActiveSubCharge(e, s) local o = slot(e, s); return o and peek(ptr(e), o + 12, "i32") or 0 end
  function EM.GetActiveItemDesc(e, s)
    local o, p = slot(e, s), ptr(e)
    if not o then return nil end
    return {Item = peek(p, o, "i32"), Charge = peek(p, o + 4, "i32"), BatteryCharge = peek(p, o + 8, "i32"),
            SubCharge = peek(p, o + 12, "i32"), TimedRechargeCooldown = peek(p, o + 16, "i32"),
            PartialCharge = peek(p, o + 20, "f32"), VarData = peek(p, o + 24, "i32")}
  end
  local holding = native(EPK .. "13IsHoldingItemEv", "bp", "EntityPlayer.IsHoldingItem", false)
  local lives = native(EPK .. "13GetExtraLivesEv", "ip", "EntityPlayer.GetExtraLives", 0)
  function EM.IsHoldingItem(e) return e.Type == 1 and holding(ptr(e)) end
  function EM.GetExtraLives(e) return e.Type == 1 and lives(ptr(e)) or 0 end
  function EM.AreControlsEnabled(e) return e.ControlsEnabled end
  -- costumes of an ItemConfigItem (the proxy keeps the engine pointer in _p)
  local add_costume = native(EP .. "10AddCostumeEPKNS_10ItemConfig4ItemEb", "_ppb", "EntityPlayer.AddCostume")
  local rm_costume = native(EP .. "13RemoveCostumeEPKNS_10ItemConfig4ItemE", "_pp", "EntityPlayer.RemoveCostume")
  local rm_coll = native(EP .. "16TryRemoveCostumeENS_16eCollectibleTypeEb", "_pib", "EntityPlayer.TryRemoveCollectibleCostume")
  local rm_trinket = native(EP .. "16TryRemoveCostumeENS_12eTrinketTypeE", "_pi", "EntityPlayer.TryRemoveTrinketCostume")
  local function item_p(it) return type(it) == "table" and rawget(it, "_p") or nil end
  function EM.AddCostume(e, it, state_only)
    if e.Type == 1 and item_p(it) then add_costume(ptr(e), item_p(it), state_only or false) end
  end
  function EM.RemoveCostume(e, it) if e.Type == 1 and item_p(it) then rm_costume(ptr(e), item_p(it)) end end
  function EM.TryRemoveCollectibleCostume(e, id, keep) if e.Type == 1 then rm_coll(ptr(e), id, keep or false) end end
  function EM.TryRemoveTrinketCostume(e, id) if e.Type == 1 then rm_trinket(ptr(e), id) end end
  -- movement and shooting input of the player's controller, as the game reads it (ButtonAction 0-7)
  local function axis(e, neg, pos)
    local c = e.ControllerIndex
    return Input.GetActionValue(pos, c) - Input.GetActionValue(neg, c)
  end
  function EM.GetMovementInput(e) return Vector(axis(e, 0, 1), axis(e, 2, 3)) end
  function EM.GetShootingInput(e) return Vector(axis(e, 4, 5), axis(e, 6, 7)) end
  function EM.GetMovementVector(e) return EM.GetMovementInput(e) end
  local function direction(v)  -- Direction: -1 none, 0 left, 1 up, 2 right, 3 down
    if v.X == 0 and v.Y == 0 then return -1 end
    if math.abs(v.X) >= math.abs(v.Y) then return v.X < 0 and 0 or 2 end
    return v.Y < 0 and 1 or 3
  end
  function EM.GetFireDirection(e) return direction(EM.GetShootingInput(e)) end
  function EM.GetMovementDirection(e) return direction(EM.GetMovementInput(e)) end
end
-- black hearts: bit n = soul heart pair n (Entity_Player::IsBlackHeart reads Player+0x16c8)
function EM.GetBlackHearts(e) return e.Type == 1 and peek(ptr(e), 0x16c8, "u32") or 0 end
function EM.IsCoopGhost(e) return e.Type == 1 and (peek(ptr(e), 0x284c, "u8") or 0) ~= 0 end  -- cleared by revive_coop_ghost
-- the Switch players.xml names the vanilla characters by string key (#ISAAC_NAME); PC players.xml has the English
-- name there, so a key goes through EntityConfig::Player::GetDisplayName(English) ("Isaac", "???", "The Lost")
do
local player_display = native(NSK .. "12EntityConfig6Player14GetDisplayNameEN4KAGE9eLanguageE", "Spi", "EntityPlayer.GetName", "")
function EM.GetName(e)
  local entry = entity_config_player(offset(C.managerptr(), O.Manager_EntityConfig), e:GetPlayerType())
  local name = entry and peek(entry, 0x8, "str") or ""
  if name:sub(1, 1) == "#" then
    local shown = player_display(entry, 0)
    if shown and shown ~= "" then return shown end
  end
  return name
end
end
-- PlayerConfig (EntityConfig::GetPlayer): id +0, name +8, skinColor +0xf4 (EntityConfig::LoadPlayers' stack
-- copy, inferred); head/body colour from items that change it is not followed
do
  local function skin(e)
    local entry = e.Type == 1 and entity_config_player(offset(C.managerptr(), O.Manager_EntityConfig), e:GetPlayerType())
    return entry and peek(entry, 0xf4, "i32") or -1
  end
  EM.GetHeadColor, EM.GetBodyColor = skin, skin
  -- Curse of the Mist effect: u8 Player+0x27e0 (Entity_Player::AddCurseMistEffect / RemoveCurseMistEffect)
  function EM.HasCurseMistEffect(e) return e.Type == 1 and (peek(ptr(e), 0x27e0, "u8") or 0) ~= 0 end
end
-- vanilla tainted characters are 21-40; characters from mods' players.xml (loaded by the game's own
-- ModManager since 0.36) come after 40 and are matched by name only
-- as PC's (isaac-ng.exe 0x80beb0): the last player of that name whose taintedness matches; tainted = the
-- vanilla tainted range 21..40, or a bSkinParent in players.xml (std::string at +288, LoadPlayers)
function Isaac.GetPlayerTypeByName(name, tainted)
  local ec = offset(C.managerptr(), O.Manager_EntityConfig)
  local n = 0
  while n < 256 and entity_config_player(ec, n) do n = n + 1 end
  for t = n - 1, 0, -1 do
    local entry = entity_config_player(ec, t)
    if peek(entry, 0x8, "str") == name then
      local is_tainted = (t >= 21 and t < 41) or (peek(entry, 288, "str") or "") ~= ""
      if is_tainted == (tainted == true) then return t end
    end
  end
  return -1
end

-- TemporaryEffects (Entity_Player+0x18d8)
local TE = NSK .. "16TemporaryEffects"
local K_COLL, K_TRINKET, K_NULL = "ENS_16eCollectibleTypeE", "ENS_12eTrinketTypeE", "ENS_10ItemConfig11eNullItemIDE"
local te_has = {c = native(TE .. "9HasEffect" .. K_COLL, "bpi", "TemporaryEffects.HasCollectibleEffect", false),
  t = native(TE .. "9HasEffect" .. K_TRINKET, "bpi", "TemporaryEffects.HasTrinketEffect", false),
  n = native(TE .. "9HasEffect" .. K_NULL, "bpi", "TemporaryEffects.HasNullEffect", false)}
local te_num = {c = native(TE .. "12GetEffectNum" .. K_COLL, "ipi", "TemporaryEffects.GetCollectibleEffectNum", 0),
  t = native(TE .. "12GetEffectNum" .. K_TRINKET, "ipi", "TemporaryEffects.GetTrinketEffectNum", 0),
  n = native(TE .. "12GetEffectNum" .. K_NULL, "ipi", "TemporaryEffects.GetNullEffectNum", 0)}
local te_get = {c = native(TE .. "9GetEffect" .. K_COLL, "ppi", "TemporaryEffects.GetCollectibleEffect"),
  t = native(TE .. "9GetEffect" .. K_TRINKET, "ppi", "TemporaryEffects.GetTrinketEffect"),
  n = native(TE .. "9GetEffect" .. K_NULL, "ppi", "TemporaryEffects.GetNullEffect")}
local function wrap_effect(p)  -- TemporaryEffect {Item*, Count +8, Cooldown +0xc}
  if not p then return nil end
  local it = peek(p, 0, "p")
  return {Item = it and wrap_item(it) or nil, Count = peek(p, 8, "i32"), Cooldown = peek(p, 0xc, "i32")}
end
local te_mt = {__type = "TemporaryEffects"}
-- a function, as LuaBridge's: CustomHealthAPI wraps getmetatable(TemporaryEffects).__class.__index (= te_mt)
te_mt.__index = function(_, k) return rawget(te_mt, k) end
for k, n in pairs({c = "Collectible", t = "Trinket", n = "Null"}) do
  te_mt["Has" .. n .. "Effect"] = function(self, id) return te_has[k](self._p, id) end
  te_mt["Get" .. n .. "EffectNum"] = function(self, id) return te_num[k](self._p, id) end
  te_mt["Get" .. n .. "Effect"] = function(self, id) return wrap_effect(te_get[k](self._p, id)) end
end
function te_mt:GetEffectsList()
  local b, e = peek(self._p, 8, "i64"), peek(self._p, 0x10, "i64")
  local out = {}
  if b and e then
    local bp = peek(self._p, 8, "p")
    for i = 0, (e - b) // 16 - 1 do out[#out + 1] = wrap_effect(offset(bp, i * 16)) end
  end
  return setmetatable({Size = #out, Get = function(_, i) return out[i + 1] end}, {__len = function() return #out end})  -- PC binds __len
end
local TEW = NS .. "16TemporaryEffects"
for k, n in pairs({c = "Collectible", t = "Trinket", n = "Null"}) do
  local kind = ({c = K_COLL, t = K_TRINKET, n = K_NULL})[k]
  local add = native(TEW .. "9AddEffect" .. kind .. "bj", "_pibu", "TemporaryEffects.Add" .. n .. "Effect")
  local rm = native(TEW .. "12RemoveEffect" .. kind .. "j", "_piu", "TemporaryEffects.Remove" .. n .. "Effect")
  te_mt["Add" .. n .. "Effect"] = function(self, id, costume, count)
    add(self._p, id, costume ~= false, count or 1)
  end
  te_mt["Remove" .. n .. "Effect"] = function(self, id, num) rm(self._p, id, num or 1) end
end
-- one TemporaryEffects object per player address (MinimapAPI asks every room, every frame); it only holds the pointer
do
local te_objs = setmetatable({}, {__mode = "v"})
function EM.GetEffects(e)
  local p = ptr(e)
  if not p then return nil end
  local o = te_objs[p]
  if not o then o = setmetatable({_p = offset(p, O.Player_Effects)}, te_mt); te_objs[p] = o end
  return o
end
end

-- the player's RNGs (copies: what mods read from them is the seed)
local function shift_index(a, b, c)
  for i, s in ipairs(RNG_SHIFTS) do
    if s[1] == a and s[2] == b and s[3] == c then return i - 1 end
  end
  return 35
end
-- An RNG the game owns (GetCollectibleRNG, GetDropRNG, GetDevilAngelRoomRNG...): on PC these hand out the engine's
-- own object, so each draw advances the game's sequence and the next call goes on from there. A copy gave the
-- same number every time (a mod rolling "1 in 8" each frame never hit). Here seed and shift live in the engine's
-- 16 bytes {seed, shift1, shift2, shift3}: reads and writes of r.seed / r.shift go there, RNG_mt does the rest.
-- rng_vec(nil, addr): the engine RNG at addr (one local only: the chunk is at Lua's 200-local limit)
local rng_vec
do
local engine_rng_mt = {__index = function(r, k)
  local a = rawget(r, "addr")
  if k == "seed" then return peek(a, 0, "u32") end
  if k == "shift" then return shift_index(peek(a, 4, "u32"), peek(a, 8, "u32"), peek(a, 12, "u32")) end
  return RNG_mt[k]
end, __newindex = function(r, k, v)
  local a = rawget(r, "addr")
  if k == "seed" then poke(a, 0, "u32", v & 0xffffffff)
  elseif k == "shift" then
    local sh = RNG_SHIFTS[(v % #RNG_SHIFTS) + 1]
    poke(a, 4, "u32", sh[1]); poke(a, 8, "u32", sh[2]); poke(a, 12, "u32", sh[3])
  else rawset(r, k, v) end
end}
local function engine_rng(addr) return setmetatable({addr = addr}, engine_rng_mt) end
function rng_vec(e, vec, id)
  if e == nil then return engine_rng(vec) end
  local p = ptr(e)
  local b, en = peek(p, O.Player_RNGs + vec, "i64"), peek(p, O.Player_RNGs + vec + 8, "i64")
  if not b or not en or id < 0 or id >= (en - b) // 16 then return RNG() end
  return engine_rng(offset(peek(p, O.Player_RNGs + vec, "p"), id * 16))
end
end
-- CheckFamiliar(variant, count, rng, item, subtype): Entity_Player::check_familiar with an engine RNG
-- {seed, shift1..3} made from the Lua one, whose seed follows the engine's afterwards
do
  local check = native(EP .. "14check_familiarEjiRNS_3RNGEPKNS_10ItemConfig4ItemEi", "_puippi",
    "EntityPlayer.CheckFamiliar")
  function EM.CheckFamiliar(e, variant, count, rng, item, subtype)
    if e.Type ~= 1 then return end
    local r = C.buf(16)
    local sh = RNG_SHIFTS[((rng and rng.shift or 35) % #RNG_SHIFTS) + 1]
    poke(r, 0, "u32", rng and rng.seed or 2853650767)
    for i = 1, 3 do poke(r, 4 * i, "u32", sh[i]) end
    check(ptr(e), variant, count, r, item and rawget(item, "_p") or nil, subtype or -1)
    if rng and rng.SetSeed then rng.seed = peek(r, 0, "u32") end
  end
end
-- Level:QueryRoomTypeIndex(type, visited, rng, ignore_group): Level::QueryRoomTypeIndex with an engine RNG made
-- from the Lua one, as CheckFamiliar
do
  local query = native(NS .. "5Level18QueryRoomTypeIndexENS_9eRoomTypeEbRNS_3RNGEb", "ipibpb", "Level.QueryRoomTypeIndex", -1)
  function level:QueryRoomTypeIndex(t, visited, rng, ignore_group)
    local r = C.buf(16)
    local sh = RNG_SHIFTS[((rng and rng.shift or 35) % #RNG_SHIFTS) + 1]
    poke(r, 0, "u32", rng and rng.seed or 2853650767)
    for i = 1, 3 do poke(r, 4 * i, "u32", sh[i]) end
    local i = query(gp(), t or 0, visited == true, r, ignore_group == true)
    if rng and rng.SetSeed then rng.seed = peek(r, 0, "u32") end
    return i
  end
end
function EM.GetCollectibleRNG(e, id) return rng_vec(e, 0x0, id) end
function EM.GetTrinketRNG(e, id) return rng_vec(e, 0x18, id & 0x7fff) end
function EM.GetPillRNG(e, id) return rng_vec(e, 0x30, id) end
function EM.GetCardRNG(e, id) return rng_vec(e, 0x48, id) end
function EM.GetDropRNG(e) return rng_vec(nil, offset(ptr(e), O.Ent_DropRNG)) end
-- GridEntity:GetRNG(): the grid's own RNG (PC _rng +0x2c; Switch GridEntity::GridEntity builds it at +0x30)
function grid_mt:GetRNG() return rng_vec(nil, offset(rawget(self, "_p"), 0x30)) end

---------------------------------------------------------------- small API classes (userdata, as on PC)
local function class(name, methods)
  local mt = {__type = name}
  function mt.__index(u, k)
    local m = methods[k]
    if m ~= nil then return m end
    return C.unbox(u)[k]
  end
  function mt.__newindex(u, k, v) C.unbox(u)[k] = v end
  return mt, function(fields) return C.box(mt, fields) end
end

-- BitSet128 (TearFlags): two 64-bit halves l, h
local bs_mt, new_bs
local function bs(l, h) return new_bs({l = l or 0, h = h or 0}) end
local function halves(x)
  if type(x) == "number" then return x, 0 end
  local f = C.unbox(x)
  return f.l, f.h
end
bs_mt, new_bs = class("BitSet128", {
  Get = function(self, i) local f = C.unbox(self); return (i < 64 and f.l >> i or f.h >> (i - 64)) & 1 == 1 end,
  Set = function(self, i, v)
    local f = C.unbox(self)
    local k, n = i < 64 and "l" or "h", i % 64
    if v then f[k] = f[k] | (1 << n) else f[k] = f[k] & ~(1 << n) end
  end,
  __tostring = function(self) return tostring(self) end})
bs_mt.__band = function(a, b) local al, ah = halves(a); local bl, bh = halves(b); return bs(al & bl, ah & bh) end
bs_mt.__bor = function(a, b) local al, ah = halves(a); local bl, bh = halves(b); return bs(al | bl, ah | bh) end
bs_mt.__bxor = function(a, b) local al, ah = halves(a); local bl, bh = halves(b); return bs(al ~ bl, ah ~ bh) end
bs_mt.__bnot = function(a) local l, h = halves(a); return bs(~l, ~h) end
bs_mt.__eq = function(a, b) local al, ah = halves(a); local bl, bh = halves(b); return al == bl and ah == bh end
local function ult(a, b) return math.ult(a, b) end
bs_mt.__lt = function(a, b)
  local al, ah = halves(a); local bl, bh = halves(b)
  if ah ~= bh then return ult(ah, bh) end
  return ult(al, bl)
end
bs_mt.__le = function(a, b) return a == b or a < b end
bs_mt.__shl = function(a, n)
  local l, h = halves(a)
  if n >= 128 then return bs(0, 0) elseif n >= 64 then return bs(0, l << (n - 64)) elseif n == 0 then return bs(l, h) end
  return bs(l << n, (h << n) | (l >> (64 - n)))
end
bs_mt.__shr = function(a, n)
  local l, h = halves(a)
  if n >= 128 then return bs(0, 0) elseif n >= 64 then return bs(h >> (n - 64), 0) elseif n == 0 then return bs(l, h) end
  return bs((l >> n) | (h << (64 - n)), h >> n)
end
bs_mt.__tostring = function(a) local l, h = halves(a); return string.format("%016x%016x", h, l) end
BitSet128 = bs
-- TearFlags (as on PC: BitSet128 values) and TEARFLAG(n); bits 0-83, then 104-127
function TEARFLAG(n) return bs(1, 0) << n end
TearFlags = {TEAR_NORMAL = bs(0, 0)}
for i, n in ipairs({"SPECTRAL", "PIERCING", "HOMING", "SLOW", "POISON", "FREEZE", "SPLIT", "GROW", "BOOMERANG", "PERSISTENT", "WIGGLE", "MULLIGAN", "EXPLOSIVE", "CHARM", "CONFUSION", "HP_DROP", "ORBIT", "WAIT", "QUADSPLIT", "BOUNCE", "FEAR", "SHRINK", "BURN", "ATTRACTOR", "KNOCKBACK", "PULSE", "SPIRAL", "FLAT", "SAD_BOMB", "BUTT_BOMB", "SQUARE", "GLOW", "GISH", "MYSTERIOUS_LIQUID_CREEP", "SHIELDED", "GLITTER_BOMB", "SCATTER_BOMB", "STICKY", "CONTINUUM", "LIGHT_FROM_HEAVEN", "COIN_DROP", "BLACK_HP_DROP", "TRACTOR_BEAM", "GODS_FLESH", "GREED_COIN", "CROSS_BOMB", "BIG_SPIRAL", "PERMANENT_CONFUSION", "BOOGER", "EGG", "ACID", "BONE", "BELIAL", "MIDAS", "NEEDLE", "JACOBS", "HORN", "LASER", "POP", "ABSORB", "LASERSHOT", "HYDROBOUNCE", "BURSTSPLIT", "CREEP_TRAIL", "PUNCH", "ICE", "MAGNETIZE", "BAIT", "OCCULT", "ORBIT_ADVANCED", "ROCK", "TURN_HORIZONTAL", "BLOOD_BOMB", "ECOLI", "COIN_DROP_DEATH", "BRIMSTONE_BOMB", "RIFT", "SPORE", "GHOST_BOMB", "CARD_DROP_DEATH", "RUNE_DROP_DEATH", "TELEPORT", "TEAR_DECELERATE", "TEAR_ACCELERATE"}) do TearFlags["TEAR_" .. n] = TEARFLAG(i - 1) end
for i, n in ipairs({"BOUNCE_WALLSONLY", "NO_GRID_DAMAGE", "BACKSTAB", "FETUS_SWORD", "FETUS_BONE", "FETUS_KNIFE", "FETUS_TECHX", "FETUS_TECH", "FETUS_BRIMSTONE", "FETUS_BOMBER", "FETUS", "REROLL_ROCK_WISP", "MOM_STOMP_WISP", "ENEMY_TO_WISP", "REROLL_ENEMY", "GIGA_BOMB", "EXTRA_GORE", "RAINBOW", "DETONATE", "CHAIN", "DARK_MATTER", "GOLDEN_BOMB", "FAST_BOMB", "LUDOVICO"}) do TearFlags["TEAR_" .. n] = TEARFLAG(103 + i) end
TearFlags.TEAR_EFFECT_COUNT = TEARFLAG(84)
setmetatable(TearFlags, {__index = function() return bs(0, 0) end})
TearFlags.TEAR_DECELERATE, TearFlags.TEAR_ACCELERATE = TearFlags.TEAR_TEAR_DECELERATE, TearFlags.TEAR_TEAR_ACCELERATE
-- the entity's TearFlags field: player +0x1858 (Weapon::GetTearFlags), tear and laser +0x418
-- (Entity_Tear::SetTearFlags, FireTechLaser), bomb +0x428 (Entity_Bomb::SetTearFlags), knife +0x400 (FireKnife)
local TEARFLAGS_AT = {[1] = 0x1858, [2] = 0x418, [4] = 0x428, [7] = 0x418, [8] = 0x400}
EG.TearFlags = function(e)
  local off = TEARFLAGS_AT[e.Type]
  if not off then return nil end
  return bs(peek(ptr(e), off, "i64"), peek(ptr(e), off + 8, "i64"))
end
ES.TearFlags = function(e, v)
  local off = TEARFLAGS_AT[e.Type]
  if not off then return end
  local l, h = halves(v)
  poke(ptr(e), off, "i64", l)
  poke(ptr(e), off + 8, "i64", h)
end
-- coverage batch 2: fields of one entity class at a time (type -> offset), from the class's own setters
do
  local function typed(name, ty, at)
    EG[name] = function(e) local o = at[e.Type]; if o then return peek(ptr(e), o, ty) end end
    ES[name] = function(e, v) local o = at[e.Type]; if o then poke(ptr(e), o, ty, v) end end
  end
  -- Entity_Tear::SetFallingSpeed +0x404, SetFallingAcceleration +0x408, SetHomingFriction +0x40c;
  -- Height +0x400 (just before them, as on PC; inferred)
  typed("FallingSpeed", "f32", {[2] = 0x404})
  typed("FallingAcceleration", "f32", {[2] = 0x408})
  typed("HomingFriction", "f32", {[2] = 0x40c})
  typed("Height", "f32", {[2] = 0x400})
  -- bomb flags are its TearFlags (Entity_Bomb::SetTearFlags +0x428)
  EG.Flags = function(e) if e.Type == 4 then return EG.TearFlags(e) end end
  ES.Flags = function(e, v) if e.Type == 4 then ES.TearFlags(e, v) end end
  -- laser angle in degrees: Entity_Laser::GetAngleDegrees / SetAngleDegrees (+0x40c)
  local get_a = native(NSK .. "12Entity_Laser15GetAngleDegreesEv", "fp", "EntityLaser.Angle")
  local set_a = native(NS .. "12Entity_Laser15SetAngleDegreesEf", "_pf", "EntityLaser.Angle")
  for _, k in ipairs({"Angle", "AngleDegrees"}) do
    EG[k] = function(e) if e.Type == 7 then return get_a(ptr(e)) end end
    ES[k] = function(e, v) if e.Type == 7 then set_a(ptr(e), v) end end
  end
end
-- coverage batch 3: fields that several classes share by name. Offsets follow PC's layout (REPENTOGON's
-- libzhl) and are checked against the Switch code that uses them.
do
  local X = {}
  function X.multi(name, ty, at, npc)  -- at: Type -> offset; npc: the offset for EntityNPC (Type 10-999)
    local function off(e) local t = e.Type; if npc and npc_type(t) then return npc end return at[t] end
    EG[name] = function(e) local o = off(e); if o then return peek(ptr(e), o, ty) end end
    ES[name] = function(e, v) local o = off(e); if o then poke(ptr(e), o, ty, v) end end
  end
  -- State: NPC +0xb70, familiar +0xd98 (its ai_* state machines), effect +0x448 (Entity_Effect::Init)
  X.multi("State", "i32", {[3] = 0xd98, [1000] = 0x448}, 0xb70)
  -- Timeout: pickup +0x570, effect +0x454 with LifeSpan +0x458 (Init sets both to the same value),
  -- laser +0x448 (counted down in Entity_Laser::Update)
  X.multi("Timeout", "i32", {[5] = O.Pickup_Timeout, [1000] = 0x454, [7] = 0x448})
  X.multi("LifeSpan", "i32", {[1000] = 0x458})
  -- Entity_Tear (Init, SetCollisionDamage, MakeMultidimensionalCopy): TearIndex +0x450, BaseDamage +0x454,
  -- Rotation +0x458, KnockbackMultiplier +0x460; WaitFrames +0x428 and ContinueVelocity +0x42c follow
  -- TearFlags as on PC (inferred)
  X.multi("TearIndex", "i32", {[2] = 0x450})
  X.multi("BaseDamage", "f32", {[2] = 0x454})
  X.multi("Rotation", "f32", {[2] = 0x458})
  X.multi("KnockbackMultiplier", "f32", {[2] = 0x460})
  X.multi("WaitFrames", "i32", {[2] = 0x428})
  X.multi("ContinueVelocity", "vec", {[2] = 0x42c})
  -- Entity_Laser (SetScale +0x508, CopyProperties): MaxDistance +0x4f8, DisableFollowParent u8 +0x50c,
  -- Shrink u8 +0x44e (PC's order around them)
  X.multi("MaxDistance", "f32", {[7] = 0x4f8})
  X.multi("DisableFollowParent", "b", {[7] = 0x50c})
  X.multi("Shrink", "b", {[7] = 0x44e})
  -- Entity_Projectile: ProjectileFlags u64 +0x428 (bit-tested all over Update), Height +0x400 (-23 in
  -- Init); FallingSpeed +0x404 / FallingAccel +0x408 after it, as on tears (inferred)
  X.multi("ProjectileFlags", "i64", {[9] = 0x428})
  X.multi("Height", "f32", {[2] = 0x400, [9] = 0x400})
  X.multi("FallingSpeed", "f32", {[2] = 0x404, [9] = 0x404})
  X.multi("FallingAccel", "f32", {[9] = 0x408})
  function EM.AddProjectileFlags(e, f) if e.Type == 9 then ES.ProjectileFlags(e, EG.ProjectileFlags(e) | f) end end
  function EM.ClearProjectileFlags(e, f) if e.Type == 9 then ES.ProjectileFlags(e, EG.ProjectileFlags(e) & ~f) end end
  function EM.HasProjectileFlags(e, f) return e.Type == 9 and (EG.ProjectileFlags(e) & f) == f end
  -- Entity_NPC (PC order, fields as the Switch code uses them): StateFrame +0x400, ProjectileCooldown +0xbd0,
  -- ProjectileDelay +0xbd4, V1 +0xbe0, V2 +0xbe8 (just before I1 +0xbf0)
  X.multi("StateFrame", "i32", {}, 0x400)
  X.multi("ProjectileCooldown", "i32", {}, 0xbd0)
  X.multi("ProjectileDelay", "i32", {}, 0xbd4)
  X.multi("V1", "vec", {}, 0xbe0)
  X.multi("V2", "vec", {}, 0xbe8)
  -- player TearColor +0x1868 and LaserColor +0x1894: ColorMods built in Entity_Player's constructor
  -- (PC keeps the same pair right after the tear stats)
  for name, at in pairs({TearColor = 0x1868, LaserColor = 0x1894}) do
    EG[name] = function(e)
      if e.Type ~= 1 then return nil end
      local p = offset(ptr(e), at)
      local c = Color(peek(p, 0, "f32"), peek(p, 4, "f32"), peek(p, 8, "f32"), peek(p, 12, "f32"),
        peek(p, 32, "f32"), peek(p, 36, "f32"), peek(p, 40, "f32"))
      if c.SetColorize then c:SetColorize(peek(p, 16, "f32"), peek(p, 20, "f32"), peek(p, 24, "f32"), peek(p, 28, "f32")) end
      return c
    end
    ES[name] = function(e, c)
      if e.Type ~= 1 or type(c) ~= "table" then return end
      local p = offset(ptr(e), at)
      for i, k in ipairs({"R", "G", "B", "A", "CR", "CG", "CB", "CA", "RO", "GO", "BO"}) do
        poke(p, (i - 1) * 4, "f32", c[k] or (i <= 4 and 1 or 0))
      end
    end
  end
end
-- coverage batch 4: the rest of the PC API (IsaacDocs). Fields with an engine offset read and write the
-- entity; the few still not found keep what a mod writes (per entity) and report "stored only" once.
do
  local X = {}
  function X.multi(name, ty, at, npc)
    local function off(e) local t = e.Type; if npc and npc_type(t) then return npc end return at[t] end
    EG[name] = function(e) local o = off(e); if o then return peek(ptr(e), o, ty) end end
    ES[name] = function(e, v) local o = off(e); if o then poke(ptr(e), o, ty, v) end end
  end
  -- Entity_Projectile, as Entity_NPC::fire_projectiles copies ProjectileParams into it: HomingStrength +0x414,
  -- CurvingStrength +0x418, Acceleration +0x41c, WiggleFrameOffset +0x434, ChangeFlags u64 +0x438,
  -- ChangeVelocity +0x440, ChangeTimeout +0x444 (and its DepthOffset is the entity's +0x32c)
  X.multi("HomingStrength", "f32", {[9] = 0x414})
  X.multi("CurvingStrength", "f32", {[9] = 0x418})
  X.multi("Acceleration", "f32", {[9] = 0x41c})
  X.multi("WiggleFrameOffset", "i32", {[9] = 0x434})
  X.multi("ChangeFlags", "i64", {[9] = 0x438})
  X.multi("ChangeVelocity", "f32", {[9] = 0x440})
  X.multi("ChangeTimeout", "i32", {[9] = 0x444})
  -- Entity_Tear BaseScale +0x414 (after Scale +0x410, as on PC); Entity_Laser SampleLaser u8 +0x44d (before
  -- Shrink), HomingType +0x4fc (after MaxDistance); player BabySkin
  X.multi("BaseScale", "f32", {[2] = 0x414})
  X.multi("SampleLaser", "b", {[7] = 0x44d})
  X.multi("HomingType", "i32", {[7] = 0x4fc})
  X.multi("BabySkin", "i32", {[1] = O.Player_BabySkin})

  -- PathFinder: NPCAI_Pathfinder at NPC +0x408 / familiar +0x5f8 (their constructors), the game's own code
  local PF, PFK, V = NS .. "16NPCAI_Pathfinder", NSK .. "16NPCAI_Pathfinder", "ERKN4KAGE4Math7Vector2E"
  local pf = {
    find = native(PF .. "12FindGridPath" .. V .. "fib", "_pVfib", "PathFinder.FindGridPath"),
    evade = native(PF .. "11EvadeTarget" .. V .. "b", "_pVb", "PathFinder.EvadeTarget"),
    rand = native(PF .. "12MoveRandomlyEb", "bpb", "PathFinder.MoveRandomly", false),
    axis = native(PF .. "23MoveRandomlyAxisAlignedEfb", "_pfb", "PathFinder.MoveRandomlyAxisAligned"),
    boss = native(PF .. "16MoveRandomlyBossEb", "_pb", "PathFinder.MoveRandomlyBoss"),
    reset = native(PF .. "5ResetEv", "_p", "PathFinder.Reset"),
    reset_target = native(PF .. "19ResetMovementTargetEv", "_p", "PathFinder.ResetMovementTarget"),
    update = native(PF .. "15UpdateGridIndexEv", "_p", "PathFinder.UpdateGridIndex"),
    grid = native(PFK .. "12GetGridIndexEv", "ip", "PathFinder.GetGridIndex", -1),
    path = native(PFK .. "12HasPathToPos" .. V .. "b", "bpVb", "PathFinder.HasPathToPos", true),
  }
  local pf_mt = {__type = "PathFinder"}
  pf_mt.__index = {
    FindGridPath = function(s, pos, speed, marker, direct) pf.find(s._p, pos, speed or 1, marker or 0, direct == true) end,
    EvadeTarget = function(s, pos) pf.evade(s._p, pos, false) end,
    MoveRandomly = function(s, ign) return pf.rand(s._p, ign == true) end,
    MoveRandomlyAxisAligned = function(s, speed, ign) pf.axis(s._p, speed or 1, ign == true) end,
    MoveRandomlyBoss = function(s, ign) pf.boss(s._p, ign == true) end,
    Reset = function(s) pf.reset(s._p) end,
    ResetMovementTarget = function(s) pf.reset_target(s._p) end,
    UpdateGridIndex = function(s) pf.update(s._p) end,
    GetGridIndex = function(s) return pf.grid(s._p) end,
    HasPathToPos = function(s, pos, ign) return pf.path(s._p, pos, ign == true) end,
    HasDirectPath = function(s) return pf.path(s._p, s._e.TargetPosition or s._e.Position, false) end,
    GetEvadeMovementCountdown = function() return 0 end,
    SetCanCrushRocks = function() end,
  }
  EG.Pathfinder = function(e)
    local t = e.Type
    local o = npc_type(t) and 0x408 or (t == 3 and 0x5f8) or nil
    if o then return setmetatable({_p = offset(ptr(e), o), _e = e}, pf_mt) end
  end
  EG.EntityRef = function(e) if npc_type(e.Type) then return EntityRef(e) end end

  -- the fields still not found: kept per entity, so a mod reads back what it wrote
  X.store = {}
  function X.stub(cls, name, types, default)
    local what = cls .. "." .. name .. " (stored only)"
    local function slot(e, make)
      local p = ptr(e)
      if not p then return nil end
      local s = X.store[p]
      if s and s.seed ~= e.InitSeed then s = nil end
      if not s and make then
        s = {seed = e.InitSeed}
        X.store[p] = s
      end
      return s
    end
    local function ok(e) local t = e.Type; return types == "npc" and npc_type(t) or types == "any" or types == t end
    local reg = debug.getregistry()
    reg["isaaclua.stored"] = reg["isaaclua.stored"] or {}
    EG[name] = EG[name] or function(e)
      if not ok(e) then return nil end
      C.missing(what)
      local s = slot(e, false)
      if s and s[name] ~= nil then return s[name] end
      if type(default) == "function" then return default(e) end
      return default
    end
    reg["isaaclua.stored"][EG[name]] = what
    ES[name] = ES[name] or function(e, v)
      if not ok(e) then return end
      C.missing(what)
      slot(e, true)[name] = v
    end
  end
  -- BounceLaser: Entity_Laser* +0x510 (PC +0x438, the laser's +0xd8 shift)
  EG.BounceLaser = function(e) if e.Type == 7 then return C.entity(peek(ptr(e), 0x510, "p")) end end
  -- isaac-ng.exe registers these as LuaBridge data members (push writable, member offset, name). Each class block keeps
  -- its PC layout with one shift on Switch, checked against fields already found there: bomb +0xa0 (the
  -- Game::BombExplosionEffects call in Entity_Bomb::Update passes +0x408 damage, +0x43c radius, +0x438 fetus),
  -- familiar +0xc4 (OrbitDistance/OrbitLayer/IsFollower/State), effect +0xa8 (State/Timeout/LifeSpan), laser
  -- +0xa0 up to Shrink and +0xd8 from CurveStrength (MaxDistance/HomingType/DisableFollowParent/BounceLaser),
  -- knife +0xa0 (TearFlags), entity +0x7c (Velocity/Size/Mass/Parent), player +0xa0 (ControlsEnabled) and
  -- +0x8c4 (SamsonBerserkCharge), +0x558 between CanShoot and GoldenHearts (ItemHoldCooldown: set by
  -- Entity_Player::AddCollectible, counted down in Update)
  for _, f in ipairs({
    {"ExplosionDamage", "f32", 4, 0x408}, {"IsFetus", "b", 4, 0x438}, {"RadiusMultiplier", "f32", 4, 0x43c},
    {"FireCooldown", "i32", 3, 0xd60}, {"HeadFrameDelay", "i32", 3, 0xd64}, {"MoveDirection", "i32", 3, 0xd68},
    {"ShootDirection", "i32", 3, 0xd6c}, {"LastDirection", "i32", 3, 0xd70}, {"OrbitAngleOffset", "f32", 3, 0xd74},
    {"OrbitSpeed", "f32", 3, 0xd80}, {"IsDelayed", "b", 3, 0xd89}, {"Coins", "i32", 3, 0xd8c},
    {"Hearts", "i32", 3, 0xd90}, {"Keys", "i32", 3, 0xd94}, {"RoomClearCount", "i32", 3, 0xd9c},
    {"MinRadius", "f32", 1000, 0x44c}, {"MaxRadius", "f32", 1000, 0x450}, {"IsFollowing", "b", 1000, 0x45c},
    {"DamageSource", "i32", 1000, 0x468},
    -- effect m_Height +0x400, FallingSpeed +0x404, FallingAcceleration +0x408: PC +0x360/+0x364/+0x368 right after
    -- the Entity base as on tears and projectiles; Switch Entity_Effect::Interpolate and clamp_particle_to_walls
    -- read +0x400/+0x404, Init sets all three
    {"m_Height", "f32", 1000, 0x400}, {"FallingSpeed", "f32", 1000, 0x404}, {"FallingAcceleration", "f32", 1000, 0x408},
    {"ParentOffset", "vec", 7, 0x400}, {"StartAngleDegrees", "f32", 7, 0x408}, {"LastAngleDegrees", "f32", 7, 0x410},
    {"FirstUpdate", "b", 7, 0x44c}, {"LaserLength", "f32", 7, 0x450}, {"CurveStrength", "f32", 7, 0x4e0},
    {"Radius", "f32", 7, 0x4e4}, {"IsActiveRotating", "b", 7, 0x4e8}, {"RotationDelay", "i32", 7, 0x4ec},
    {"RotationDegrees", "f32", 7, 0x4f0}, {"RotationSpd", "f32", 7, 0x4f4}, {"EndPoint", "vec", 7, 0x500},
    {"RotationOffset", "f32", 8, 0x42c},
    {"ControlsCooldown", "i32", 1, 0x404}, {"IBSCharge", "f32", 1, 0x2844}, {"ItemHoldCooldown", "i32", 1, 0x1cfc},
  }) do
    local name, ty, t, o = f[1], f[2], f[3], f[4]
    local g, st = EG[name], ES[name]  -- the same name on other classes keeps what it had
    EG[name] = function(e) if e.Type == t then return peek(ptr(e), o, ty) end if g then return g(e) end end
    ES[name] = function(e, v) if e.Type == t then return poke(ptr(e), o, ty, v) end if st then return st(e, v) end end
  end
  local V0 = function() return Vector(0, 0) end
  for _, f in ipairs({
    {"Entity", "Friction", "any", 1}, {"Entity", "Mass", "any", 10}, {"Entity", "SpawnGridIndex", "any", -1},
    {"Entity", "SplatColor", "any", function() return Color(1, 1, 1, 1) end},
    {"EntityKnife", "PathFollowSpeed", 8, 0}, {"EntityKnife", "PathOffset", 8, 0},
    {"EntityLaser", "HomingLaser", 7, nil},
    {"EntityPlayer", "SamsonBerserkCharge", 1, 0},
    {"EntityPlayer", "SecondaryActiveItem", 1, nil}, {"EntityPlayer", "TearsOffset", 1, V0},
    {"EntityPlayer", "FriendBallEnemy", 1, function() return {Type = 0, Variant = 0, SubType = 0} end},
    {"EntityTear", "Bounced", 2, false}, {"EntityTear", "CanTriggerStreakEnd", 2, true},
    {"EntityTear", "PosDisplacement", 2, V0},
  }) do X.stub(f[1], f[2], f[3], f[4]) end
  -- fields that were stored only until 0.86, offsets matched against the PC exe's data members:
  -- laser BlackHpDropChance f32 +0x524 / OneHit u8 +0x561 / GridHit u8 +0x562 (PC +0x444/+0x465/+0x466:
  -- Entity_Laser::CopyProperties copies them in PC's order; do_damage and update_laser read them), tear StickTarget
  -- Entity* +0x478 / StickDiff +0x480 / StickTimer i32 +0x488 (PC +0x3c8/+0x3cc/+0x3d4, after KnockbackMultiplier
  -- +0x460; Entity_Tear::Update / handle_collision), NPC GroupIdx i32 +0xd28 (Entity_NPC::query_npcs_group, PC
  -- +0xbcc); ParentNPC / ChildNPC are Parent / Child as an NPC (PC: Entity +0x314 / +0x318 cast to Entity_NPC)
  for _, f in ipairs({{"BlackHpDropChance", 7, 0x524, "f32"}, {"OneHit", 7, 0x561, "b"}, {"GridHit", 7, 0x562, "b"},
      {"StickDiff", 2, 0x480, "vec"}, {"StickTimer", 2, 0x488, "i32"}, {"GroupIdx", "npc", 0xd28, "i32"}}) do
    local name, t, o, ty = f[1], f[2], f[3], f[4]
    local function ok(e) local et = e.Type; return t == "npc" and npc_type(et) or et == t end
    EG[name] = function(e) if ok(e) then return peek(ptr(e), o, ty) end end
    ES[name] = function(e, v) if ok(e) then poke(ptr(e), o, ty, v) end end
  end
  EG.StickTarget = function(e) if e.Type == 2 then return C.entity(peek(ptr(e), 0x478, "p")) end end
  ES.StickTarget = function(e, v)  -- a raw pointer the engine does not track: only clearing it is safe
    if e.Type == 2 and v == nil then poke(ptr(e), 0x478, "p", nil) end
  end
  local function as_npc(e) if e then local n = e:ToNPC(); return n end end
  EG.ParentNPC = function(e) if npc_type(e.Type) then return as_npc(e.Parent) end end
  EG.ChildNPC = function(e) if npc_type(e.Type) then return as_npc(e.Child) end end

  -- Seeds (Game+0x25d40): the game's own Seeds functions; strings go in as libc++ std::strings
  function X.str(s)  -- 24-byte long-form std::string over a copy of s; keep both alive across the call
    s = tostring(s or "")
    local n = #s
    local chars, str = C.buf(n + 1), C.buf(24)
    for i = 1, n do poke(chars, i - 1, "u8", s:byte(i)) end
    poke(str, 0, "i64", ((n + 16) & ~15) | 1)
    poke(str, 8, "i64", n)
    poke(str, 16, "p", chars)
    return str, chars
  end
  local SD, STR = NS .. "5Seeds", "RKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE"
  local s2s = native(SD .. "11String2SeedE" .. STR, "up", "Seeds.String2Seed", 0)
  local valid = native(SD .. "17IsStringValidSeedE" .. STR, "bp", "Seeds.IsStringValidSeed", false)
  local get_effect = native(SD .. "13GetSeedEffectENSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE", "ip",
    "Seeds.GetSeedEffect", 0)
  local count_unlocked = native(SD .. "24CountUnlockedSeedEffectsEv", "i", "Seeds.CountUnlockedSeedEffects", 0)
  local can_add = native(SD .. "16CanAddSeedEffectENS_11eSeedEffectE", "bpi", "Seeds.CanAddSeedEffect", true)
  local rm_blocking = native(SD .. "25RemoveBlockingSeedEffectsENS_11eSeedEffectE", "_pi", "Seeds.RemoveBlockingSeedEffects")
  local set_start_s = native(SD .. "12SetStartSeedENSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE", "_pp",
    "Seeds.SetStartSeed")
  local set_start_u = native(SD .. "12SetStartSeedEj", "_pu", "Seeds.ClearStartSeed")
  local restart = native(SD .. "7RestartENS_10eChallengeE", "_pi", "Seeds.Restart")
  local init_info = native(SD .. "12InitSeedInfoEv", "_", "Seeds.InitSeedInfo")
  local function sp() return offset(gp(), O.Game_Seeds) end
  function seeds:String2Seed(s) local b, c = X.str(s); local r = s2s(b); X.keep = c; return r end
  function seeds:IsStringValidSeed(s) local b, c = X.str(s); local r = valid(b); X.keep = c; return r end
  function seeds:GetSeedEffect(s) local b, c = X.str(s); local r = get_effect(b); X.keep = c; return r end
  function seeds:IsSpecialSeed(s) return seeds:GetSeedEffect(s) ~= 0 end
  function seeds:CountUnlockedSeedEffects() return count_unlocked() end
  function seeds:CanAddSeedEffect(e) return can_add(sp(), e) end
  function seeds:RemoveBlockingSeedEffects(e) rm_blocking(sp(), e) end
  function seeds:SetStartSeed(s) local b, c = X.str(s); set_start_s(sp(), b); X.keep = c end
  function seeds:ClearStartSeed() set_start_u(sp(), 0) end
  function seeds:Restart(challenge) restart(sp(), challenge or 0) end
  function seeds:InitSeedInfo() init_info() end
  function seeds:IsInitialized() return seeds:GetStartSeed() ~= 0 end
  function seeds:GetNextSeed() return Random() end
  function seeds:GetPlayerInitSeed() return seeds:GetStartSeed() end
  function seeds:IsSeedComboBanned() return false end
  Seeds.String2Seed = function(s) return seeds:String2Seed(s) end
  Seeds.IsStringValidSeed = function(s) return seeds:IsStringValidSeed(s) end
  Seeds.GetSeedEffect = function(s) return seeds:GetSeedEffect(s) end
  Seeds.IsSpecialSeed = function(s) return seeds:IsSpecialSeed(s) end
  Seeds.CountUnlockedSeedEffects = function() return count_unlocked() end
  Seeds.InitSeedInfo = function() init_info() end

  -- TemporaryEffects:ClearEffects (the game's own, keeps the permanent ones like PC)
  local clear_te = native(NS .. "16TemporaryEffects12ClearEffectsEb", "_pb", "TemporaryEffects.ClearEffects")
  function te_mt:ClearEffects() clear_te(self._p, false) end

  -- Isaac.*
  -- Isaac.RegisterMod(mod, name, apiVersion): makes an existing table the mod (RegisterMod's own fields and methods)
  Isaac.RegisterMod = function(mod, name, ver)
    local m = RegisterMod(name, ver)
    if type(mod) ~= "table" then return m end
    for k, v in pairs(m) do if mod[k] == nil then mod[k] = v end end
    if getmetatable(mod) == nil then setmetatable(mod, getmetatable(m)) end
    return mod
  end
  function Isaac.GetRandomPosition() return Game():GetRoom():GetRandomPosition(0) end
  function Isaac.GetScreenPointScale() return 1 end
  function Isaac.WorldToScreenDistance(v) return Isaac.WorldToScreen(v) - Isaac.WorldToScreen(Vector(0, 0)) end
  local CURSES = {["Curse of Darkness"] = 1, ["Curse of the Labyrinth"] = 2, ["Curse of the Lost"] = 3,
    ["Curse of the Unknown"] = 4, ["Curse of the Cursed"] = 5, ["Curse of the Maze"] = 6, ["Curse of the Blind"] = 7,
    ["Curse of the Giant!"] = 8}
  function Isaac.GetCurseIdByName(name) return CURSES[name] or -1 end
  X.cb_state = {}
  function Isaac.GetBuiltInCallbackState(cb) return X.cb_state[cb] ~= false end
  function Isaac.SetBuiltInCallbackState(cb, v) X.cb_state[cb] = v ~= false end
  local add_pill = native(NS .. "8ItemPool18ForceAddPillEffectENS_11ePillEffectE", "ipi", "Isaac.AddPillEffectToPool", 0)
  function Isaac.AddPillEffectToPool(effect) return add_pill(offset(gp(), O.Game_ItemPool), effect) end
  function Input.GetButtonValue(button, ctrl) return Input.IsButtonPressed(button, ctrl) and 1 or 0 end
  function config:ShouldAddCostumeOnPickup(item) return item ~= nil and item.AddCostumeOnPickup == true end
end
-- DepthOffset +0x32c: Entity::GetRenderZ adds it to Position.Y
field("DepthOffset", 0x32c, "f32")
-- Entity::Update: velocity *= TimeScaledFriction(+0x340), then +0x340 = base friction +0x33c (reset every frame, as on PC)
field("Friction", 0x340, "f32")
-- Entity::get_collision_mass starts from +0x350
field("Mass", 0x350, "f32")
-- Entity_Player::add_samson_berserk_charge: i32 +0x2840
field("SamsonBerserkCharge", 0x2840, "i32", 1)
-- TargetPosition: Entity +0x308 (PC Entity+0x28c, the +0x7c shift of Velocity/Size/Mass/Parent; NPC AI reads it)
EG.TargetPosition = function(e) return peek(ptr(e), 0x308, "vec") end
ES.TargetPosition = function(e, v) poke(ptr(e), 0x308, "vec", v) end
-- coverage batch 5: the entity methods the 0.62 check missed (its fallback took the class globals,
-- which forward any name, for real methods). Offsets from the Switch code named at each line.
do
  local X = {}
  local function is_npc(e) local t = e.Type; return npc_type(t) end
  local function at(e, o, ty) return peek(ptr(e), o, ty) end
  local function put(e, o, ty, v) poke(ptr(e), o, ty, v) end
  local function int(v, d) return math.tointeger(v) or (type(v) == "number" and math.floor(v)) or d end
  function X.str(s)  -- libc++ long-form std::string over a copy of s (keep the results alive)
    s = tostring(s or "")
    local n = #s
    local chars, str = C.buf(n + 1), C.buf(24)
    for i = 1, n do poke(chars, i - 1, "u8", s:byte(i)) end
    poke(str, 0, "i64", ((n + 16) & ~15) | 1); poke(str, 8, "i64", n); poke(str, 16, "p", chars)
    return str, chars
  end
  function X.colormod(c)  -- ColorMod: tint RGBA +0, colorize RGBA +16, offset RGB +32
    local b = C.buf(0x40)
    c = c or {}
    poke(b, 0, "f32", c.R or 1); poke(b, 4, "f32", c.G or 1); poke(b, 8, "f32", c.B or 1); poke(b, 12, "f32", c.A or 1)
    poke(b, 16, "f32", c.CR or 0); poke(b, 20, "f32", c.CG or 0); poke(b, 24, "f32", c.CB or 0); poke(b, 28, "f32", c.CA or 0)
    poke(b, 32, "f32", c.RO or 0); poke(b, 36, "f32", c.GO or 0); poke(b, 40, "f32", c.BO or 0)
    return b
  end
  function X.color_at(p)
    local c = Color(peek(p, 0, "f32"), peek(p, 4, "f32"), peek(p, 8, "f32"), peek(p, 12, "f32"),
      peek(p, 32, "f32"), peek(p, 36, "f32"), peek(p, 40, "f32"))
    if c.SetColorize then c:SetColorize(peek(p, 16, "f32"), peek(p, 20, "f32"), peek(p, 24, "f32"), peek(p, 28, "f32")) end
    return c
  end
  function X.ref(src)  -- an engine EntityRef (0x30 bytes) from a Lua EntityRef or entity
    local r = C.buf(0x30)
    src = src and C.unbox(src) or {}
    poke(r, 0, "i32", src.Type or 0); poke(r, 4, "i32", src.Variant or 0)
    poke(r, 8, "i32", src.SpawnerType or 0); poke(r, 0xc, "i32", src.SpawnerVariant or 0)
    local pos, vel = src.Position or Vector(0, 0), src.Velocity or Vector(0, 0)
    poke(r, 0x10, "f32", pos.X); poke(r, 0x14, "f32", pos.Y); poke(r, 0x18, "f32", vel.X); poke(r, 0x1c, "f32", vel.Y)
    poke(r, 0x20, "u32", (src.IsCharmed and 1 or 0) | (src.IsFriendly and 2 or 0))
    if src.Entity and ptr(src.Entity) then poke(r, 0x28, "p", ptr(src.Entity)) end
    return r
  end

  ---------------------------------------------------------------- Entity
  -- the entity's EntityConfig::Entity is at +0x300 (Entity::load_entity_config); bossID +52 (EntityConfig::Load)
  function EM.GetBossID(e) local c = at(e, 0x300, "p"); return c and peek(c, 52, "i32") or 0 end
  -- Entity::CollideWithGrid clears u8 +0x1ec at the start of the frame's grid collision
  function EM.CollidesWithGrid(e) return at(e, 0x1ec, "u8") ~= 0 end
  function EM.AddVelocity(e, v) e.Velocity = e.Velocity + v end
  function EM.MultiplyFriction(e, f) e.Friction = e.Friction * f end
  -- Entity::Die is not exported on Switch: the entity is killed (no separate death animation step)
  -- Die: PC only sets the entity's dead flag (isaac-ng.exe 0x436b60: Entity+0x16b = 1); Switch dead flag +0x1c3
  function EM.Die(e) if ptr(e) then poke(ptr(e), 0x1c3, "u8", 1) end end
  function EM.KillWithSource(e) e:Kill() end
  function EM.SetSpriteFrame(e, anim, frame) e:GetSprite():SetFrame(anim, int(frame, 0)) end
  function EM.SetSpriteOverlayFrame(e, anim, frame) e:GetSprite():SetOverlayFrame(anim, int(frame, 0)) end
  X.slowing = native(NS .. "6Entity10AddSlowingERKNS0_9EntityRefEifNS_8ColorModE", "_ppifp", "Entity.AddSlowing")
  function EM.AddSlowing(e, src, dur, value, color)
    X.slowing(ptr(e), X.ref(src), int(dur, 0), value or 0.5, X.colormod(color))
  end

  ---------------------------------------------------------------- EntityBomb
  -- Entity_Bomb::Init: fuse i32 +0x400 (45 frames)
  function EM.SetExplosionCountdown(e, n) if e.Type == 4 then put(e, 0x400, "i32", int(n, 0)) end end

  ---------------------------------------------------------------- EntityEffect
  X.creep = native(NS .. "13Entity_Effect13IsPlayerCreepEi", "bi", "EntityEffect.IsPlayerCreep", false)
  function EM.IsPlayerCreep(e, variant)
    if type(e) == "number" then return X.creep(e) end
    return X.creep(int(variant, e.Variant))
  end
  if EntityEffect then rawset(EntityEffect, "IsPlayerCreep", function(v) return X.creep(int(v, 0)) end) end
  function EM.SetTimeout(e, t) e.Timeout = int(t, 0) end
  function EM.SetDamageSource(e, t) e.DamageSource = t end
  function EM.SetRadii(e, mn, mx) e.MinRadius = mn; e.MaxRadius = mx end

  ---------------------------------------------------------------- EntityFamiliar
  X.fam_fire = native(NS .. "15Entity_Familiar16fire_projectilesERKN4KAGE4Math7Vector2Eb", "epVb", "EntityFamiliar.FireProjectile")
  function EM.FireProjectile(e, dir) if e.Type == 3 then return X.fam_fire(ptr(e), dir, false) end end
  X.orbit_dist = native(NS .. "15Entity_Familiar18get_orbit_distanceEi", "vi", "EntityFamiliar.GetOrbitDistance")
  -- PC binds it static: EntityFamiliar.GetOrbitDistance(layer) (Fiend Folio), fam:GetOrbitDistance(layer) kept
  function EM.GetOrbitDistance(e, layer)
    if type(e) == "number" then layer = e end
    return X.orbit_dist(int(layer, 0))
  end
  if EntityFamiliar then rawset(EntityFamiliar, "GetOrbitDistance", EM.GetOrbitDistance) end
  X.orbit_recalc = native(NS .. "15Entity_Familiar26recalc_orbit_layer_offsetsEib", "_pib", "EntityFamiliar.RecalculateOrbitOffset")
  function EM.RecalculateOrbitOffset(e, layer, add) if e.Type == 3 then X.orbit_recalc(ptr(e), int(layer, 0), add == true) end; return 0 end

  ---------------------------------------------------------------- EntityKnife
  -- Entity_Knife::Shoot(charge, range): distance +0x434 (reset to 0), velocity +0x438;
  -- InitHomingPath: path offset +0x4e4, follow speed +0x4e8 (inferred from the PC layout +0x20)
  function EM.GetKnifeDistance(e) return e.Type == 8 and at(e, 0x434, "f32") or 0 end
  function EM.GetKnifeVelocity(e) return e.Type == 8 and at(e, 0x438, "f32") or 0 end
  function EM.SetPathFollowSpeed(e, v) if e.Type == 8 then put(e, 0x4e8, "f32", v) end end
  EG.PathFollowSpeed = function(e) if e.Type == 8 then return at(e, 0x4e8, "f32") end end
  ES.PathFollowSpeed = function(e, v) if e.Type == 8 then put(e, 0x4e8, "f32", v) end end
  EG.PathOffset = function(e) if e.Type == 8 then return at(e, 0x4e4, "f32") end end
  ES.PathOffset = function(e, v) if e.Type == 8 then put(e, 0x4e4, "f32", v) end end
  local rz = {[7] = native(NSK .. "12Entity_Laser10GetRenderZEv", "fp", "EntityLaser.GetRenderZ", 0),
              [8] = native(NSK .. "12Entity_Knife10GetRenderZEv", "fp", "EntityKnife.GetRenderZ", 0)}
  local rz_ent = native(NSK .. "6Entity10GetRenderZEv", "fp", "Entity.GetRenderZ", 0)
  function EM.GetRenderZ(e) local f = rz[e.Type] or rz_ent; return f(ptr(e)) end

  ---------------------------------------------------------------- EntityLaser
  -- Laser: TearFlags +0x418, Timeout +0x448, SampleLaser u8 +0x44d, MaxDistance +0x4f8, HomingType +0x4fc;
  -- after the HomingLaser the Switch layout is the PC one +0x28: MultidimensionalTouched u8 +0x528, OneHit u8 +0x53d (inferred)
  local function laser(e) return e.Type == 7 end
  function EM.SetMaxDistance(e, d) if laser(e) then put(e, 0x4f8, "f32", d) end end
  function EM.SetHomingType(e, t) if laser(e) then put(e, 0x4fc, "u32", int(t, 0)) end end
  EG.HomingType = function(e) if laser(e) then return at(e, 0x4fc, "u32") end end
  ES.HomingType = function(e, v) if laser(e) then put(e, 0x4fc, "u32", int(v, 0)) end end
  function EM.IsSampleLaser(e) return laser(e) and at(e, 0x44d, "u8") ~= 0 end
  function EM.SetOneHit(e, v) if laser(e) then put(e, 0x53d, "u8", v and 1 or 0) end end
  EG.OneHit = function(e) if laser(e) then return at(e, 0x53d, "u8") ~= 0 end end
  ES.OneHit = function(e, v) if laser(e) then put(e, 0x53d, "u8", v and 1 or 0) end end
  function EM.SetMultidimensionalTouched(e, v) if laser(e) then put(e, 0x528, "u8", v and 1 or 0) end end
  function EM.SetBlackHpDropChance(e, v) e.BlackHpDropChance = v end
  -- circle lasers (Tech X, Brimstone rings) have a radius
  function EM.IsCircleLaser(e) return laser(e) and (e.Radius or 0) > 0 end
  -- Entity_Laser::GetSamples: the std::vector<Vector2> at +0x478; GetNonOptimizedSamples returns its own
  local _, new_vl = class("VectorList", {
    Get = function(l, i) return C.unbox(l)[int(i, 0) + 1] end,
    __len = function(l) return #C.unbox(l) end,
  })
  local function samples(v)
    local t = {}
    if v then
      local b, en = peek(v, 0, "i64"), peek(v, 8, "i64")
      if b and en and b ~= 0 then
        local n = math.max(0, math.min((en - b) // 8, 4096))
        for i = 0, n - 1 do t[#t + 1] = Vector(peek(b, i * 8, "f32"), peek(b, i * 8 + 4, "f32")) end
      end
    end
    local l = new_vl(t)
    return l
  end
  X.samples = native(NS .. "12Entity_Laser10GetSamplesEv", "pp", "EntityLaser.GetSamples")
  X.samples_all = native(NS .. "12Entity_Laser22GetNonOptimizedSamplesEv", "pp", "EntityLaser.GetNonOptimizedSamples")
  function EM.GetSamples(e) return samples(laser(e) and X.samples(ptr(e)) or nil) end
  function EM.GetNonOptimizedSamples(e) return samples(laser(e) and X.samples_all(ptr(e)) or nil) end
  -- GetEndPoint: PC returns the EndPoint field (isaac-ng.exe 0x7a7f50: lea eax, [ecx+0x428]); Switch +0x500
  function EM.GetEndPoint(e)
    if not laser(e) then return e.Position end
    return at(e, 0x500, "vec") or e.Position
  end
  EG.EndPoint = function(e) if laser(e) then return EM.GetEndPoint(e) end end
  function EM.CalculateEndPoint(e, start, dir, off, parent, margin)
    local r = Game():GetRoom()
    local p = start + (off or Vector(0, 0))
    local d = dir:Normalized()
    local maxd = laser(e) and at(e, 0x4f8, "f32") or 0
    if maxd <= 0 then maxd = 2000 end
    local step, cur = 10, p
    for _ = 1, math.floor(maxd / step) do
      local nx = cur + d * step
      if not r:IsPositionInRoom(nx, -(margin or 0)) then break end
      cur = nx
    end
    return cur
  end

  ---------------------------------------------------------------- EntityNPC
  -- ProjectileParams: same layout on Switch as on PC (Entity_NPC::fire_projectiles), 0xa8 bytes
  function X.pparams(pp)
    local b = C.buf(0xb0)
    pp = pp and C.unbox(pp) or {}
    poke(b, 0, "u8", pp.GridCollision == false and 0 or 1)
    poke(b, 4, "f32", pp.HeightModifier or 0); poke(b, 8, "f32", pp.FallingSpeedModifier or 0)
    poke(b, 0xc, "f32", pp.FallingAccelModifier or 0); poke(b, 0x10, "f32", pp.VelocityMulti or 1)
    poke(b, 0x14, "f32", pp.Scale or 1); poke(b, 0x18, "f32", pp.CircleAngle or 0)
    poke(b, 0x1c, "f32", pp.HomingStrength or 0); poke(b, 0x20, "f32", pp.CurvingStrength or 0)
    poke(b, 0x24, "f32", pp.Acceleration or 1); poke(b, 0x28, "f32", pp.Spread or 1)
    local c = pp.Color or {}
    poke(b, 0x2c, "f32", c.R or 1); poke(b, 0x30, "f32", c.G or 1); poke(b, 0x34, "f32", c.B or 1); poke(b, 0x38, "f32", c.A or 1)
    poke(b, 0x3c, "f32", c.CR or 0); poke(b, 0x40, "f32", c.CG or 0); poke(b, 0x44, "f32", c.CB or 0); poke(b, 0x48, "f32", c.CA or 0)
    poke(b, 0x4c, "f32", c.RO or 0); poke(b, 0x50, "f32", c.GO or 0); poke(b, 0x54, "f32", c.BO or 0)
    poke(b, 0x58, "i64", int(pp.BulletFlags, 0))
    local po, tp, fd = pp.PositionOffset or Vector(0, 0), pp.TargetPosition or Vector(0, 0), pp.FireDirectionLimit or Vector(0, 0)
    poke(b, 0x60, "f32", po.X); poke(b, 0x64, "f32", po.Y); poke(b, 0x68, "f32", tp.X); poke(b, 0x6c, "f32", tp.Y)
    poke(b, 0x70, "f32", fd.X); poke(b, 0x74, "f32", fd.Y); poke(b, 0x78, "f32", pp.DotProductLimit or 0)
    poke(b, 0x7c, "i32", int(pp.WiggleFrameOffset, 0)); poke(b, 0x80, "i64", int(pp.ChangeFlags, 0))
    poke(b, 0x88, "f32", pp.ChangeVelocity or 0); poke(b, 0x8c, "u32", int(pp.ChangeTimeout, 0))
    poke(b, 0x90, "f32", pp.DepthOffset or 0); poke(b, 0x94, "u32", int(pp.Variant, 0)); poke(b, 0x9c, "f32", pp.Damage or 1)
    return b
  end
  X.fire = native(NS .. "10Entity_NPC16fire_projectilesERKN4KAGE4Math7Vector2ES5_NS0_16eProjectilesModeERKNS0_16ProjectileParamsE",
    "_pVVip", "EntityNPC.FireProjectiles")
  function EM.FireProjectiles(e, pos, vel, mode, params)
    if is_npc(e) then X.fire(ptr(e), pos, vel, int(mode, 0), X.pparams(params)) end
  end
  X.fire_boss = native(NS .. "10Entity_NPC21fire_boss_projectilesEiRKN4KAGE4Math7Vector2EfRKNS0_16ProjectileParamsE",
    "epiVfp", "EntityNPC.FireBossProjectiles")
  function EM.FireBossProjectiles(e, n, target, trajectory, params)
    if is_npc(e) then return X.fire_boss(ptr(e), int(n, 1), target, trajectory or 0, X.pparams(params)) end
  end
  X.splat = native(NS .. "6Entity10make_splatEf", "epf", "EntityNPC.MakeSplat")
  function EM.MakeSplat(e, size) return X.splat(ptr(e), size or 1) end
  X.spider = native(NS .. "10Entity_NPC11ThrowSpiderERKN4KAGE4Math7Vector2EPNS_6EntityES3_bf", "eVpvbf", "EntityNPC.ThrowSpider")
  function EM.ThrowSpider(...)
    local a = {...}
    if type(a[1]) == "userdata" and a[1].Type then table.remove(a, 1) end  -- method or static form
    local pos, spawner, target, big, yoff = a[1], a[2], a[3], a[4], a[5]
    return X.spider(pos, spawner and ptr(spawner) or nil, target or pos, big == true, yoff or 0)
  end
  if EntityNPC then rawset(EntityNPC, "ThrowSpider", EM.ThrowSpider) end
  local function list_npcs(pred)
    local t = {}
    for _, x in ipairs(Isaac.GetRoomEntities()) do
      local n = npc_type(x.Type) and x:ToNPC()
      if n and pred(n) then t[#t + 1] = n end
    end
    return t
  end
  function EM.QueryNPCsType(e, t, v) v = int(v, -1); return list_npcs(function(n) return n.Type == t and (v < 0 or n.Variant == v) end) end
  function EM.QueryNPCsSpawnerType(e, st, t, only_enemies)
    return list_npcs(function(n) return n.SpawnerType == st and (t == nil or t < 0 or n.Type == t) and (not only_enemies or n:IsEnemy()) end)
  end
  function EM.QueryNPCsGroup(e, g) return list_npcs(function(n) return n.GroupIdx == g end) end
  function EM.ResetPathFinderTarget(e) local pf = e.Pathfinder; if pf and pf.Reset then pf:Reset() end end
  -- Entity_NPC::anim_walkframe(horizontal, vertical, speed threshold): the same choice in Lua
  function EM.AnimWalkFrame(e, h, v, thr)
    local s, vel = e:GetSprite(), e.Velocity
    if vel:Length() < (thr or 0.1) then s:SetFrame(v, 0); return end
    local anim = math.abs(vel.X) > math.abs(vel.Y) and h or v
    if not s:IsPlaying(anim) then s:Play(anim, true) end
    s.FlipX = anim == h and vel.X < 0
  end

  ---------------------------------------------------------------- EntityProjectile / EntityTear
  -- projectile Height +0x400, FallingSpeed +0x404, FallingAccel +0x408, ChangeFlags u64 +0x438 (Entity_NPC::fire_projectiles)
  local function proj(e) return e.Type == 9 end
  function EM.AddHeight(e, v) if proj(e) then put(e, 0x400, "f32", at(e, 0x400, "f32") + v) end end
  function EM.AddFallingSpeed(e, v) if proj(e) then put(e, 0x404, "f32", at(e, 0x404, "f32") + v) end end
  function EM.AddFallingAccel(e, v) if proj(e) then put(e, 0x408, "f32", at(e, 0x408, "f32") + v) end end
  function EM.AddChangeFlags(e, f) if proj(e) then put(e, 0x438, "i64", at(e, 0x438, "i64") | int(f, 0)) end end
  function EM.AddScale(e, v) e.Scale = (e.Scale or 1) + v end
  function EM.SetKnockbackMultiplier(e, v) e.KnockbackMultiplier = v end
  function EM.SetWaitFrames(e, v) e.WaitFrames = int(v, 0) end
  function EM.SetParentOffset(e, v) e.ParentOffset = v end

  ---------------------------------------------------------------- EntityPlayer
  local function pl(e) return e.Type == 1 end
  -- TearParams (returned through x8 by Entity_Player::GetTearHitParams; same layout as on PC):
  -- variant +0, bomb variant +4, ColorMod +8, TearFlags +0x38, damage +0x48, scale +0x4c, height +0x50
  local _, new_tp = class("TearParams", {})
  X.hit_params = native(EP .. "16GetTearHitParamsENS_11eWeaponTypeEfiPNS_6EntityE", "Rpifip", "EntityPlayer.GetTearHitParams")
  function EM.GetTearHitParams(e, wt, scale, disp, src)
    local b = pl(e) and X.hit_params(ptr(e), int(wt, 1), scale or 1, int(disp, 1), src and ptr(src) or nil)
    if not b then
      return new_tp({TearVariant = 0, BombVariant = 0, TearColor = Color(1, 1, 1, 1), TearFlags = bs(0, 0),
        TearDamage = e.Damage or 3.5, TearScale = 1, TearHeight = -23.75})
    end
    return new_tp({TearVariant = peek(b, 0, "i32"), BombVariant = peek(b, 4, "i32"), TearColor = X.color_at(offset(b, 8)),
      TearFlags = bs(peek(b, 0x38, "i64"), peek(b, 0x40, "i64")), TearDamage = peek(b, 0x48, "f32"),
      TearScale = peek(b, 0x4c, "f32"), TearHeight = peek(b, 0x50, "f32")})
  end
  -- Weapon::MultiShotParams (x8; PC layout): tears i16 +0, lanes i16 +2, spread tears/laser/techx/knife +4..+0x10,
  -- eyes i32 +0x14, eye angle +0x18, cross-eyed +0x1c, backwards +0x1d, sideways +0x1e, random tears i16 +0x20
  local msp_methods = {}
  local _, new_msp = class("MultiShotParams", msp_methods)
  function msp_methods.GetNumTears(m) return C.unbox(m).tears end
  function msp_methods.SetNumTears(m, v) C.unbox(m).tears = v end
  function msp_methods.GetNumLanesPerEye(m) return C.unbox(m).lanes end
  function msp_methods.SetNumLanesPerEye(m, v) C.unbox(m).lanes = v end
  function msp_methods.GetSpreadAngle(m, wt)
    local u = C.unbox(m)
    wt = int(wt, 1)
    if wt == 2 then return u.spread[2] elseif wt == 9 then return u.spread[3] elseif wt == 4 or wt == 10 then return u.spread[4] end
    return u.spread[1]
  end
  function msp_methods.GetNumEyesActive(m) return C.unbox(m).eyes end
  function msp_methods.GetMultiEyeAngle(m) return C.unbox(m).eye_angle end
  function msp_methods.IsCrossEyed(m) return C.unbox(m).cross end
  function msp_methods.IsShootingBackwards(m) return C.unbox(m).back end
  function msp_methods.IsShootingSideways(m) return C.unbox(m).side end
  function msp_methods.GetNumRandomDirTears(m) return C.unbox(m).random end
  X.msp = native(EP .. "18GetMultiShotParamsENS_11eWeaponTypeE", "Rpi", "EntityPlayer.GetMultiShotParams")
  function EM.GetMultiShotParams(e, wt)
    local b = pl(e) and X.msp(ptr(e), int(wt, 1))
    if not b then return new_msp({tears = 1, lanes = 1, spread = {0, 0, 0, 0}, eyes = 1, eye_angle = 0, cross = false, back = false, side = false, random = 0}) end
    return new_msp({tears = peek(b, 0, "i16"), lanes = peek(b, 2, "i16"),
      spread = {peek(b, 4, "f32"), peek(b, 8, "f32"), peek(b, 12, "f32"), peek(b, 16, "f32")},
      eyes = peek(b, 0x14, "i32"), eye_angle = peek(b, 0x18, "f32"), cross = peek(b, 0x1c, "u8") ~= 0,
      back = peek(b, 0x1d, "u8") ~= 0, side = peek(b, 0x1e, "u8") ~= 0, random = peek(b, 0x20, "i16")})
  end
  -- PosVel (four floats, returned in s0-s3, which the FFI cannot take): the spread is computed here
  function EM.GetMultiShotPositionVelocity(e, i, wt, dir, speed, params)
    local n = params and params:GetNumTears() or 1
    local spread = params and params:GetSpreadAngle(wt) or 0
    local ang = (n > 1) and (int(i, 0) - (n - 1) / 2) * spread or 0
    local v = dir:Rotated(ang):Resized(speed or 1)
    return {Position = Vector(0, 0), Velocity = v}
  end
  -- Entity_Player::TakeDamage: damage cooldown +0x1734, last source EntityRef +0x1a70, last flags u64 +0x1aa0,
  -- damage taken +0x1aa8 (run) / +0x1aac
  function EM.GetDamageCooldown(e) return pl(e) and at(e, 0x1734, "i32") or 0 end
  function EM.ResetDamageCooldown(e) if pl(e) then put(e, 0x1734, "i32", 0) end end
  function EM.SetMinDamageCooldown(e, n)
    if pl(e) and at(e, 0x1734, "i32") < int(n, 0) then put(e, 0x1734, "i32", int(n, 0)) end
  end
  function EM.GetLastDamageFlags(e) return pl(e) and at(e, 0x1aa0, "i64") or 0 end
  function EM.GetLastDamageSource(e) return _isaaclua_entref(pl(e) and offset(ptr(e), 0x1a70) or nil) end
  function EM.GetTotalDamageTaken(e) return pl(e) and at(e, 0x1aa8, "i32") or 0 end
  -- AddJarFlies: i32 +0x16d0; set_itemstate: i32 +0x19d4; update_canshoot: u8 +0x1c9d
  function EM.GetJarFlies(e) return pl(e) and at(e, 0x16d0, "i32") or 0 end
  function EM.GetItemState(e) return pl(e) and at(e, 0x19d4, "i32") or 0 end
  function EM.CanShoot(e) return pl(e) and at(e, 0x1c9d, "u8") ~= 0 end
  X.update_canshoot = native(EP .. "15update_canshootEv", "_p", "EntityPlayer.UpdateCanShoot")
  function EM.UpdateCanShoot(e) if pl(e) then X.update_canshoot(ptr(e)) end end
  -- tear poison damage: f32 +0x1960 (written by EvaluateItems; inferred from the PC layout)
  function EM.GetTearPoisonDamage(e) return pl(e) and at(e, 0x1960, "f32") or 0 end
  -- pocket slots {id, type} at +0x1c10 (Entity_Player::add_pocketitem)
  local _, new_pi = class("PlayerPocketItem", {
    GetSlot = function(p) return C.unbox(p).slot end,
    GetType = function(p) return C.unbox(p).type end,
  })
  function EM.GetPocketItem(e, slot)
    slot = int(slot, 0)
    if not pl(e) or slot < 0 or slot > 3 then return new_pi({slot = 0, type = 0}) end
    return new_pi({slot = at(e, 0x1c10 + 8 * slot, "i32"), type = at(e, 0x1c14 + 8 * slot, "i32")})
  end
  X.bomb_variant = native(EPK .. "14GetBombVariantENS_11bitset128_tEb", "ipiib", "EntityPlayer.GetBombVariant", 0)
  function EM.GetBombVariant(e, flags, force_small)
    local l, h = halves(flags or bs(0, 0))
    return X.bomb_variant(ptr(e), l, h, force_small == true)
  end
  EM.AddRottenHearts = pnative(EP .. "15AddRottenHeartsEib", "_pib", "AddRottenHearts")
  local add_rotten = EM.AddRottenHearts
  EM.AddRottenHearts = function(e, n, flag) return add_rotten(e, int(n, 0), flag ~= false) end
  X.pitfall = native(EP .. "16AnimatePitfallInEb", "_pb", "EntityPlayer.AnimatePitfallIn")
  function EM.AnimatePitfallIn(e) if pl(e) then X.pitfall(ptr(e), false) end end
  X.discharge = native(EP .. "19DischargeActiveItemEib", "_pib", "EntityPlayer.DischargeActiveItem")
  function EM.DischargeActiveItem(e, slot) if pl(e) then X.discharge(ptr(e), int(slot, 0), false) end end
  -- Entity_Player::FullCharge returns the charge added (PC: whether anything was charged)
  X.full_charge = native(EP .. "10FullChargeEib", "ipib", "EntityPlayer.FullCharge", 0)
  function EM.FullCharge(e, slot, force) return pl(e) and X.full_charge(ptr(e), int(slot, 0), force == true) > 0 or false end
  X.queue = native(EP .. "9QueueItemEPKNS_10ItemConfig4ItemEihi", "_ppiii", "EntityPlayer.QueueItem")
  function EM.QueueItem(e, item, charge, touched, golden, vardata)
    local p = item and rawget(item, "_p")
    if pl(e) and p then X.queue(ptr(e), p, int(charge, 0), touched and 1 or 0, int(vardata, 0)) end
  end
  X.anim_anm = native(EP .. "13AnimatePickupERKNS_4ANM2EbPKc", "_ppbs", "EntityPlayer.AnimatePickup")
  function EM.AnimatePickup(e, spr, hide_shadow, anim)
    local a = spr and _isaaclua_sprite_anm(spr)
    if pl(e) and a then X.anim_anm(ptr(e), a, hide_shadow == true, anim or "Pickup") end
  end
  X.lua_turn = native(EP .. "15lua_CanTurnHeadEv", "bp", "EntityPlayer.CanTurnHead", true)
  function EM.CanTurnHead(e) return pl(e) and X.lua_turn(ptr(e)) end
  X.inherit = native(EPK .. "29get_tear_movement_inheritanceERKN4KAGE4Math7Vector2Eb", "vpVb", "EntityPlayer.GetTearMovementInheritance")
  function EM.GetTearMovementInheritance(e, dir) if pl(e) then return X.inherit(ptr(e), dir, false) end return Vector(0, 0) end
  X.laser_off = native(EPK .. "16get_laser_offsetENS0_12eLaserOffsetERKN4KAGE4Math7Vector2E", "vpiV", "EntityPlayer.GetLaserOffset")
  function EM.GetLaserOffset(e, id, dir) if pl(e) then return X.laser_off(ptr(e), int(id, 0), dir) end return Vector(0, 0) end
  X.null_pos = native(EPK .. "20get_costume_null_posERKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEEbRKN4KAGE4Math7Vector2E",
    "vppbV", "EntityPlayer.GetCostumeNullPos")
  function EM.GetCostumeNullPos(e, name, head_scale, dir)
    if not pl(e) then return Vector(0, 0) end
    local s, keep = X.str(name)
    local v = X.null_pos(ptr(e), s, head_scale == true, dir or Vector(0, 0))
    keep = nil
    return v
  end
  X.form_costume = native(EP .. "23add_player_form_costumeENS_11ePlayerFormE", "_pi", "EntityPlayer.AddPlayerFormCostume")
  function EM.AddPlayerFormCostume(e, form) if pl(e) then X.form_costume(ptr(e), int(form, 0)) end end
  X.replace_costume = native(EP .. "22replace_costume_spriteEPKNS_10ItemConfig4ItemERKNSt3__112basic_stringIcNS5_11char_traitsIcEENS5_9allocatorIcEEEEi",
    "_pppi", "EntityPlayer.ReplaceCostumeSprite")
  function EM.ReplaceCostumeSprite(e, item, path, layer)
    local p = item and rawget(item, "_p")
    if not (pl(e) and p) then return end
    local s, keep = X.str(path)
    X.replace_costume(ptr(e), p, s, int(layer, 0))
    keep = nil
  end
  X.swap = native(EP .. "16swap_active_itemEv", "_p", "EntityPlayer.SwapActiveItems")
  function EM.SwapActiveItems(e) if pl(e) then X.swap(ptr(e)) end end
  function EM.SetBloodCharge(e, n) e:AddBloodCharge(int(n, 0) - e:GetBloodCharge()) end
  function EM.SetSoulCharge(e, n) e:AddSoulCharge(int(n, 0) - e:GetSoulCharge()) end
  function EM.HasFullHeartsAndSoulHearts(e)
    return e:HasFullHearts() and e:GetEffectiveMaxHearts() + e:GetSoulHearts() + e:GetBoneHearts() * 2 >= e:GetHeartLimit()
  end
  function EM.AreOpposingShootDirectionsPressed(e)
    local c = e.ControllerIndex
    local function p(a) return Input.IsActionPressed(a, c) end
    return (p(ButtonAction.ACTION_SHOOTLEFT) and p(ButtonAction.ACTION_SHOOTRIGHT))
      or (p(ButtonAction.ACTION_SHOOTUP) and p(ButtonAction.ACTION_SHOOTDOWN))
  end
  function EM.GetShootingJoystick(e) return e:GetShootingInput() end
  function EM.GetMovementJoystick(e) return e:GetMovementInput() end
  function EM.GetRecentMovementVector(e) return e:GetMovementVector() end
  function EM.GetVelocityBeforeUpdate(e) return e.Velocity end
  function EM.GetSmoothBodyRotation(e) return 0 end
  function EM.GetLastDirection(e)
    local v = e:GetMovementInput()
    if v:Length() > 0.1 then return v:Normalized() end
    return Vector(0, 1)
  end
  function EM.GetLastActionTriggers(e) return 0 end
  function EM.GetModelingClayEffect(e) return 0 end
  function EM.GetTractorBeam(e) return nil end
  function EM.IsP2Appearing(e) return false end
  function EM.GetTearRangeModifier(e) return 0 end
  function EM.SetTargetTrapDoor(e, grid) end
  function EM.AddControlsCooldown(e, n) e.ControlsCooldown = (e.ControlsCooldown or 0) + int(n, 0) end
  function EM.GetNumBlueFlies(e)
    local n = 0
    for _, f in ipairs(Isaac.FindByType(3, 43)) do local x = f:ToFamiliar(); if x and x.Player and GetPtrHash(x.Player) == GetPtrHash(e) then n = n + 1 end end
    return n
  end
  function EM.GetNumBlueSpiders(e)
    local n = 0
    for _, f in ipairs(Isaac.FindByType(3, 73)) do local x = f:ToFamiliar(); if x and x.Player and GetPtrHash(x.Player) == GetPtrHash(e) then n = n + 1 end end
    return n
  end
  -- extra animations: playing u8 +0x1710 (Entity_Player::PlayExtraAnimation / IsExtraAnimationFinished)
  X.anim_queue = {}
  function EM.StopExtraAnimation(e)
    if not pl(e) then return end
    put(e, 0x1710, "u8", 0)
    X.anim_queue[GetPtrHash(e)] = nil
  end
  function EM.QueueExtraAnimation(e, name)
    if not pl(e) then return end
    if e:IsExtraAnimationFinished() then return e:PlayExtraAnimation(name) end
    local q = X.anim_queue[GetPtrHash(e)] or {}
    q[#q + 1] = name
    X.anim_queue[GetPtrHash(e)] = q
  end
  Isaac.AddCallback({Name = "isaaclua", _isaaclua_internal = true}, ModCallbacks.MC_POST_PLAYER_UPDATE, function(_, p)
    local q = X.anim_queue[GetPtrHash(p)]
    if q and p:IsExtraAnimationFinished() then
      p:PlayExtraAnimation(table.remove(q, 1))
      if #q == 0 then X.anim_queue[GetPtrHash(p)] = nil end
    end
  end)
end
-- the entity's sprite (ANM2 at +0x50; ANM2::Reset): Offset +0xd4, Rotation +0xe4, FlipX +0x140
field("SpriteOffset", 0x50 + 0xd4, "vec")
field("SpriteRotation", 0x50 + 0xe4, "f32")
field("FlipX", 0x50 + 0x140, "b")
function EM.HasTearFlags(e, f)
  local t = EG.TearFlags(e)
  return t ~= nil and (t & f) == bs(halves(f))
end
function EM.AddTearFlags(e, f) local t = EG.TearFlags(e); if t then ES.TearFlags(e, t | f) end end
function EM.ClearTearFlags(e, f) local t = EG.TearFlags(e); if t then ES.TearFlags(e, t & ~bs(halves(f))) end end

local random_u32 = native_or(NS .. "9RandomU32Ev", "u", function() return math.random(0, 0xffffffff) end)
function Random() return random_u32() end
-- a unit vector in a random direction, as on PC
function RandomVector() return Vector.FromAngle(random_u32() % 36000 / 100) end

local _, new_ref = class("EntityRef", {})
function EntityRef(e)
  return new_ref({Entity = e, Type = e and e.Type or 0, Variant = e and e.Variant or 0,
    SpawnerType = e and e.SpawnerType or 0, SpawnerVariant = e and e.SpawnerVariant or 0,
    Position = e and e.Position or Vector(0, 0), Velocity = e and e.Velocity or Vector(0, 0),
    IsCharmed = false, IsFriendly = false})
end
-- an engine EntityRef (layout at Entity::TakeDamage below), for ENTITY_TAKE_DMG's source
function _isaaclua_entref(r)
  if not r then return EntityRef(nil) end
  local e = peek(r, 0x28, "p")
  return new_ref({Entity = e and C.entity(e) or nil, Type = peek(r, 0, "i32"), Variant = peek(r, 4, "i32"),
    SpawnerType = peek(r, 8, "i32"), SpawnerVariant = peek(r, 0xc, "i32"),
    Position = Vector(peek(r, 0x10, "f32"), peek(r, 0x14, "f32")), Velocity = Vector(peek(r, 0x18, "f32"), peek(r, 0x1c, "f32")),
    IsCharmed = (peek(r, 0x20, "u32") & 1) ~= 0, IsFriendly = (peek(r, 0x20, "u32") & 2) ~= 0})
end
local _, new_ptr = class("EntityPtr", {SetReference = function(self, e) C.unbox(self).Ref = e end})
function EntityPtr(e) return new_ptr({Ref = e}) end
local _, new_pp = class("ProjectileParams", {})
function ProjectileParams()
  return new_pp({BulletFlags = 0, ChangeFlags = 0, ChangeTimeout = 0, CircleAngle = 0, Color = Color(1, 1, 1, 1),
    CurvingStrength = 0, DepthOffset = 0, DotProductLimit = 0, FallingAccelModifier = 0,
    FallingSpeedModifier = 0, GridCollision = true, HeightModifier = 0, HomingStrength = 0,
    PositionOffset = Vector(0, 0), Scale = 1, Spread = 1, TargetPosition = Vector(0, 0), Variant = 0,
    VelocityMulti = 1, WiggleFrequency = 0, WiggleFrameOffset = 0, Acceleration = 1, ChangeVelocity = 0,
    FireDirectionLimit = Vector(0, 0)})  -- the defaults the game's params get (X.pparams)
end

-- console commands: Switch has no console object in the game, so the commands mods use are done here
-- with the same engine calls as Console::RunCommand
local forget_stage_seed = native(NS .. "5Seeds15ForgetStageSeedENS_11eLevelStageE", "_pi", "Seeds.ForgetStageSeed")
local level_init = native(NS .. "5Level4InitEv", "_p", "Level.Init")
local level_update = native(NS .. "5Level6UpdateEv", "_p", "Level.Update")
local new_room_effects = native(NS .. "13PlayerManager31TriggerNewRoom_TemporaryEffectsEv", "_p",
  "PlayerManager.TriggerNewRoom_TemporaryEffects")
local commands = {}
function commands.reseed()  -- Console::RunCommand "reseed": a new layout for the current floor
  local g = gp()
  forget_stage_seed(offset(g, O.Game_Seeds), level:GetStage() + (level:IsAltStage() and 1 or 0))
  if not C.level_init() then level_init(g) end  -- through the hook: POST_CURSE_EVAL, POST_NEW_LEVEL
  level_update(g)
  new_room_effects(offset(g, O.Game_PlayerManager))
  return "Floor reseeded."
end
-- Console "addplayer [type] [controller]" (PC: PlayerManager::SpawnCoPlayer2 + controller index; the Switch build
-- has no such command, but PlayerManager::SpawnCoPlayer(ePlayerType) is there): a co-op player, for testing
commands.addplayer = (function()
  local spawn = native(NS .. "13PlayerManager13SpawnCoPlayerENS_11ePlayerTypeE", "ppi", "PlayerManager.SpawnCoPlayer")
  return function(rest)
    local t, c = rest:match("^(%-?%d*)%s*(%-?%d*)")
    local p = spawn(offset(gp(), O.Game_PlayerManager), tonumber(t) or 0)
    if not p then return "Could not add a player." end
    -- ControllerIndex (Entity_Player::SetControllerIndex): by default the first player's (-1 on Switch), else the
    -- game sees the new player's pad as unplugged and stops in the controller applet (PauseScreen::
    -- controller_disconnect_handling, HW 0.99.11 with "addplayer 0 1")
    local p0 = peek(peek(offset(gp(), O.Game_PlayerManager), 16, "p"), 0, "p")
    poke(p, 0x19ec, "i32", tonumber(c) or (p0 and peek(p0, 0x19ec, "i32")) or -1)
    return "Added player."
  end
end)()
-- a new run, as holding R (on Switch L3+R3). "restart N" = as character N (PC). Manager::RestartGame keeps
-- the player type/challenge/difficulty that Manager::StartNewGame stored at Manager+0x1d7308 (+4, +8, +0x80)
function commands.restart(arg)
  local n, m = tonumber(arg), C.managerptr()
  if n and m and n >= 0 then poke(m, 0x1d730c, "i32", math.floor(n)) end
  if not C.restart() then C.missing("Isaac.ExecuteCommand(\"restart\")") end
end
function Isaac.ExecuteCommand(cmd)
  local word, rest = tostring(cmd):match("^%s*(%S+)%s*(.-)%s*$")
  local f = word and commands[word:lower()]
  if not f then
    C.missing("Isaac.ExecuteCommand(\"" .. tostring(word) .. "\")")
    return ""
  end
  return f(rest) or ""
end

---------------------------------------------------------------- moving between rooms (GoodTrip)
-- Level::m_EnterDoor at Game+0x21570 (Entity_Player::GetEnterPosition reads it: -1 = no door);
-- m_LeaveDoor is taken to be the int before it, as on PC
local LEVEL_FIELDS = {EnterDoor = 0x21570, LeaveDoor = 0x2156c}
setmetatable(level, {
  __index = function(_, k)
    local off = LEVEL_FIELDS[k]
    if off then return peek(gp(), off, "i32") end
  end,
  __newindex = function(t, k, v)
    local off = LEVEL_FIELDS[k]
    if off then poke(gp(), off, "i32", v) else rawset(t, k, v) end
  end})
local change_room = native(NS .. "4Game10ChangeRoomEii", "_pii", "Game.ChangeRoom")
function game:ChangeRoom(idx, dim) change_room(gp(), idx, dim or -1) end
local start_transition = native(NS .. "4Game19StartRoomTransitionEiNS_10eDirectionENS_14RoomTransition10eAnimationEPNS_13Entity_PlayerEi",
  "_piiipi", "Game.StartRoomTransition")
function game:StartRoomTransition(idx, dir, anim, player, dim)
  -- RoomTransition::change_room reads the target's RoomConfig (desc +0x10) unchecked: a room without data
  -- crashed the console (HW 0.99.30, Fiend Folio run); such a transition is dropped
  local d = get_room_by_idx(gp(), math.floor(idx or 0), dim or -1)
  if not d or not peek(d, 0x10, "p") then
    C.missing("StartRoomTransition to room " .. tostring(idx) .. " without data (dropped)")
    return
  end
  start_transition(gp(), idx, dir or -1, anim or 1, player and ptr(player) or nil, dim or -1)
end
local remove_curses = native(NS .. "5Level12RemoveCursesEj", "_pu", "Level.RemoveCurses")
function level:RemoveCurses(mask) remove_curses(gp(), mask) end
local add_curse = native(NS .. "5Level8AddCurseENS_11eLevelCurseEb", "_pib", "Level.AddCurse")
function level:AddCurse(curse, show) add_curse(gp(), curse, show or false) end

-- doors: Room+0xe30 holds GridEntity_Door* per DoorSlot, Room+0xe70 their grid indexes (Room::RemoveDoor)
function room:GetDoor(slot)
  if slot < 0 or slot > 7 then return nil end
  local i = peek(roomp(), 0xe70 + 4 * slot, "i32")
  if not peek(roomp(), 0xe30 + 8 * slot, "p") or not i or i < 0 then return nil end
  return self:GetGridEntity(i)
end
function grid_mt:ToDoor() return self:GetType() == GridEntityType.GRID_DOOR and self or nil end
function grid_mt:IsOpen() return peek(rawget(self, "_p"), 0x10, "i32") == 2 end  -- DoorState.STATE_OPEN
-- locked: a locked variant (GridEntity_Door::SetLocked writes DOOR_LOCKED / DOOR_UNLOCKED) not yet open (inferred)
function grid_mt:IsLocked()
  local v = peek(rawget(self, "_p"), 0xc, "i32") or 0
  return v >= 1 and v <= 6 and peek(rawget(self, "_p"), 0x10, "i32") ~= 2
end
-- GridEntity_Door fields (GridEntity_Door::InitSubclass / SetRoomTypes / SetDirection, Room::make_door; PC order):
-- Slot +0x1a0, CurrentRoomType +0x1a4, TargetRoomType +0x1a8, Direction +0x1ac, TargetRoomIndex +0x30c,
-- PreviousState +0x310, PreviousVariant +0x314
do
  local DOOR = {Slot = 0x1a0, CurrentRoomType = 0x1a4, TargetRoomType = 0x1a8, Direction = 0x1ac,
    TargetRoomIndex = 0x30c, PreviousState = 0x310, PreviousVariant = 0x314}
  local idx = grid_mt.__index
  local function is_door(t) return peek(rawget(t, "_p"), O.Grid_Type, "i32") == GridEntityType.GRID_DOOR end
  function grid_mt.__index(t, k)
    local m = rawget(grid_mt, k)
    if m ~= nil then return m end
    local o = DOOR[k]
    if o and is_door(t) then return peek(rawget(t, "_p"), o, "i32") end
    return idx(t, k)
  end
  function grid_mt.__newindex(t, k, v)
    local o = DOOR[k] or (GRID_FIELDS[k] and GRID_FIELDS[k][1])
    if o and (GRID_FIELDS[k] or is_door(t)) then return poke(rawget(t, "_p"), o, "i32", math.tointeger(v) or 0) end
    rawset(t, k, v)
  end
  local D = NS .. "15GridEntity_Door"
  local DK = NSK .. "15GridEntity_Door"
  local function door_fn(name, sig, label, def)
    local f = native(name, sig, "GridEntityDoor." .. label, def)
    grid_mt[label] = function(self, ...) if is_door(self) then return f(rawget(self, "_p"), ...) end return def end
  end
  door_fn(D .. "4OpenEv", "_p", "Open")
  door_fn(D .. "5CloseEb", "_pb", "Close")
  door_fn(D .. "3BarEv", "_p", "Bar")
  door_fn(D .. "9SpawnDustEv", "_p", "SpawnDust")
  door_fn(D .. "9SetLockedEb", "_pb", "SetLocked")
  door_fn(D .. "11CanBlowOpenEv", "bp", "CanBlowOpen", false)
  door_fn(DK .. "18IsTargetRoomArcadeEv", "bp", "IsTargetRoomArcade", false)
  door_fn(D .. "12SetRoomTypesENS_9eRoomTypeES1_", "_pii", "SetRoomTypes")
  local unlock = native(D .. "9TryUnlockEPNS_13Entity_PlayerEb", "bppb", "GridEntityDoor.TryUnlock", false)
  function grid_mt:TryUnlock(player, force)
    return is_door(self) and unlock(rawget(self, "_p"), player and ptr(player) or nil, force or false) or false
  end
  local blow = native(D .. "11TryBlowOpenEbPNS_6EntityE", "bpbp", "GridEntityDoor.TryBlowOpen", false)
  function grid_mt:TryBlowOpen(from_explosion, source)
    return is_door(self) and blow(rawget(self, "_p"), from_explosion or false, source and ptr(source) or nil) or false
  end
  function grid_mt:IsRoomType(t)
    return is_door(self) and (self.CurrentRoomType == t or self.TargetRoomType == t) or false
  end
end

local is_flying = native(NSK .. "6Entity8IsFlyingEv", "bp", "Entity.IsFlying", false)
EM.IsFlying = function(e) return is_flying(ptr(e)) end
EM.SetShootingCooldown = pnative(EP .. "19SetShootingCooldownEi", "_pi", "SetShootingCooldown")
EM.PlayExtraAnimation = pnative(EP .. "18PlayExtraAnimationEPKc", "_ps", "PlayExtraAnimation")
EM.IsExtraAnimationFinished = pnative(EPK .. "24IsExtraAnimationFinishedEv", "bp", "IsExtraAnimationFinished", true)

-- Entity::TakeDamage(float, u64 flags, const EntityRef&, int): the subclass override is called directly.
-- EntityRef on Switch (Entity::IgnoreEffectFromFriendly, compute_poison_damage): Type, Variant,
-- SpawnerType, SpawnerVariant, Position, Velocity, flags (bit 0 charmed, bit 1 friendly) at +0x20, Entity* at +0x28
local TAKE_DAMAGE = "10TakeDamageEfmRKNS_6Entity9EntityRefEi"
local function take_damage_fn(t)
  if t == 1 then return NS .. "13Entity_Player" .. TAKE_DAMAGE
  elseif t == 3 then return NS .. "15Entity_Familiar" .. TAKE_DAMAGE
  elseif t == 5 then return NS .. "13Entity_Pickup" .. TAKE_DAMAGE
  elseif t == 6 then return NS .. "11Entity_Slot" .. TAKE_DAMAGE
  elseif npc_type(t) then return NS .. "10Entity_NPC" .. TAKE_DAMAGE end
  return NS .. "6Entity10TakeDamageEfmRKNS0_9EntityRefEi"
end
EM.TakeDamage = function(e, amount, flags, source, countdown)
  local f = sym(take_damage_fn(e.Type))
  if not f then C.missing("Entity.TakeDamage"); return false end
  local r = C.buf(0x30)
  local src = source and C.unbox(source) or {}
  local se = src.Entity
  poke(r, 0, "i32", src.Type or 0); poke(r, 4, "i32", src.Variant or 0)
  poke(r, 8, "i32", src.SpawnerType or 0); poke(r, 0xc, "i32", src.SpawnerVariant or 0)
  local pos, vel = src.Position or Vector(0, 0), src.Velocity or Vector(0, 0)
  poke(r, 0x10, "f32", pos.X); poke(r, 0x14, "f32", pos.Y); poke(r, 0x18, "f32", vel.X); poke(r, 0x1c, "f32", vel.Y)
  poke(r, 0x20, "u32", (src.IsCharmed and 1 or 0) | (src.IsFriendly and 2 or 0))
  if se and ptr(se) then poke(r, 0x28, "p", ptr(se)) end
  return call(f, "bpfipi", ptr(e), amount, flags or 0, r, countdown or 0)
end

---------------------------------------------------------------- entities from a mod's content/entities2.xml
-- The Switch engine reads no mod content/, so the entities a mod adds there live on the Lua side:
-- Isaac.Spawn of one makes a Lua entity with its own Sprite, moved and updated every game frame
-- (POST_EFFECT_INIT/UPDATE/RENDER run for it as for a real effect) and drawn right after the entity it
-- follows (FollowParent), or before the mods' POST_RENDER. Isaac.FindByType returns them too.
local mod_entities = {}  -- by name and by "type.variant"
-- PC EntityConfig::Load for a mod (isaac-ng.exe 0x670df0..0x671113): while the key type.variant.subtype is
-- taken, effects (1000), 9001 and the classes 1-9 take the next variant, anything else the next type
-- (skipping 1000 and 9001). Fiend Folio's projectiles all say variant 0: Briar Thistle is 9.19 on PC and in
-- the Switch engine's own load, but was 9.0 here, so every plain projectile's removal ran the thistle's
-- death (3 new projectiles each): HW 0.99.41 China hang. The engine's entry with this name is the answer;
-- without one (no engine content) the same rule over the engine's and our keys.
do  -- the main chunk is at Lua's 200-locals limit
  local ec_get_entity = native(NSK .. "12EntityConfig9GetEntityENS_11eEntityTypeEjj", "ppiuu", "EntityConfig.GetEntity")
  local mod_keys = {}
  local function assign_key(def)
    local m = C.managerptr()
    if not m then return end
    local ec = offset(m, O.Manager_EntityConfig)
    local t, v, st = def.Type, def.Variant, def.SubType
    for _ = 1, 4096 do
      local key = (t << 20 | v << 8 | st) & 0xffffffff
      local e = not mod_keys[key] and ec_get_entity(ec, t, v, st)
      if e and peek(e, -8, "u32") ~= key then e = nil end  -- GetEntity's fallback to subtype/variant 0
      if not mod_keys[key] and (not e or not def.Name or peek(e, 0x10, "str") == def.Name) then
        mod_keys[key] = true
        def.Type, def.Variant = t, v
        return
      end
      if t == 1000 or t == 9001 or t < 10 then v = v + 1
      else t = t + 1; if t == 1000 or t == 9001 then t = t + 1 end end
    end
  end
  content_loaders[#content_loaders + 1] = function(dir)
    local xml = C.readfile(dir .. "content/entities2.xml")
    if not xml then return end
    local root = xml:match("<entities[^>]-anm2root%s*=%s*\"([^\"]*)\"") or "gfx/"
    for attrs in xml:gmatch("<entity%s([^>]*)>") do
      local a = {}
      for k, v in attrs:gmatch("([%w_]+)%s*=%s*\"([^\"]*)\"") do a[k] = v end
      local def = {Name = a.name, Type = tonumber(a.id) or 1000, Variant = tonumber(a.variant) or 0,
        SubType = tonumber(a.subtype) or 0, Anm2 = a.anm2path and (root .. a.anm2path) or nil}
      assign_key(def)
      if def.Name then mod_entities[def.Name] = def end
      mod_entities[def.Type .. "." .. def.Variant] = def
    end
  end
end
function Isaac.GetEntityVariantByName(name) local d = mod_entities[name]; return d and d.Variant or -1 end
function Isaac.GetEntityTypeByName(name) local d = mod_entities[name]; return d and d.Type or 0 end

local lua_ents = {}      -- spawned, in spawn order
local lent_methods = {}
local lent_mt = {__type = "EntityEffect"}
function lent_mt.__index(t, k)
  local m = lent_methods[k]
  if m ~= nil then return m end
  local f = rawget(t, "_f")
  local v = f[k]
  if v ~= nil then return v end
  if k == "SpawnerEntity" or k == "Parent" then
    local e = f["_" .. k]
    if e and e:Exists() then return e end
    return nil
  end
  if k == "SpawnerType" then local e = f._SpawnerEntity; return e and e.Type or 0 end
  if k == "SpawnerVariant" then local e = f._SpawnerEntity; return e and e.Variant or 0 end
  C.missing("EntityEffect." .. tostring(k) .. " (mod entity)")
end
-- grid entities and mod entities are Lua tables here but userdata on PC: mods tell them from their own
-- tables with type() (Da Rules: type(x) == "table" means its own wrapper with .Gent)
do
  local raw_type, getmt, ud = type, debug.getmetatable, {[grid_mt] = true, [lent_mt] = true}
  if C.make_type then
    type = C.make_type(ud)
  else
    function type(v)
      local t = raw_type(v)
      if t == "table" and ud[getmt(v)] then return "userdata" end
      return t
    end
  end
end
function lent_mt.__newindex(t, k, v)
  local f = rawget(t, "_f")
  if k == "Parent" then f._Parent = v else f[k] = v end
end
function lent_mt.__tostring(t) local f = rawget(t, "_f"); return "Entity " .. f.Type .. "." .. f.Variant .. "." .. f.SubType end
function lent_methods:GetSprite() return rawget(self, "_f")._sprite end
function lent_methods:Exists() return not rawget(self, "_f")._removed end
function lent_methods:IsDead() return rawget(self, "_f")._removed end
function lent_methods:Remove() rawget(self, "_f")._removed = true end
function lent_methods:Die() rawget(self, "_f")._removed = true end
function lent_methods:FollowParent(p) local f = rawget(self, "_f"); f._Parent, f._follow = p, true end
function lent_methods:ToEffect() return self end
function lent_methods:GetData() local f = rawget(self, "_f"); f._data = f._data or {}; return f._data end
function lent_methods:IsVisible() return rawget(self, "_f").Visible end
for _, k in ipairs({"ToPlayer", "ToNPC", "ToPickup", "ToTear", "ToFamiliar", "ToBomb", "ToLaser", "ToKnife", "ToProjectile"}) do
  lent_methods[k] = function() return nil end
end
function lent_methods:IsEnemy() return false end
function lent_methods:HasEntityFlags() return false end
function lent_methods:GetEntityFlags() return 0 end
function lent_methods:AddEntityFlags() end
function lent_methods:ClearEntityFlags() end
function lent_methods:SetColor(c) rawget(self, "_f").Color = c end

local lent_hooks_on = false
local internal_mod = {Name = "isaaclua", _isaaclua_internal = true}
local function lent_render(e, off)
  local f = rawget(e, "_f")
  if f._removed then return end
  Run(ModCallbacks.MC_POST_EFFECT_RENDER, f.Variant, e, off or Vector(0, 0))
  if f._removed or not f.Visible then return end
  local spr = f._sprite
  if not spr:IsLoaded() then return end
  -- as Entity::Render: the offset from the parent's render already has the room's scroll and shake in it
  local p = f.Position + f.PositionOffset
  local pos = (off and Isaac.WorldToRenderPosition(p) + off or Isaac.WorldToScreen(p)) + f.SpriteOffset
  local s, ss = spr.Scale, f.SpriteScale
  spr.Scale = Vector(s.X * ss.X, s.Y * ss.Y)
  spr:Render(pos, Vector(0, 0), Vector(0, 0))
  spr.Scale = s
end
local function lent_render_children(_, parent, off)
  for _, e in ipairs(lua_ents) do
    local f = rawget(e, "_f")
    -- drawn where the parent is now (PC: a following effect renders at its parent's position), not where it
    -- was at the last game update: render frames come twice per update
    if f._follow and f._Parent and GetPtrHash(f._Parent) == GetPtrHash(parent) then
      f.Position = parent.Position
      lent_render(e, off)
    end
  end
end
local function lent_hooks()
  if lent_hooks_on then return end
  lent_hooks_on = true
  Isaac.AddPriorityCallback(internal_mod, ModCallbacks.MC_POST_UPDATE, 1000, function()
    local keep = {}
    for _, e in ipairs(lua_ents) do
      local f = rawget(e, "_f")
      if not f._removed then
        local p = f._Parent
        if f._follow and p then
          if p:Exists() then f.Position = p.Position else f._removed = true end
        else
          f.Position = f.Position + f.Velocity
        end
      end
      if not f._removed then
        f.FrameCount = f.FrameCount + 1
        f._sprite:Update()
        Run(ModCallbacks.MC_POST_EFFECT_UPDATE, f.Variant, e)
      end
      if not f._removed then keep[#keep + 1] = e end
    end
    lua_ents = keep
  end)
  Isaac.AddPriorityCallback(internal_mod, ModCallbacks.MC_POST_PICKUP_RENDER, 1000, lent_render_children)
  Isaac.AddPriorityCallback(internal_mod, ModCallbacks.MC_POST_PLAYER_RENDER, 1000, lent_render_children)
  Isaac.AddPriorityCallback(internal_mod, ModCallbacks.MC_POST_RENDER, -1000, function()
    for _, e in ipairs(lua_ents) do
      local f = rawget(e, "_f")
      if not (f._follow and f._Parent) then lent_render(e) end
    end
  end)
end
-- effects do not outlive the room
Isaac.AddPriorityCallback(internal_mod, ModCallbacks.MC_POST_NEW_ROOM, -1000, function()
  for _, e in ipairs(lua_ents) do rawget(e, "_f")._removed = true end
  lua_ents = {}
end)
-- Room:GetEnemyDamageInflicted: not exported on Switch. Counted here from ENTITY_TAKE_DMG on enemies (the
-- damage asked for, after the mods' callbacks), from the first call on, reset in each new room
do
  local inflicted, counting = 0, false
  function room:GetEnemyDamageInflicted()
    if not counting then
      counting = true
      Isaac.AddPriorityCallback(internal_mod, ModCallbacks.MC_ENTITY_TAKE_DMG, 10000, function(_, e, amount)
        if e:IsVulnerableEnemy() then inflicted = inflicted + amount end
      end)
      Isaac.AddPriorityCallback(internal_mod, ModCallbacks.MC_POST_NEW_ROOM, -1000, function() inflicted = 0 end)
    end
    return inflicted
  end
end
-- MC_GET_SHADER_PARAMS: Switch has no mod shaders, but PC mods also use it to draw over the HUD (PC calls
-- it for every shader of content/shaders.xml each frame, after POST_RENDER). Run the same way, with the
-- shader names the mods declare; the parameters a callback returns are dropped.
local shader_names = {}
content_loaders[#content_loaders + 1] = function(dir)
  for _, sub in ipairs({"content/", "content-dlc3/"}) do
    local xml = C.readfile(dir .. sub .. "shaders.xml")
    for name in (xml or ""):gmatch("<shader%s+name%s*=%s*\"([^\"]*)\"") do
      if not shader_names[name] then shader_names[name] = true; shader_names[#shader_names + 1] = name end
    end
  end
end
Isaac.AddPriorityCallback(internal_mod, ModCallbacks.MC_POST_RENDER, 9000, function()
  if not HasCallbacks(ModCallbacks.MC_GET_SHADER_PARAMS) then return end
  if #shader_names == 0 then Run(ModCallbacks.MC_GET_SHADER_PARAMS, nil, "") end
  for _, name in ipairs(shader_names) do Run(ModCallbacks.MC_GET_SHADER_PARAMS, nil, name) end
end)
local next_lent_index = 100000
local function spawn_lua_entity(def, subtype, pos, vel, spawner)
  lent_hooks()
  -- only an effect is drawn like one: anything else (To<Class> gives nil) is worth knowing about
  if def.Type ~= 1000 then C.missing("engine entity " .. def.Type .. "." .. def.Variant .. "." .. tostring(subtype or 0) .. " (Lua stand-in)") end
  local spr = Sprite()
  if def.Anm2 and C.resolve(def.Anm2) then spr:Load(def.Anm2, true); spr:Play(spr:GetDefaultAnimation(), true) end
  next_lent_index = next_lent_index + 1
  local e = setmetatable({_f = {Type = def.Type, Variant = def.Variant, SubType = subtype or def.SubType,
    Position = pos or Vector(0, 0), Velocity = vel or Vector(0, 0), PositionOffset = Vector(0, 0),
    SpriteOffset = Vector(0, 0), SpriteScale = Vector(1, 1), DepthOffset = 0, FrameCount = 0,
    Visible = true, InitSeed = Random(), DropSeed = Random(), Index = next_lent_index, Timeout = -1,
    State = 0, LifeSpan = 0, Rotation = 0, m_Height = 0, _sprite = spr, _SpawnerEntity = spawner}}, lent_mt)
  lua_ents[#lua_ents + 1] = e
  Run(ModCallbacks.MC_POST_EFFECT_INIT, def.Variant, e)
  return e
end
-- Isaac.Spawn = Game():Spawn on PC: Game::SpawnEx with a random seed (POST_*_INIT come from the Init hooks)
-- Game::SpawnEx on Switch sets the spawner only after Init, so POST_*_INIT would see SpawnerEntity nil
-- (3D Items): while a spawn runs, the spawner given to it answers for the new entity's empty fields
do
  local raw_spawn, pend = C.spawn, {}
  C.spawn = function(t, v, st, pos, vel, spawner, seed)
    pend[#pend + 1] = spawner and {spawner, t, v} or false
    local ok, e = pcall(raw_spawn, t, v, st, pos, vel, spawner, seed)
    pend[#pend] = nil
    if not ok then error(e, 2) end
    return e
  end
  local get_se, get_st, get_sv = EG.SpawnerEntity, EG.SpawnerType, EG.SpawnerVariant
  local function pending(e)
    -- the entity being made: its type and variant (Entity::GetFrameCount is not 0 during Init on Switch)
    local s = pend[#pend]
    if s and e.Type == s[2] and e.Variant == s[3] and get_se(e) == nil then return s[1] end
  end
  EG.SpawnerEntity = function(e) return get_se(e) or pending(e) end
  EG.SpawnerType = function(e) local s = pending(e); if s then return s.Type end return get_st(e) end
  EG.SpawnerVariant = function(e) local s = pending(e); if s then return s.Variant end return get_sv(e) end
end
do
  -- Scale exists on PC only for tears/NPCs/bombs etc.; elsewhere (EntityPlayer) it is a plain nil
  -- tears +0x410, bombs +0x424, NPCs +0xcc4 (their SetScale); written through SetScale
  local scale_at = {[2] = {0x410, "11Entity_Tear"}, [4] = {0x424, "11Entity_Bomb"}, [7] = {0x508, "12Entity_Laser"}}
  local setters = {}
  local function scale_of(e)
    local t = e.Type
    if npc_type(t) then return 0xcc4, "10Entity_NPC" end
    local s = scale_at[t]
    if s then return s[1], s[2] end
  end
  EG.Scale = function(e)
    local off = scale_of(e)
    if off then return peek(ptr(e), off, "f32") end
    if e.Type ~= 1 then C.missing("Entity.Scale") end
    return nil
  end
  ES.Scale = function(e, v)
    local off, cls = scale_of(e)
    if not off then return C.missing("Entity.Scale (write)") end
    setters[cls] = setters[cls] or native(NS .. cls .. "8SetScaleEf", "_pf", "Entity.Scale")
    setters[cls](ptr(e), v)
  end
end
local function spawn_engine(t, v, st, pos, vel, spawner)
  local seed = Random()
  return C.spawn(t, v or 0, st or 0, pos or Vector(0, 0), vel or Vector(0, 0), spawner, seed ~= 0 and seed or 1)
end
function game:Spawn(t, v, pos, vel, spawner, st, seed)
  return C.spawn(t, v or 0, st or 0, pos or Vector(0, 0), vel or Vector(0, 0), spawner, seed or Random())
end
function Isaac.Spawn(t, v, st, pos, vel, spawner)
  local def = mod_entities[tostring(t) .. "." .. tostring(v)]
  if def then return spawn_lua_entity(def, st, pos, vel, spawner) end
  return spawn_engine(t, v, st, pos, vel, spawner)
end
-- with the game's own mod content (main.c) the engine knows the mod's entities: spawn the real one
do
  local get_entity = native(NSK .. "12EntityConfig9GetEntityENS_11eEntityTypeEjj", "ppiuu", "EntityConfig.GetEntity")
  local lua_spawn = Isaac.Spawn
  function Isaac.Spawn(t, v, st, pos, vel, spawner)
    local ti, vi = math.tointeger(t), math.tointeger(v or 0)
    -- pickups and the other engine classes 1-9: always the engine's (5.30.0, a random key, has no EntityConfig
    -- entry until Init picks the subtype; a Lua stand-in there is no pickup: ToPickup nil)
    if ti and ti >= 1 and ti <= 9 then return spawn_engine(ti, vi, st, pos, vel, spawner) end
    if ti and vi and mod_entities[ti .. "." .. vi] and C.managerptr() then
      -- GetEntity falls back to subtype 0, then to variant 0: the key in front of the entry tells which.
      -- The key is the u32 type << 20 | variant << 8 | subtype, unmasked, so types above 4095 (Samael's
      -- scythe, 6617) and subtypes above 255 (its hitbox, knife 8.10.617) spill over, in the game's own
      -- lookup as well (EntityConfig::GetEntity ORs them in w registers): compare with the same key
      local sti = math.tointeger(st or 0) or 0
      local e = get_entity(offset(C.managerptr(), O.Manager_EntityConfig), ti, vi, sti)
      local key = e and peek(e, -8, "u32")
      local base = (ti << 20 | vi << 8) & 0xffffffff
      if key and (key == (base | sti) & 0xffffffff or key == base) then return spawn_engine(ti, vi, st, pos, vel, spawner) end
      -- not an effect, and the engine has some entry to fall back to: its entity (a Lua stand-in is no pickup or
      -- NPC; vanilla 5.30.0, a random key, has no entry of its own and fell to Epiphany's 5.30.2092 stand-in)
      if key and ti ~= 1000 and key >> 20 == ti & 0xfff then
        return spawn_engine(ti, vi, st, pos, vel, spawner)
      end
    end
    return lua_spawn(t, v, st, pos, vel, spawner)
  end
end
local find_engine = Isaac.FindByType
function Isaac.FindByType(t, v, st, cache, ignore_friendly)
  local out = find_engine(t, v, st, cache, ignore_friendly)
  for _, e in ipairs(lua_ents) do
    local f = rawget(e, "_f")
    if not f._removed and f.Type == t and (v == nil or v == -1 or f.Variant == v)
       and (st == nil or st == -1 or f.SubType == st) then
      out[#out + 1] = e
    end
  end
  return out
end

---------------------------------------------------------------- Level and Room as classes
-- PC mods (MinimapAPI's APIOverride) take getmetatable(Level).__class, keep its methods and replace its
-- __index: the methods move into a class table that is the object's metatable
do
  local make_red = native(NS .. "5Level15MakeRedRoomDoorEiNS_9eDoorSlotE", "bpii", "Level.MakeRedRoomDoor", false)
  function level:MakeRedRoomDoor(idx, slot) return make_red(gp(), idx, slot) end
  local function classify(obj, name)
    local old = getmetatable(obj) or {}
    local cls = {__type = name, __newindex = old.__newindex}
    for k, v in pairs(obj) do if type(v) == "function" then cls[k] = v end end
    for k in pairs(cls) do if obj[k] == cls[k] then rawset(obj, k, nil) end end
    local oi = old.__index
    cls.__index = function(self, k)
      local v = rawget(cls, k)
      if v ~= nil then return v end
      if type(oi) == "function" then return oi(self, k) elseif oi then return oi[k] end
    end
    setmetatable(obj, cls)
    _G[name] = setmetatable({}, {__class = cls, __index = function(_, k)
      return function(o, ...) return o[k](o, ...) end
    end})
  end
  classify(level, "Level")
  classify(room, "Room")
end

---------------------------------------------------------------- PC API inlined away in the Switch build
do
  function level:SetStateFlag(flag, v)
    local f = peek(gp(), O.Level_StateFlags, "i64")
    if f then poke(gp(), O.Level_StateFlags, "i64", v and f | (1 << flag) or f & ~(1 << flag)) end
  end
  function EM.HasFullHearts(e) return e.Type == 1 and e:GetHearts() >= e:GetEffectiveMaxHearts() end
  -- the nearest free tile, as the game picks a spot for a pickup (Room::FindFreePickupSpawnPosition, api_gen.lua)
  function Isaac.GetFreeNearPosition(pos, step)
    return room:FindFreePickupSpawnPosition(pos, step or 40, true, false)
  end
  function Isaac.ScreenToWorldDistance(v)
    local k = Isaac.WorldToScreenDistance(Vector(100, 0)).X / 100
    if k == 0 then return Vector(v.X, v.Y) end
    return Vector(v.X / k, v.Y / k)
  end
end
do
  local X = {}  -- one local: the main chunk is near Lua's 200-local limit
  -- a field of one entity type (Entity_Familiar etc. on Switch); other types keep whatever was there
  function X.typed_field(name, t, get, set)
    local pg, ps = EG[name], ES[name]
    EG[name] = function(e) if e.Type == t then return get(ptr(e)) end; return pg and pg(e) end
    ES[name] = function(e, v) if e.Type == t then if set then set(ptr(e), v) end elseif ps then ps(e, v) end end
  end
  -- Entity_Familiar: Player* +0x400 (the owner check_familiar compares), orbit layer +0xd84 and orbit
  -- distance +0xd78 (add_to_orbit), follower flag +0xd88 (IsFollowing)
  X.typed_field("Player", 3, function(p) local q = peek(p, 0x400, "p"); return q and C.entity(q) or nil end,
    function(p, v) poke(p, 0x400, "p", v and ptr(v) or nil) end)
  X.typed_field("OrbitLayer", 3, function(p) return peek(p, 0xd84, "i32") end, function(p, v) poke(p, 0xd84, "i32", v) end)
  X.typed_field("OrbitDistance", 3, function(p) return peek(p, 0xd78, "vec") end, function(p, v) poke(p, 0xd78, "vec", v) end)
  X.typed_field("IsFollower", 3, function(p) return peek(p, 0xd88, "b") end, function(p, v) poke(p, 0xd88, "b", v) end)

  -- EntityPlayer: Entity_Player::AnimatePickup overloads; aim vector +0x1a04 and head direction +0x19fc
  -- (Entity_Player::SetAimDirection writes both)
  X.anim_coll = native(EP .. "13AnimatePickupENS_16eCollectibleTypeEPKcS3_", "_piss", "EntityPlayer.AnimateCollectible")
  X.anim_trinket = native(EP .. "13AnimatePickupENS_12eTrinketTypeEPKcS3_", "_piss", "EntityPlayer.AnimateTrinket")
  X.anim_card = native(EP .. "13AnimatePickupENS_5eCardEPKc", "_pis", "EntityPlayer.AnimateCard")
  X.anim_pill = native(EP .. "13AnimatePickupENS_10ePillColorEPKc", "_pis", "EntityPlayer.AnimatePill")
  function EM.AnimateCollectible(e, id, anim, sprite_anim)
    if e.Type == 1 then X.anim_coll(ptr(e), id, anim or "Pickup", sprite_anim or "PlayerPickupSparkle") end
  end
  function EM.AnimateTrinket(e, id, anim, sprite_anim)
    if e.Type == 1 then X.anim_trinket(ptr(e), id, anim or "Pickup", sprite_anim or "PlayerPickupSparkle") end
  end
  function EM.AnimateCard(e, id, anim) if e.Type == 1 then X.anim_card(ptr(e), id, anim or "Pickup") end end
  function EM.AnimatePill(e, id, anim) if e.Type == 1 then X.anim_pill(ptr(e), id, anim or "Pickup") end end
  function EM.GetAimDirection(e) return e.Type == 1 and peek(ptr(e), 0x1a04, "vec") or Vector(0, 0) end
  function EM.GetHeadDirection(e) return e.Type == 1 and peek(ptr(e), 0x19fc, "i32") or Direction.NO_DIRECTION end

  -- Morph: the class's own version (Entity_NPC::Morph / Entity_Pickup::Morph)
  X.npc_morph = native(NS .. "10Entity_NPC5MorphENS_11eEntityTypeEjji", "bpiuui", "EntityNPC.Morph", false)
  X.pickup_morph = native(NS .. "13Entity_Pickup5MorphENS_11eEntityTypeEjjbbb", "_piuubbb", "EntityPickup.Morph")
  function EM.Morph(e, t, v, st, a, b, c)
    if e.Type == 5 then X.pickup_morph(ptr(e), t, v, st, a or false, b or false, c or false)
    elseif npc_type(e.Type) then return X.npc_morph(ptr(e), t, v, st, a or -1) end
  end
  -- EntityLaser.ShootAngle(variant, pos, angle, timeout, offset, source): static
  X.shoot_angle = native(NS .. "12Entity_Laser10ShootAngleEjRKN4KAGE4Math7Vector2EfiS5_PNS_6EntityEb", "euVfiVpb",
    "EntityLaser.ShootAngle")
  EntityLaser = EntityLaser or {}
  function EntityLaser.ShootAngle(variant, pos, angle, timeout, offset, source)
    return X.shoot_angle(variant, pos, angle, timeout or 0, offset or Vector(0, 0), source, false)
  end
end
-- coverage batch 6 (0.64): Game / ItemPool methods the 0.63 check counted although they were stand-ins
-- (Game, HUD and ItemPool answer any name with a warning function; test/apichk now tells those apart).
-- A function of its own: the main chunk is at Lua's 200-local limit
;(function()
  local X = {}
  local function int(v, d) return math.tointeger(v) or (type(v) == "number" and math.floor(v)) or d end
  local function g() return gp() end
  local function eptr(e) return e and ptr(e) or nil end
  function X.colormod(c)  -- ColorMod: tint RGBA +0, colorize RGBA +16, offset RGB +32
    local b = C.buf(0x40)
    c = c or {}
    poke(b, 0, "f32", c.R or 1); poke(b, 4, "f32", c.G or 1); poke(b, 8, "f32", c.B or 1); poke(b, 12, "f32", c.A or 1)
    poke(b, 16, "f32", c.CR or 0); poke(b, 20, "f32", c.CG or 0); poke(b, 24, "f32", c.CB or 0); poke(b, 28, "f32", c.CA or 0)
    poke(b, 32, "f32", c.RO or 0); poke(b, 36, "f32", c.GO or 0); poke(b, 40, "f32", c.BO or 0)
    return b
  end
  local function flags(f) return halves(f or bs(0, 0)) end
  local EXPLOSION = 4  -- DamageFlag.DAMAGE_EXPLOSION
  local G, GK = NS .. "4Game", NSK .. "4Game"
  -- counters: devil deals Game+0x26fa70 (AddDevilRoomDeal), donations greed +0x26fa74 / angel +0x26fa78
  -- (DonateGreed / DonateAngel); encountered bosses: vector of {type, variant} at +0x2f0038 (HasEncounteredBoss)
  local add_deal = native(G .. "16AddDevilRoomDealEv", "_p", "Game.AddDevilRoomDeal")
  function game:AddDevilRoomDeal() add_deal(g()) end
  function game:GetDevilRoomDeals() return peek(g(), 0x26fa70, "i32") or 0 end
  local donate_a = native(G .. "11DonateAngelEi", "_pi", "Game.DonateAngel")
  local donate_g = native(G .. "11DonateGreedEi", "_pi", "Game.DonateGreed")
  function game:DonateAngel(n) donate_a(g(), int(n, 0)) end
  function game:DonateGreed(n) donate_g(g(), int(n, 0)) end
  function game:GetDonationModAngel() return peek(g(), 0x26fa78, "i32") or 0 end
  function game:GetDonationModGreed() return peek(g(), 0x26fa74, "i32") or 0 end
  function game:ClearDonationModAngel() poke(g(), 0x26fa78, "i32", 0) end
  function game:ClearDonationModGreed() poke(g(), 0x26fa74, "i32", 0) end
  local add_boss = native(G .. "18AddEncounteredBossENS_11eEntityTypeEj", "_pii", "Game.AddEncounteredBoss")
  local has_boss = native(G .. "18HasEncounteredBossENS_11eEntityTypeEj", "bpii", "Game.HasEncounteredBoss", false)
  function game:AddEncounteredBoss(t, v) add_boss(g(), int(t, 0), int(v, 0)) end
  function game:HasEncounteredBoss(t, v) return has_boss(g(), int(t, 0), int(v, 0)) end
  function game:GetNumEncounteredBosses()
    local b, e = peek(g(), 0x2f0038, "i64"), peek(g(), 0x2f0040, "i64")
    return b and e and (e - b) // 8 or 0
  end
  local fade_in = native(G .. "6FadeinEf", "_pf", "Game.Fadein")
  local fade_out = native(G .. "7FadeoutEfNS0_14eFadeoutTargetE", "_pfi", "Game.Fadeout")
  function game:Fadein(speed) fade_in(g(), speed or 0.05) end
  function game:Fadeout(speed, target) fade_out(g(), speed or 0.05, int(target, 0)) end
  local finish = native(G .. "15FinishChallengeEv", "_p", "Game.FinishChallenge")
  function game:FinishChallenge() finish(g()) end
  local end_game = native(G .. "3EndENS_7eEndingE", "_pi", "Game.End")
  function game:End(ending) end_game(g(), int(ending, 1)) end
  local waves = native(GK .. "16GetGreedWavesNumEv", "ip", "Game.GetGreedWavesNum", 11)
  local boss_wave = native(GK .. "19GetGreedBossWaveNumEv", "ip", "Game.GetGreedBossWaveNum", 9)
  function game:GetGreedWavesNum() return waves(g()) end
  function game:GetGreedBossWaveNum() return boss_wave(g()) end
  local nearest = native(G .. "16GetNearestPlayerERKN4KAGE4Math7Vector2E", "epV", "Game.GetNearestPlayer")
  local random_p = native(G .. "15GetRandomPlayerERKN4KAGE4Math7Vector2Ef", "epVf", "Game.GetRandomPlayer")
  function game:GetNearestPlayer(pos) return nearest(g(), pos or Vector(0, 0)) end
  function game:GetRandomPlayer(pos, radius) return random_p(g(), pos or Vector(0, 0), radius or 0) end
  local shock = native(G .. "13MakeShockwaveERKN4KAGE4Math7Vector2Effi", "_pVffi", "Game.MakeShockwave")
  function game:MakeShockwave(pos, amp, speed, dur) shock(g(), pos, amp or 0.03, speed or 0.025, int(dur, 10)) end
  local move_rand = native(G .. "16MoveToRandomRoomEbjPNS_13Entity_PlayerE", "_pbup", "Game.MoveToRandomRoom")
  function game:MoveToRandomRoom(error_room, seed, player)
    move_rand(g(), error_room == true, int(seed, 0) & 0xffffffff, eptr(player or Isaac.GetPlayer(0)))
  end
  local lap = native(G .. "14NextVictoryLapEv", "_p", "Game.NextVictoryLap")
  function game:NextVictoryLap() lap(g()) end
  local reroll_enemy = native(G .. "11RerollEnemyEPNS_6EntityEb", "bppb", "Game.RerollEnemy", false)
  function game:RerollEnemy(e) return e and reroll_enemy(g(), ptr(e), false) or false end
  local reroll_col = native(G .. "23RerollLevelCollectiblesEv", "_p", "Game.RerollLevelCollectibles")
  local reroll_pick = native(G .. "18RerollLevelPickupsEj", "_pu", "Game.RerollLevelPickups")
  function game:RerollLevelCollectibles() reroll_col(g()) end
  function game:RerollLevelPickups(seed) reroll_pick(g(), int(seed, 0) & 0xffffffff) end
  local particles = native(G .. "14SpawnParticlesERKN4KAGE4Math7Vector2ENS_13Entity_Effect8eVariantEifRKNS_8ColorModEfj",
    "_pViifpfu", "Game.SpawnParticles")
  function game:SpawnParticles(pos, variant, count, speed, color, height, subtype)
    particles(g(), pos, int(variant, 0), int(count, 1), speed or 0, X.colormod(color), height or 100000, int(subtype, 0))
  end
  local stage_tr = native(G .. "20StartStageTransitionEbNS_15StageTransition10eAnimationEPNS_13Entity_PlayerE", "_pbip",
    "Game.StartStageTransition")
  function game:StartStageTransition(same, anim, player) stage_tr(g(), same == true, int(anim, 0), eptr(player)) end
  -- farts and bombs (argument order as PC's Lua API; the Switch code takes TearFlags as a 128-bit value)
  local bean = native(G .. "14ButterBeanFartERKN4KAGE4Math7Vector2EfPNS_6EntityEbb", "_pVfpbb", "Game.ButterBeanFart")
  function game:ButterBeanFart(pos, radius, source, effect, knock)
    bean(g(), pos, radius or 85, eptr(source), effect ~= false, knock == true)
  end
  local charm = native(G .. "9CharmFartERKN4KAGE4Math7Vector2EfPNS_6EntityE", "_pVfp", "Game.CharmFart")
  function game:CharmFart(pos, radius, source) charm(g(), pos, radius or 85, eptr(source)) end
  local fart = native(G .. "4FartERKN4KAGE4Math7Vector2EfPNS_6EntityEfjRKNS_8ColorModE", "_pVfpfup", "Game.Fart")
  function game:Fart(pos, radius, source, scale, subtype, color)
    fart(g(), pos, radius or 85, eptr(source), scale or 1, int(subtype, 0), X.colormod(color))
  end
  local bomb_dmg = native(G .. "10BombDamageERKN4KAGE4Math7Vector2EffbPNS_6EntityENS_11bitset128_tEmb", "_pVffbpiiib",
    "Game.BombDamage")
  function game:BombDamage(pos, dmg, radius, line, source, tf, df, dsrc)
    local l, h = flags(tf)
    bomb_dmg(g(), pos, dmg or 0, radius or 0, line ~= false, eptr(source), l, h, int(df, EXPLOSION), dsrc == true)
  end
  local bomb_fx = native(G .. "20BombExplosionEffectsERKN4KAGE4Math7Vector2EfNS_11bitset128_tERKNS_8ColorModEPNS_6EntityEfbmb",
    "_pVfiippfbib", "Game.BombExplosionEffects")
  function game:BombExplosionEffects(pos, dmg, tf, color, source, mult, line, dsrc, df)
    local l, h = flags(tf)
    bomb_fx(g(), pos, dmg or 0, l, h, X.colormod(color), eptr(source), mult or 1, line ~= false, int(df, EXPLOSION),
      dsrc == true)
  end
  local bomb_tf = native(G .. "19BombTearflagEffectsERKN4KAGE4Math7Vector2EfNS_11bitset128_tEPNS_6EntityEf", "_pVfiipf",
    "Game.BombTearflagEffects")
  function game:BombTearflagEffects(pos, radius, tf, source, mult)
    local l, h = flags(tf)
    bomb_tf(g(), pos, radius or 0, l, h, eptr(source), mult or 1)
  end
  local attract = native(G .. "22UpdateStrangeAttractorERKN4KAGE4Math7Vector2EffPFbPNS_6EntityEE", "_pVffp",
    "Game.UpdateStrangeAttractor")
  function game:UpdateStrangeAttractor(pos, force, radius) attract(g(), pos, force or 10, radius or 250, nil) end
  -- ShowFortune / ShowRule: a random line of the fortune paper, as the Fortune Cookie (Entity_Player::UseActiveItem)
  -- and the Rules card (UseCard) do: Fortunes::show_random(game + 0x25c30, "Fortune_General" / "Fortune_Rules")
  local fortune = native(NS .. "8Fortunes11show_randomEPKc", "_ps", "Game.ShowFortune")
  function game:ShowFortune() fortune(offset(g(), 0x25c30), "Fortune_General") end
  function game:ShowRule() fortune(offset(g(), 0x25c30), "Fortune_Rules") end

  -- ItemPool
  local reset_bl = native(NS .. "8ItemPool18ResetRoomBlacklistEv", "_p", "ItemPool.ResetRoomBlacklist")
  function itempool:ResetRoomBlacklist() reset_bl(offset(gp(), O.Game_ItemPool)) end
  local bible = native(NS .. "8ItemPool15AddBibleUpgradeEiNS0_13eItemPoolTypeE", "_pii", "ItemPool.AddBibleUpgrade")
  function itempool:AddBibleUpgrade(n, pool) bible(offset(gp(), O.Game_ItemPool), int(n, 1), int(pool, 0)) end
end)()
-- coverage batch 7 (0.65): Level / Room methods the check counted although only the class globals
-- (which forward any name) had them
;(function()
  local function int(v, d) return math.tointeger(v) or (type(v) == "number" and math.floor(v)) or d end
  local R, RK, L = NS .. "4Room", NSK .. "4Room", NS .. "5Level"
  local uncover = native(L .. "17UncoverHiddenDoorEiNS_9eDoorSlotE", "_pii", "Level.UncoverHiddenDoor")
  function level:UncoverHiddenDoor(idx, slot) uncover(gp(), int(idx, 0), int(slot, 0)) end
  -- CheckLine(pos1, pos2, mode, threshold, ignoreWalls, ignoreCrushable): bool, the end position
  local check = native(RK .. "9CheckLineERKN4KAGE4Math7Vector2ES5_NS0_14eLinecheckModeEibbPS3_", "bpVViibbp",
    "Room.CheckLine", false)
  function room:CheckLine(a, b, mode, thr, walls, crush)
    local out = C.buf(8)
    local ok = check(roomp(), a, b, int(mode, 0), int(thr, 0), walls == true, crush == true, out)
    return ok, Vector(peek(out, 0, "f32"), peek(out, 4, "f32"))
  end
  local destroy = native(R .. "11DestroyGridEibb", "bpibb", "Room.DestroyGrid", false)
  function room:DestroyGrid(i, immediate) return destroy(roomp(), int(i, -1), immediate == true, false) end
  function room:DestroyGridWithSource(i, immediate) return destroy(roomp(), int(i, -1), immediate == true, false) end
  -- GridEntity:Destroy(immediate): the grid's own virtual Destroy, through Room::DestroyGrid at its index
  function grid_mt:Destroy(immediate) return destroy(roomp(), rawget(self, "_i") or -1, immediate == true, false) end
  -- GridEntity:Hurt(damage) = Room::DamageGrid at its index (PC GridEntity::Hurt); the *WithSource forms are
  -- Repentance+ (the source only names who did it: same effect)
  function grid_mt:Hurt(damage) return room:DamageGrid(rawget(self, "_i") or -1, int(damage, 0)) end
  function grid_mt:HurtWithSource(damage) return self:Hurt(damage) end
  function grid_mt:DestroyWithSource(immediate) return self:Destroy(immediate) end
  function room:DamageGridWithSource(i, damage) return room:DamageGrid(i, damage) end
  local clamped = native(RK .. "18GetClampedPositionERKN4KAGE4Math7Vector2Effff", "vpVffff", "Room.GetClampedPosition")
  function room:GetClampedPosition(pos, m) m = m or 0; return clamped(roomp(), pos, m, m, m, m) end
  local wrap = native(RK .. "18ScreenWrapPositionERKN4KAGE4Math7Vector2Effff", "vpVffff", "Room.ScreenWrapPosition")
  function room:ScreenWrapPosition(pos, m) m = m or 0; return wrap(roomp(), pos, m, m, m, m) end
  local slot_pos = native(RK .. "19GetDoorSlotPositionENS_9eDoorSlotE", "vpi", "Room.GetDoorSlotPosition")
  function room:GetDoorSlotPosition(slot) return slot_pos(roomp(), int(slot, 0)) end
  local coll = native(RK .. "16GetGridCollisionEi", "ipi", "Room.GetGridCollision", 0)
  local coll_pos = native(RK .. "16GetGridCollisionEN4KAGE4Math7Vector2E", "ipv", "Room.GetGridCollisionAtPos", 0)
  function room:GetGridCollision(i) return coll(roomp(), int(i, 0)) end
  function room:GetGridCollisionAtPos(pos) return coll_pos(roomp(), pos) end
  function room:GetGridPathFromPos(pos) return room:GetGridPath(room:GetGridIndex(pos)) end
  local seeded = native(R .. "20GetSeededCollectibleEjb", "ipub", "Room.GetSeededCollectible", 0)
  function room:GetSeededCollectible(seed, no_decrease) return seeded(roomp(), int(seed, 0) & 0xffffffff, no_decrease == true) end
  local rm_door = native(R .. "10RemoveDoorENS_9eDoorSlotE", "_pi", "Room.RemoveDoor")
  function room:RemoveDoor(slot) rm_door(roomp(), int(slot, 0)) end
  -- SetGridPath(index, value): Room::SetGridPath takes x, y (width at Room+0x14)
  local set_path = native(R .. "11SetGridPathEiii", "bpiii", "Room.SetGridPath", false)
  function room:SetGridPath(i, v)
    local w = room:GetGridWidth()
    i = int(i, -1)
    if w <= 0 or i < 0 then return false end
    return set_path(roomp(), i % w, i // w, int(v, 0))
  end
  local bridge = native(R .. "13TryMakeBridgeEPNS_10GridEntityES2_", "bppp", "Room.TryMakeBridge", false)
  function room:TryMakeBridge(rock, pit)
    return rock and pit and bridge(roomp(), rawget(rock, "_p"), rawget(pit, "_p")) or false
  end
end)()
;(function()  -- batch 8: Room methods as isaac-ng.exe has them
  -- GetSecondBossID: PC Room+0x1d20, right after the boss id +0x1d1c; Switch Room::Init stores the second boss id
  -- at +0x1fc4 when it differs from the first (+0x1fc0)
  function room:GetSecondBossID() return peek(roomp(), O.Room_BossId + 4, "i32") or 0 end
  -- IsDoorSlotAllowed(slot): PC tests bit slot of the room data's door mask (Desc->Data +0x38); Switch
  -- RoomConfig::Room doors mask +0x40
  function room:IsDoorSlotAllowed(slot)
    if type(slot) ~= "number" or slot < 0 or slot > 7 then return false end
    local d = peek(room_desc(), O.Desc_Data, "p")
    return d ~= nil and ((peek(d, 0x40, "u32") or 0) >> slot) & 1 == 1
  end
  -- SetClear / HasWaterPits: PC binds read the room's RoomDescriptor flags (Room+4 -> +0x44 on PC, +0x50 here):
  -- bit 0 FLAG_CLEAR, bit 5 FLAG_HAS_WATER
  function room:SetClear(v)
    local d = room_desc()
    if not d then return end
    local fl = peek(d, O.Desc_Flags, "u32") or 0
    poke(d, O.Desc_Flags, "u32", v and (fl | 1) or (fl & ~1))
  end
  function room:HasWaterPits() return ((peek(room_desc(), O.Desc_Flags, "u32") or 0) >> 5) & 1 == 1 end
  -- GetRenderSurfaceTopLeft: PC Room+0x11fc, the corner of the 442x286 room picture on the screen, so screen size =
  -- 2 * TopLeft + (442, 286) (as mods such as Dead Sea Scrolls compute it); worked out from the screen size here
  function room:GetRenderSurfaceTopLeft()
    return Vector((Isaac.GetScreenWidth() - 442) / 2, (Isaac.GetScreenHeight() - 286) / 2)
  end
  -- Game:Update / Render, Level:Update, Room:Render, HUD:Render: one more call of the game's own function, as PC
  -- binds them (a call made from inside the same function is skipped, so a mod cannot recurse into it)
  local function once(sym, what, obj)
    local f, busy = native(NS .. sym, "_p", what), false
    return function()
      local p = obj()
      if busy or not p then return end
      busy = true
      local ok, e = pcall(f, p)
      busy = false
      if not ok then error(e, 3) end
    end
  end
  game.Update = once("4Game6UpdateEv", "Game.Update", gp)
  game.Render = once("4Game6RenderEv", "Game.Render", gp)
  level.Update = once("5Level6UpdateEv", "Level.Update", gp)
  room.Render = once("4Room6RenderEv", "Room.Render", roomp)
  rawset(hud, "Render", once("3HUD6RenderEv", "HUD.Render", function() return gp() and offset(gp(), 0xe8468) end))
end)()
-- Level/Room/Game/ItemPool fields of the PC bindings, offsets matched against the PC exe:
-- Level (Game+0): bytes HasBossChallenge +8, DevilRoomDisabled +9, HeartPicked +0x10, CanSeeEverything +0x11
-- (PC the same; Level::RestoreGameState copies them from GameState+0x1bb/+0x1b9/+0x1b8/+0x1ba), DungeonPlacementSeed
-- +0x21580 and LastBossRoomListIndex +0x21568 (RestoreGameState from +0x1ac/+0x1a4, PC +0x181bc/+0x181a4).
-- Room: HasTriggerPressurePlates u8 +0x78ed (Room::check_pressure_plates_triggered, PC +0x711d), FloorColor /
-- WallColor ColorMods +0x7968 / +0x7994 (built in Room's constructor, PC +0x7188 / +0x71b4).
-- Game (from FrameCount +0x24f99c, PC +0x1a30dc): ScreenShakeCountdown +0x10, darkness timeout +0x1c, modifier
-- +0x20, target +0x24 (Game::ShakeScreen, Entity_NPC::ai_lamb's Darken(1, 600)).
-- ItemPool room blacklist: std::vector<bool> words +0x908, bit count +0x910 (ItemPool::ResetRoomBlacklist).
;(function()
  local function lb(o) return function() return (peek(gp(), o, "u8") or 0) ~= 0 end end
  level.HasBossChallenge, level.IsDevilRoomDisabled = lb(8), lb(9)
  level.GetHeartPicked, level.GetCanSeeEverything = lb(0x10), lb(0x11)
  function level:DisableDevilRoom() poke(gp(), 9, "u8", 1) end
  function level:SetHeartPicked() poke(gp(), 0x10, "u8", 1) end
  function level:SetCanSeeEverything(v) poke(gp(), 0x11, "u8", v and 1 or 0) end
  function level:GetDungeonPlacementSeed() return peek(gp(), 0x21580, "u32") or 0 end
  function level:GetLastBossRoomListIndex() return peek(gp(), 0x21568, "i32") or 0 end
  function room:HasTriggerPressurePlates() return (peek(roomp(), 0x78ed, "u8") or 0) ~= 0 end
  local function set_color(o)
    return function(_, c)
      local r = roomp()
      if not r or not c then return end
      local cr, cg, cb, ca = 0, 0, 0, 0
      if c.GetColorize then local z = c:GetColorize(); cr, cg, cb, ca = z.R or 0, z.G or 0, z.B or 0, z.A or 0 end
      local v = {c.R or 1, c.G or 1, c.B or 1, c.A or 1, cr, cg, cb, ca, c.RO or 0, c.GO or 0, c.BO or 0}
      for i = 1, 11 do poke(r, o + 4 * (i - 1), "f32", v[i]) end
    end
  end
  room.SetFloorColor, room.SetWallColor = set_color(0x7968), set_color(0x7994)
  local FC = O.Game_FrameCount
  function game:Darken(darkness, timeout)
    poke(gp(), FC + 0x24, "f32", tonumber(darkness) or 0)
    poke(gp(), FC + 0x1c, "i32", math.tointeger(timeout) or math.floor(tonumber(timeout) or 0))
  end
  function game:GetDarknessModifier() return peek(gp(), FC + 0x20, "f32") or 0 end
  function game:GetTargetDarkness() return peek(gp(), FC + 0x24, "f32") or 0 end
  function game:GetScreenShakeCountdown() return peek(gp(), FC + 0x10, "i32") or 0 end
  function itempool:AddRoomBlacklist(item)
    local p, id = offset(gp(), O.Game_ItemPool), math.tointeger(item)
    local words, n = peek(p, 0x908, "p"), peek(p, 0x910, "i64") or 0
    if not words or not id or id < 0 or id >= n then return end
    local w = (id >> 6) * 8
    poke(words, w, "i64", (peek(words, w, "i64") or 0) | (1 << (id & 63)))
  end
end)()
-- more PC binding fields (0.88), offsets matched against the PC exe:
-- Game (PC +0x1c3174.. -> +0x26fa5c.., the shift of AddDevilRoomDeal's +0x1c3188 -> +0x26fa70): LastDevilRoomStage
-- +0x26fa5c (Game::TriggerRKey resets it), TreasureRoomVisitCount +0x26fa60 (Room::Init, GetPlanetariumChance),
-- StagesWithoutHeartsPicked +0x26fa68 / StagesWithoutDamage +0x26fa6c (StageTransition::Update); LastLevelWithoutHalfHp
-- +0x2f0020 / LastLevelWithDamage +0x2f002c (PC +0x24361c / +0x243628 by the shift of Difficulty +0x243624 -> +0x2f0028;
-- Entity_Player::Update / TakeDamage write them, Room::SpawnClearAward reads the second).
-- Level: angel room chance bonus f32 +0x21538 (PC +0x18184; Room::GetAngelRoomChance), devil/angel RNG +0x21528
-- (PC +0x18174; Level::RestoreGameState seeds it). Room: SlowDown i32 +0x78f0 (PC +0x7120; Death, the Drowsy pill),
-- wall blood duration / count +0x78d8 / +0x78dc (PC +0x7108; Mom: (20, 6)), FirstEnemyDead u8 +0x7964 (PC +0x7184;
-- Entity_NPC::trigger_death_remove). Ambush / sacrifice done: room descriptor flags 8 / 4, as on PC.
;(function()
  local function int(v, d) return math.tointeger(v) or (type(v) == "number" and math.floor(v)) or d end
  local function gi(o) return function() return peek(gp(), o, "i32") or 0 end end
  local function gs(o) return function(_, v) poke(gp(), o, "i32", int(v, 0)) end end
  local function gadd(o) return function() poke(gp(), o, "i32", (peek(gp(), o, "i32") or 0) + 1) end end
  local function gclr(o) return function() poke(gp(), o, "i32", 0) end end
  game.GetLastDevilRoomStage, game.SetLastDevilRoomStage = gi(0x26fa5c), gs(0x26fa5c)
  game.GetTreasureRoomVisitCount, game.AddTreasureRoomsVisited = gi(0x26fa60), gadd(0x26fa60)
  game.GetStagesWithoutHeartsPicked, game.AddStageWithoutHeartsPicked = gi(0x26fa68), gadd(0x26fa68)
  game.ClearStagesWithoutHeartsPicked = gclr(0x26fa68)
  game.GetStagesWithoutDamage, game.AddStageWithoutDamage, game.ClearStagesWithoutDamage =
    gi(0x26fa6c), gadd(0x26fa6c), gclr(0x26fa6c)
  game.GetLastLevelWithoutHalfHp, game.SetLastLevelWithoutHalfHp = gi(0x2f0020), gs(0x2f0020)
  game.GetLastLevelWithDamage, game.SetLastLevelWithDamage = gi(0x2f002c), gs(0x2f002c)
  function level:AddAngelRoomChance(f) poke(gp(), 0x21538, "f32", (peek(gp(), 0x21538, "f32") or 0) + (tonumber(f) or 0)) end
  function level:GetRoomCount() return peek(gp(), O.Level_RoomCount, "i32") or 0 end
  function level:GetDevilAngelRoomRNG() return rng_vec(nil, offset(gp(), 0x21528)) end  -- the level's own, as on PC
  function room:SetSlowDown(n)  -- PC: keeps the longer of the two
    local r = roomp(); if not r then return end
    n = int(n, 0)
    if n > (peek(r, 0x78f0, "i32") or 0) then poke(r, 0x78f0, "i32", n) end
  end
  function room:HasSlowDown() return (peek(roomp(), 0x78f0, "i32") or 0) > 0 end
  function room:EmitBloodFromWalls(duration, count)
    local r = roomp(); if not r then return end
    poke(r, 0x78d8, "i32", int(duration, 0)); poke(r, 0x78dc, "i32", int(count, 0))
  end
  function room:IsFirstEnemyDead() return (peek(roomp(), 0x7964, "u8") or 0) ~= 0 end
  function room:SetFirstEnemyDead(v) poke(roomp(), 0x7964, "u8", v and 1 or 0) end
  local function desc_flag(bit)
    return function(_, v)
      local d = room_desc(); if not d then return end
      local f = peek(d, O.Desc_Flags, "u32") or 0
      poke(d, O.Desc_Flags, "u32", v and (f | bit) or (f & ~bit))
    end
  end
  room.SetAmbushDone, room.SetSacrificeDone = desc_flag(8), desc_flag(4)
  function room:IsSacrificeDone() return ((peek(room_desc(), O.Desc_Flags, "u32") or 0) & 4) ~= 0 end
  -- TintedRockIdx i32 +0xe90, set in Room::Init next to the dungeon rock index +0xe94 (Room::GetDungeonRockIdx) and
  -- read by Room::spawn_entity (PC +0x764 / +0x768); ShopLevel i32 +0x76c8 (Room::init_shop; PC +0x6f5c, inferred
  -- from its place between the fields above and the pressure plate flag +0x78ed)
  function room:GetTintedRockIdx() return peek(roomp(), 0xe90, "i32") or -1 end
  function room:GetShopLevel() return peek(roomp(), 0x76c8, "i32") or 0 end
  -- KeepDoorsClosed: i32 +0x1918 = 10 (PC +0x11ec, right before the room's start frame +0x11f0 -> +0x191c);
  -- Room::Update counts it down and only clears the room while it is 0
  function room:KeepDoorsClosed() poke(roomp(), 0x1918, "i32", 10) end
  function itempool:IdentifyPill(color)
    local c = int(color, 0) & 0x7ff
    if c > 0 and c < 15 then poke(offset(gp(), O.Game_ItemPool), O.Pool_PillIdentified + c, "u8", 1) end
  end
  -- Repentance+ API
  function itempool:UnidentifyPill(color)
    local c = int(color, 0) & 0x7ff
    if c >= 0 and c < 15 then poke(offset(gp(), O.Game_ItemPool), O.Pool_PillIdentified + c, "u8", 0) end
  end
end)()
;(function()  -- fields PC has on more classes than were mapped (found by test/apichk: they read nil)
  -- each offset is PC's (isaac-ng.exe data members) plus the class's shift, checked in the Switch code:
  -- effect +0xa8: ParentOffset vec +0x460 (Init sets it from a zero vector), Rotation f32 +0x46c, Scale f32 +0x470
  --   (Init sets 1.0); knife +0xa0: Scale f32 +0x420 (Init sets 1.0), Rotation f32 +0x428 (update_bone_swing),
  --   MaxDistance f32 +0x44c and Charge f32 +0x450 (Entity_Knife::Shoot(charge, range) stores both);
  -- projectile +0xa0: Scale f32 +0x410 (Init sets 1.0); pickup +0xe4: State i32 +0x5b4
  local function extend(name, ty, at)
    local get, set = EG[name], ES[name]
    EG[name] = function(e)
      local o = at[e.Type]
      if o then return peek(ptr(e), o, ty) end
      if get then return get(e) end
    end
    ES[name] = function(e, v)
      local o = at[e.Type]
      if o then return poke(ptr(e), o, ty, v) end
      if set then return set(e, v) end
    end
  end
  extend("ParentOffset", "vec", {[1000] = 0x460})
  extend("Rotation", "f32", {[1000] = 0x46c, [8] = 0x428})
  extend("Scale", "f32", {[1000] = 0x470, [8] = 0x420, [9] = 0x410})
  extend("MaxDistance", "f32", {[8] = 0x44c})
  extend("Charge", "f32", {[8] = 0x450})
  extend("State", "i32", {[5] = 0x5b4})
  -- Level: PC +0x181b0.. right after LeaveDoor/EnterDoor (+0x181a8/ac), Switch +0x2156c/+0x21570 (the same
  -- +0x93c4 as LastBossRoomListIndex and DungeonPlacementSeed, which bracket these)
  local LV = {DungeonReturnPosition = {0x21574, "vec"}, DungeonReturnRoomIndex = {0x2157c, "i32"},
    GreedModeWave = {0x21588, "i32"}}
  local mt = getmetatable(level)
  local idx, nidx = mt.__index, mt.__newindex
  mt.__index = function(t, k)
    local f = LV[k]
    if f then return peek(gp(), f[1], f[2]) end
    return idx(t, k)
  end
  mt.__newindex = function(t, k, v)
    local f = LV[k]
    if f then return poke(gp(), f[1], f[2], v) end
    return nidx(t, k, v)
  end
end)()
-- the other classes PC registers as globals (isaac-ng.exe pushes each name in its LuaBridge setup): mods
-- reach them for getmetatable(X).__class or by mistake (Samael: GridEntity.GRID_WALL, nil on PC too).
-- A key gives a forwarder only when it is a method of that class here, anything else reads as nil.
do
  local function class_global(name, methods, inst)
    if rawget(_G, name) ~= nil then return end
    local cls = inst or {__type = name}  -- inst: the instances' own metatable, as PC's __class
    _G[name] = setmetatable({}, {__class = cls, __index = function(t, k)
      if methods and type(rawget(methods, k)) == "function" then
        local f = function(o, ...) return o[k](o, ...) end
        rawset(t, k, f)
        return f
      end
    end})
  end
  for _, n in ipairs({"GridEntity", "GridEntityDoor", "GridEntityPit", "GridEntityPoop", "GridEntityPressurePlate",
                      "GridEntityRock", "GridEntitySpikes", "GridEntityTNT"}) do
    class_global(n, grid_mt)
  end
  for _, n in ipairs({"GridEntityDesc", "HUD", "ItemPool", "PathFinder", "QueueItemData", "TearParams",
                      "TemporaryEffect", "EntityList", "RoomConfig"}) do
    class_global(n)
  end
  class_global("TemporaryEffects", te_mt, te_mt)
end
debug.getregistry()["isaaclua.EG"] = EG  -- test/apichk: which entity fields are still stored only
-- the main menu's MODS entry and cursor without REP+ (main.c hk_game_menu_render): anim "Mods" or "Cursor" of
-- gfx/ui/main menu/isaaclua_menu.anm2 at x, y; false while that can't be drawn (the menu font is used then)
function _isaaclua_menu_item(anim, x, y)
  local m = rawget(_G, "_isaaclua_menu_spr")
  if type(m) == "number" then  -- not loaded: the save data (where the anm2 is written) may not be mounted yet
    if m > 0 then rawset(_G, "_isaaclua_menu_spr", m - 1) return false end
    m = nil
  end
  if m == nil then
    m = Sprite()
    m:Load("gfx/ui/main menu/isaaclua_menu.anm2", true)
    if not m:IsLoaded() then m = 120 end
    rawset(_G, "_isaaclua_menu_spr", m)
  end
  if type(m) == "number" then return false end
  m:Play(anim, true)
  m:Render(Vector(x, y))
  return true
end
