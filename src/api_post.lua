---------------------------------------------------------------- fixes on top of the generated methods (api_gen.lua)
;(function()  -- its own function: the main chunk is near Lua's 200-locals limit
  -- ShootRedCandle: PC returns the new flame (REPENTOGON libzhl: Entity* Entity_Player::ShootRedCandle); the Switch
  -- shoot_red_candle leaves a vtable call's result in x0, so return the flame it just spawned (1000.52, frame 0)
  local shoot = EM.ShootRedCandle
  if shoot then
    EM.ShootRedCandle = function(e, dir, ...)
      local r = shoot(e, dir, ...)
      if e.Type ~= 1 then return r end
      local h, flame = GetPtrHash(e)
      for _, f in ipairs(Isaac.FindByType(1000, 52)) do
        if f.FrameCount == 0 and (not f.SpawnerEntity or GetPtrHash(f.SpawnerEntity) == h) then flame = f end
      end
      return flame and flame:ToEffect() or nil
    end
  end
  -- EntityPlayer:GetBombFlags returns a BitSet128 (TearFlags) as on PC; the Switch returns bitset128_t in x0:x1, and
  -- the generated binding read only x0 as an int (always 0 for Sticky Bombs, bit 37)
  do
    local f = C.sym("_ZNK15IsaacRepentance13Entity_Player12GetBombFlagsEb")
    if f then
      local prev = EM.GetBombFlags
      EM.GetBombFlags = function(e, fetus)
        if e.Type ~= 1 then return prev and prev(e, fetus) end
        local l, h = C.call(f, "Bpb", C.ptr(e), fetus and true or false)
        return BitSet128(l, h)
      end
    end
  end
  -- EntityFamiliar:AddCoins (PC 0x609de0: Coins += n; variant 232 clamps to 399 and skips the x01..x99 values below
  -- 300; then it updates the player's familiar cache, not done here). The Switch has no such function
  if not EM.AddCoins then
    EM.AddCoins = function(e, n)
      if e.Type ~= 3 then return end
      local c = (e.Coins or 0) + math.floor(n)
      if e.Variant == 232 then
        if c > 399 then c = 399 end
        while c < 300 and c % 100 >= 1 do c = c + 99 end
      end
      e.Coins = c
    end
  end
  -- PC LuaBridge keeps a class's properties as getter / setter tables in its metatable (__propget / __propset);
  -- mods copy a sprite by walking them (Epiphany's status_effect_library: copySprite[k] = get(sprite))
  local smt = debug.getregistry()["isaaclua.Sprite"]
  if type(smt) == "table" and rawget(smt, "__propget") == nil then
    local get, set = {}, {}
    for _, k in ipairs({"Color", "FlipX", "FlipY", "Offset", "PlaybackSpeed", "Rotation", "Scale"}) do
      get[k] = function(spr) return spr[k] end
      set[k] = function(spr, v) spr[k] = v end
    end
    rawset(smt, "__propget", get)
    rawset(smt, "__propset", set)
  end
  -- PC LuaBridge: indexing a class table (EntityPlayer, Vector...) looks in its metatable first, so
  -- EntityPlayer.__class is the class table (Sodom and Gomorrah: EntityPlayer.__class.GetMaxHearts)
  for name, t in pairs(_G) do
    local mt = type(t) == "table" and getmetatable(t)
    -- the instances' metatable names its class (LuaBridge __type): getmetatable(v).__type == "Vector" (Epiphany)
    local inst = type(mt) == "table" and rawget(mt, "__class")
    if type(inst) == "table" and type(name) == "string" and rawget(inst, "__type") == nil then
      rawset(inst, "__type", name)
    end
    if type(mt) == "table" and rawget(mt, "__class") ~= nil then
      local old = rawget(mt, "__index")
      mt.__index = function(tbl, k)
        local v = rawget(mt, k)
        if v ~= nil then return v end
        if type(old) == "function" then return old(tbl, k) end
        if old ~= nil then return old[k] end
      end
    end
  end
end)()
---------------------------------------------------------------- 0.99.162: the rest of the PC API (apichk on the console)
;(function()
  local function int(v, d) return math.tointeger(v) or (type(v) == "number" and math.floor(v)) or d end
  -- a field of one entity type at a Switch offset; other types keep what the name had
  local function field(name, t, o, ty)
    local g, s = EG[name], ES[name]
    EG[name] = function(e) if e.Type == t then return peek(ptr(e), o, ty) end if g then return g(e) end end
    ES[name] = function(e, v) if e.Type == t then return poke(ptr(e), o, ty, v) end if s then return s(e, v) end end
  end
  -- Entity.SpawnGridIndex: PC Entity+0x284 -> +0x2f8 (Room::spawn_entity stores the grid index there; -1 otherwise)
  do
    local g, st = EG.SpawnGridIndex, ES.SpawnGridIndex
    EG.SpawnGridIndex = function(e) return peek(ptr(e), 0x2f8, "i32") end
    ES.SpawnGridIndex = function(e, v) poke(ptr(e), 0x2f8, "i32", int(v, -1)) end
  end
  -- EntityPlayer.TearsOffset: Entity_Player::SetTearsOffset writes +0x1a20 (PC +0x1580)
  field("TearsOffset", 1, 0x1a20, "vec")
  -- EntityFamiliar:AddHearts / AddKeys: the Switch's own Entity_Familiar::AddHearts (+0xd90) / AddKeys (+0xd94)
  local fam_hearts = native(NS .. "15Entity_Familiar9AddHeartsEi", "_pi", "EntityFamiliar.AddHearts")
  local fam_keys = native(NS .. "15Entity_Familiar7AddKeysEi", "_pi", "EntityFamiliar.AddKeys")
  EM.AddHearts = (function(prev) return function(e, n, ...)
    if e.Type == 3 then return fam_hearts(ptr(e), int(n, 0)) end
    if prev then return prev(e, n, ...) end
  end end)(EM.AddHearts)
  EM.AddKeys = (function(prev) return function(e, n, ...)
    if e.Type == 3 then return fam_keys(ptr(e), int(n, 0)) end
    if prev then return prev(e, n, ...) end
  end end)(EM.AddKeys)

  -- Room (PC offsets from isaac-ng.exe 1.7.9b getters; Switch from the code that uses them):
  -- red heart damage PC +0x120c -> Switch +0x1940 (Entity_Player::TakeDamage); Card Against Humanity PC +0x11f4 ->
  -- +0x1920 (Room::Update calls update_card_against_humanity while it is set, which clears it); rain PC +0x7340 stop
  -- flag -> +0x7c74 (Rainmaker death and Room::TriggerBossDeath set it; update_rain reads it), water current PC
  -- +0x7344 -> +0x7c78 (update_water_current)
  function room:GetRedHeartDamage() return peek(roomp(), 0x1940, "b") or false end
  function room:SetCardAgainstHumanity() poke(roomp(), 0x1920, "u8", 1) end
  function room:StopRain() poke(roomp(), 0x7c74, "u8", 1) end
  function room:GetWaterCurrent() return peek(roomp(), 0x7c78, "vec") or Vector(0, 0) end
  -- Game:HasHallucination: PC Game+0x1c3158 > 0; Switch Game::ShowHallucination stores its countdown at +0x26fa38
  function game:HasHallucination() return (peek(gp(), 0x26fa38, "i32") or 0) > 0 end
  -- Game:GetItemOverlay: the ItemOverlay class (Show) as on PC, where the object has no other Lua methods
  function game:GetItemOverlay() return ItemOverlay end

  -- grid entities: the GridEntity base ends at PC +0x154 / Switch +0x1a0 (door Slot, pit ladder, TNT and spikes
  -- counters all start there: Switch GridEntity_TNT/Spikes Update, GridEntity_Pit::SetLadder)
  local GT = GridEntityType
  local function gtype(g) return peek(rawget(g, "_p"), O.Grid_Type, "i32") end
  local function is_poop(g) return gtype(g) == GT.GRID_POOP end
  local function is_door(g) return gtype(g) == GT.GRID_DOOR end
  local GF = {
    -- door: ExtraVisible / Busted PC +0x278/+0x279 -> +0x308/+0x309 (GridEntity_Door::TryBlowOpen sets Busted);
    -- the animation names are std::strings PC +0x28c.. -> +0x320/+0x338/+0x350/+0x368 (GridEntity_Door dtor)
    Busted = {is_door, 0x309, "b"}, ExtraVisible = {is_door, 0x308, "b"},
    OpenAnimation = {is_door, 0x320, "str", true}, CloseAnimation = {is_door, 0x338, "str", true},
    LockedAnimation = {is_door, 0x350, "str", true}, OpenLockedAnimation = {is_door, 0x368, "str", true},
    -- poop: ReviveTimer PC +0x154 -> +0x1a0 (GridEntity_Poop::RespawnRedPoop / Hurt), StateAnimation std::string
    -- PC +0x158 -> +0x1a8, UnderPlayer PC +0x170 -> +0x1c0 (GridEntity_Poop::Update)
    ReviveTimer = {is_poop, 0x1a0, "i32"}, StateAnimation = {is_poop, 0x1a8, "str", true},
    UnderPlayer = {is_poop, 0x1c0, "b"},
    -- ReducedSpawnRate: PC ReduceSpawnRate stores 1 in the Desc's VarData (PC +0x18, Switch +0x1c)
    ReducedSpawnRate = {is_poop, 0x1c, "i32", false, function(v) return v ~= 0 end},
    HasLadder = {function(g) return gtype(g) == GT.GRID_PIT end, 0x1a0, "b"},
    FrameCnt = {function(g) return gtype(g) == GT.GRID_TNT end, 0x1a0, "i32"},
    Timeout = {function(g) return gtype(g) == GT.GRID_SPIKES end, 0x1a0, "i32"},
    -- rock: Anim / RubbleAnim std::strings PC +0x154/+0x16c -> +0x1a0/+0x1b8 (GridEntity_Rock dtor)
    Anim = {function(g) return g:ToRock() ~= nil end, 0x1a0, "str", true},
    RubbleAnim = {function(g) return g:ToRock() ~= nil end, 0x1b8, "str", true},
  }
  local idx, newidx = grid_mt.__index, grid_mt.__newindex
  function grid_mt.__index(t, k)
    local m = rawget(grid_mt, k)
    if m ~= nil then return m end
    local f = GF[k]
    if f and f[1](t) then
      local v = peek(rawget(t, "_p"), f[2], f[3])
      if f[5] then return f[5](v) end
      return v
    end
    if k == "ExtraSprite" and is_door(t) then return grid_mt._extra_sprite(t) end
    return idx(t, k)
  end
  function grid_mt.__newindex(t, k, v)
    local f = GF[k]
    if f and not f[4] and f[1](t) then
      if f[3] == "b" then return poke(rawget(t, "_p"), f[2], "u8", v and 1 or 0) end
      if f[5] then return poke(rawget(t, "_p"), f[2], "i32", v and 1 or 0) end
      return poke(rawget(t, "_p"), f[2], f[3], int(v, 0))
    end
    return newidx(t, k, v)
  end
  -- door ExtraSprite: the ANM2 at PC +0x164 -> +0x1b0, right after Direction (grid sprites are read at +72)
  local grid_sprite = debug.getregistry()["isaaclua.grid_sprite"]
  function grid_mt._extra_sprite(t) return grid_sprite(offset(rawget(t, "_p"), 0x1b0 - 72)) end
  -- PC GridEntityDoor:IsBusted reads Busted; IsKeyFamiliarTarget = variant DOOR_LOCKED_KEYFAMILIAR (5)
  function grid_mt:IsBusted() return is_door(self) and peek(rawget(self, "_p"), 0x309, "b") or false end
  function grid_mt:IsKeyFamiliarTarget() return is_door(self) and peek(rawget(self, "_p"), 0xc, "i32") == 5 end
  function grid_mt:GetRubbleAnim() if self:ToRock() then return peek(rawget(self, "_p"), 0x1b8, "str") end return "" end
  -- poop: ReduceSpawnRate (PC: VarData = 1), RespawnRedPoop (Switch GridEntity_Poop::RespawnRedPoop)
  local respawn = native(NS .. "15GridEntity_Poop14RespawnRedPoopEv", "_p", "GridEntityPoop.RespawnRedPoop")
  function grid_mt:ReduceSpawnRate() if is_poop(self) then poke(rawget(self, "_p"), 0x1c, "i32", 1) end end
  function grid_mt:RespawnRedPoop() if is_poop(self) then respawn(rawget(self, "_p")) end end
  -- pit: SetLadder / UpdateCollision are the Switch's own
  local set_ladder = native(NS .. "14GridEntity_Pit9SetLadderEb", "_pb", "GridEntityPit.SetLadder")
  local pit_coll = native(NS .. "14GridEntity_Pit15UpdateCollisionEv", "_p", "GridEntityPit.UpdateCollision")
  local function is_pit(g) return gtype(g) == GT.GRID_PIT end
  function grid_mt:SetLadder(b) if is_pit(self) then set_ladder(rawget(self, "_p"), b and true or false) end end
  function grid_mt:UpdateCollision() if is_pit(self) then pit_coll(rawget(self, "_p")) end end
  -- GridEntity:Init(seed) is the Switch GridEntity::Init(unsigned); Render(offset) is virtual: its slot is found
  -- from the subclasses' Render, as Update's is
  local init = native(NS .. "10GridEntity4InitEj", "_pi", "GridEntity.Init")
  function grid_mt:Init(seed) init(rawget(self, "_p"), int(seed, 0) & 0xffffffff) end
  local render_slot
  local RENDER = {"21GridEntity_Decoration", "15GridEntity_Door", "15GridEntity_Fire", "18GridEntity_Gravity",
    "15GridEntity_Lock", "14GridEntity_Pit", "15GridEntity_Poop", "24GridEntity_PressurePlate", "15GridEntity_Rock",
    "17GridEntity_Spikes", "17GridEntity_Stairs", "21GridEntity_Teleporter", "14GridEntity_TNT",
    "19GridEntity_TrapDoor", "15GridEntity_Wall", "14GridEntity_Web"}
  function grid_mt:Render(off)
    local p = rawget(self, "_p")
    local vt = peek(p, 0, "p")
    if not vt then return end
    if render_slot == nil then
      local fns = {}
      for _, c in ipairs(RENDER) do
        local f = C.sym(NS .. c .. "6RenderERKN4KAGE4Math7Vector2E"); if f then fns[tostring(f)] = true end
      end
      render_slot = false
      for i = 0, 63 do if fns[tostring(peek(vt, 8 * i, "p"))] then render_slot = i; break end end
      if not render_slot then C.missing("GridEntity.Render") end
    end
    if render_slot then
      local b = C.buf(8)
      poke(b, 0, "f32", off and off.X or 0); poke(b, 4, "f32", off and off.Y or 0)
      call(peek(vt, 8 * render_slot, "p"), "_pp", p, b)
    end
  end
end)()
