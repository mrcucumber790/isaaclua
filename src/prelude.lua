-- isaaclua prelude: the parts of the PC modding API that are plain Lua (classes, enums, callback
-- registry). Runs once before any mod. The C side passes in `core` (native functions) and gets back
-- the dispatcher it calls from the game hooks.
local core = ...

---------------------------------------------------------------- Vector
local Vector_mt = {}
Vector_mt.__index = Vector_mt
local function vec(x, y) return setmetatable({X = x, Y = y}, Vector_mt) end
-- getmetatable(Vector).__class is the instances' metatable, as with PC LuaBridge (MinimapAPI:InstanceOf tests it)
Vector = setmetatable({}, {__class = Vector_mt, __call = function(_, x, y) return vec(x or 0, y or 0) end})
Vector_mt.__add = function(a, b) return vec(a.X + b.X, a.Y + b.Y) end
Vector_mt.__sub = function(a, b) return vec(a.X - b.X, a.Y - b.Y) end
Vector_mt.__unm = function(a) return vec(-a.X, -a.Y) end
Vector_mt.__mul = function(a, b)
  if type(a) == "number" then return vec(a * b.X, a * b.Y) end
  if type(b) == "number" then return vec(a.X * b, a.Y * b) end
  return vec(a.X * b.X, a.Y * b.Y)
end
Vector_mt.__div = function(a, b)
  if type(b) == "number" then return vec(a.X / b, a.Y / b) end
  return vec(a.X / b.X, a.Y / b.Y)
end
Vector_mt.__eq = function(a, b) return a.X == b.X and a.Y == b.Y end
Vector_mt.__tostring = function(a) return string.format("%g %g", a.X, a.Y) end
function Vector_mt:Length() return math.sqrt(self.X * self.X + self.Y * self.Y) end
function Vector_mt:LengthSquared() return self.X * self.X + self.Y * self.Y end
function Vector_mt:Distance(o) return (self - o):Length() end
function Vector_mt:DistanceSquared(o) return (self - o):LengthSquared() end
function Vector_mt:Dot(o) return self.X * o.X + self.Y * o.Y end
function Vector_mt:Cross(o) return self.X * o.Y - self.Y * o.X end
function Vector_mt:Normalized()
  local l = self:Length()
  if l == 0 then return vec(0, 0) end
  return vec(self.X / l, self.Y / l)
end
function Vector_mt:Normalize()
  local n = self:Normalized(); self.X, self.Y = n.X, n.Y
end
function Vector_mt:Resized(len) return self:Normalized() * len end
function Vector_mt:Rotated(deg)
  local r = math.rad(deg); local c, s = math.cos(r), math.sin(r)
  return vec(self.X * c - self.Y * s, self.X * s + self.Y * c)
end
function Vector_mt:GetAngleDegrees() return math.deg(math.atan(self.Y, self.X)) end
function Vector_mt:Lerp(o, t) return vec(self.X + (o.X - self.X) * t, self.Y + (o.Y - self.Y) * t) end
function Vector_mt:Clamped(x1, y1, x2, y2)
  return vec(math.min(math.max(self.X, x1), x2), math.min(math.max(self.Y, y1), y2))
end
function Vector_mt:Resize(len) local v = self:Resized(len); self.X, self.Y = v.X, v.Y end
function Vector_mt:Clamp(x1, y1, x2, y2) local v = self:Clamped(x1, y1, x2, y2); self.X, self.Y = v.X, v.Y end
function Vector.FromAngle(deg) local r = math.rad(deg); return vec(math.cos(r), math.sin(r)) end
Vector.Zero = vec(0, 0)
Vector.One = vec(1, 1)

---------------------------------------------------------------- Color / KColor
local Color_mt = {}
Color_mt.__index = Color_mt
Color = setmetatable({}, {__class = Color_mt, __call = function(_, r, g, b, a, ro, go, bo)
  return setmetatable({R = r or 1, G = g or 1, B = b or 1, A = a or 1, RO = ro or 0, GO = go or 0, BO = bo or 0}, Color_mt)
end})
Color.Default = Color(1, 1, 1, 1)
-- colorize (R, G, B, amount) is kept as CR CG CB CA; Sprite.Color and Entity:SetColor pass it to the engine
function Color_mt:SetTint(r, g, b, a) self.R, self.G, self.B, self.A = r, g, b, a end
function Color_mt:SetOffset(r, g, b) self.RO, self.GO, self.BO = r, g, b end
function Color_mt:SetColorize(r, g, b, a) self.CR, self.CG, self.CB, self.CA = r, g, b, a end
function Color_mt:GetTint() return {R = self.R, G = self.G, B = self.B, A = self.A} end
function Color_mt:GetOffset() return {R = self.RO, G = self.GO, B = self.BO} end
function Color_mt:GetColorize() return {R = self.CR or 0, G = self.CG or 0, B = self.CB or 0, A = self.CA or 0} end
function Color_mt:Reset() self.R, self.G, self.B, self.A, self.RO, self.GO, self.BO = 1, 1, 1, 1, 0, 0, 0; self.CA = nil end
function Color_mt:Print() return string.format("Color(%g, %g, %g, %g, %g, %g, %g)", self.R, self.G, self.B, self.A,
  self.RO, self.GO, self.BO) end
Color_mt.__tostring = Color_mt.Print
Color_mt.__eq = function(a, b)
  return a.R == b.R and a.G == b.G and a.B == b.B and a.A == b.A and a.RO == b.RO and a.GO == b.GO and a.BO == b.BO
end
Color_mt.__mul = function(a, b)  -- PC: tints multiply, offsets add
  return Color(a.R * b.R, a.G * b.G, a.B * b.B, a.A * b.A, a.RO + b.RO, a.GO + b.GO, a.BO + b.BO)
end
function Color.Lerp(a, b, t)
  local function l(x, y) return x + (y - x) * t end
  return Color(l(a.R, b.R), l(a.G, b.G), l(a.B, b.B), l(a.A, b.A), l(a.RO, b.RO), l(a.GO, b.GO), l(a.BO, b.BO))
end
Color_mt.Lerp = Color.Lerp  -- c:Lerp(other, t) as well as Color.Lerp(a, b, t)
local KColor_mt = {}
KColor_mt.__index = KColor_mt
KColor = setmetatable({}, {__class = KColor_mt, __call = function(_, r, g, b, a)
  return setmetatable({Red = r or 1, Green = g or 1, Blue = b or 1, Alpha = a or 1}, KColor_mt)
end})

---------------------------------------------------------------- enums
ModCallbacks = {
  MC_NPC_UPDATE = 0, MC_POST_UPDATE = 1, MC_POST_RENDER = 2, MC_USE_ITEM = 3, MC_POST_PEFFECT_UPDATE = 4,
  MC_USE_CARD = 5, MC_FAMILIAR_UPDATE = 6, MC_FAMILIAR_INIT = 7, MC_EVALUATE_CACHE = 8,
  MC_POST_PLAYER_INIT = 9, MC_USE_PILL = 10, MC_ENTITY_TAKE_DMG = 11, MC_POST_CURSE_EVAL = 12,
  MC_INPUT_ACTION = 13, MC_POST_GAME_STARTED = 15, MC_POST_GAME_END = 16, MC_PRE_GAME_EXIT = 17,
  MC_POST_NEW_LEVEL = 18, MC_POST_NEW_ROOM = 19, MC_GET_CARD = 20, MC_GET_SHADER_PARAMS = 21,
  MC_EXECUTE_CMD = 22, MC_PRE_USE_ITEM = 23, MC_PRE_ENTITY_SPAWN = 24, MC_POST_FAMILIAR_RENDER = 25,
  MC_PRE_FAMILIAR_COLLISION = 26, MC_POST_NPC_INIT = 27, MC_POST_NPC_RENDER = 28, MC_POST_NPC_DEATH = 29,
  MC_PRE_NPC_COLLISION = 30, MC_POST_PLAYER_UPDATE = 31, MC_POST_PLAYER_RENDER = 32,
  MC_PRE_PLAYER_COLLISION = 33, MC_POST_PICKUP_INIT = 34, MC_POST_PICKUP_UPDATE = 35,
  MC_POST_PICKUP_RENDER = 36, MC_POST_PICKUP_SELECTION = 37, MC_PRE_PICKUP_COLLISION = 38,
  MC_POST_TEAR_INIT = 39, MC_POST_TEAR_UPDATE = 40, MC_POST_TEAR_RENDER = 41, MC_PRE_TEAR_COLLISION = 42,
  MC_POST_PROJECTILE_INIT = 43, MC_POST_PROJECTILE_UPDATE = 44, MC_POST_PROJECTILE_RENDER = 45,
  MC_PRE_PROJECTILE_COLLISION = 46, MC_POST_LASER_INIT = 47, MC_POST_LASER_UPDATE = 48,
  MC_POST_LASER_RENDER = 49, MC_POST_KNIFE_INIT = 50, MC_POST_KNIFE_UPDATE = 51, MC_POST_KNIFE_RENDER = 52,
  MC_PRE_KNIFE_COLLISION = 53, MC_POST_EFFECT_INIT = 54, MC_POST_EFFECT_UPDATE = 55,
  MC_POST_EFFECT_RENDER = 56, MC_POST_BOMB_INIT = 57, MC_POST_BOMB_UPDATE = 58, MC_POST_BOMB_RENDER = 59,
  MC_PRE_BOMB_COLLISION = 60, MC_POST_FIRE_TEAR = 61, MC_PRE_GET_COLLECTIBLE = 62,
  MC_POST_GET_COLLECTIBLE = 63, MC_GET_PILL_COLOR = 64, MC_GET_PILL_EFFECT = 65, MC_GET_TRINKET = 66,
  MC_POST_ENTITY_REMOVE = 67, MC_POST_ENTITY_KILL = 68, MC_PRE_NPC_UPDATE = 69,
  MC_PRE_SPAWN_CLEAN_AWARD = 70, MC_PRE_ROOM_ENTITY_SPAWN = 71, MC_PRE_ENTITY_DEVOLVE = 72,
  MC_PRE_MOD_UNLOAD = 73,
}
CacheFlag = {
  CACHE_DAMAGE = 1, CACHE_FIREDELAY = 2, CACHE_SHOTSPEED = 4, CACHE_RANGE = 8, CACHE_SPEED = 16,
  CACHE_TEARFLAG = 32, CACHE_TEARCOLOR = 64, CACHE_FLYING = 128, CACHE_WEAPON = 256, CACHE_FAMILIARS = 512,
  CACHE_LUCK = 1024, CACHE_SIZE = 2048, CACHE_COLOR = 4096, CACHE_PICKUP_VISION = 8192,
  CACHE_ALL = 65535, CACHE_TWIN_SYNC = 2147483648,
}
-- EntityType, PickupVariant: src/enums.lua
ButtonAction = {
  ACTION_LEFT = 0, ACTION_RIGHT = 1, ACTION_UP = 2, ACTION_DOWN = 3, ACTION_SHOOTLEFT = 4,
  ACTION_SHOOTRIGHT = 5, ACTION_SHOOTUP = 6, ACTION_SHOOTDOWN = 7, ACTION_BOMB = 8, ACTION_ITEM = 9,
  ACTION_PILLCARD = 10, ACTION_DROP = 11, ACTION_PAUSE = 12, ACTION_MAP = 13, ACTION_MENUCONFIRM = 14,
  ACTION_MENUBACK = 15, ACTION_RESTART = 16, ACTION_FULLSCREEN = 17, ACTION_MUTE = 18,
  ACTION_JOINMULTIPLAYER = 19, ACTION_MENULEFT = 20, ACTION_MENURIGHT = 21, ACTION_MENUUP = 22,
  ACTION_MENUDOWN = 23, ACTION_MENULT = 24, ACTION_MENURT = 25, ACTION_MENUTAB = 26, ACTION_CONSOLE = 28,
}
-- CollectibleType, TrinketType, PlayerType, PillColor, ActiveSlot, Keyboard: src/enums.lua (generated), loaded first.
-- An unknown name is nil, as on PC (mods test for names of other game versions).

---------------------------------------------------------------- Game() / Room
local g = core.game
local room = {}
function room:GetFrameCount() return g.RoomFrameCount() end
local game = {}
function game:GetFrameCount() return g.GetFrameCount() end
function game:GetPlayer(i) return Isaac.GetPlayer(i) end
function game:GetNumPlayers() return g.GetNumPlayers() end
function game:IsGreedMode() return g.IsGreedMode() end
function game:GetRoom() return room end
function Game() return game end

---------------------------------------------------------------- callbacks and mods
-- callbacks[id] = list of {mod, fn, param}; Run is called by the C hooks.
local callbacks = {}
local current_mod_dir = nil
-- mods_off[dir]: switched off in the main menu's Mods screen (main.c); their callbacks are skipped
local mods_off = {}

local function traceback(e) return debug.traceback(tostring(e), 2) end

do
-- tell C which keys an engine callback id has callbacks for (main.c cb_key_ok): Runs for other keys are skipped
local function send_keys(id)
  if type(id) ~= "number" or id < 0 or id >= 80 or not core.callback_keys then return end
  local keys, seen = {}, {}
  for _, cb in ipairs(callbacks[id] or {}) do
    local p = cb.param
    if p == nil or p == -1 then return core.callback_keys(id, nil) end
    local k = math.tointeger(p)
    if not k then return core.callback_keys(id, nil) end
    if not seen[k] then seen[k] = true; keys[#keys + 1] = k end
  end
  core.callback_keys(id, keys)
end
function Isaac.AddPriorityCallback(mod, id, priority, fn, param)
  if type(fn) ~= "function" then error("AddCallback: function expected", 2) end
  -- copy on write: a Run in progress keeps walking the list it started with (mods add and remove
  -- callbacks from inside callbacks)
  local list = table.move(callbacks[id] or {}, 1, #(callbacks[id] or {}), 1, {})
  callbacks[id] = list
  local pos = #list + 1  -- lower priority first, registration order among equals
  while pos > 1 and list[pos - 1].priority > priority do pos = pos - 1 end
  table.insert(list, pos, {mod = mod, fn = fn, param = param, priority = priority,
    dir = current_mod_dir or (type(mod) == "table" and rawget(mod, "Path")) or nil})
  if type(id) == "number" and core.callback_supported and not core.callback_supported(id) then
    core.log("callback " .. tostring(id) .. " is not supported on Switch yet (" .. tostring(mod and mod.Name) .. ")")
  end
  send_keys(id)
end
function Isaac.AddCallback(mod, id, fn, param) Isaac.AddPriorityCallback(mod, id, 0, fn, param) end
function Isaac.RemoveCallback(mod, id, fn)
  if not callbacks[id] then return end
  local list = table.move(callbacks[id], 1, #callbacks[id], 1, {})
  callbacks[id] = list
  for i = #list, 1, -1 do
    if list[i].mod == mod and list[i].fn == fn then table.remove(list, i) end
  end
  send_keys(id)
end
end

local Mod_mt = {}
Mod_mt.__index = Mod_mt
function Mod_mt:AddCallback(id, fn, param) Isaac.AddCallback(self, id, fn, param) end
function Mod_mt:AddPriorityCallback(id, pr, fn, param) Isaac.AddPriorityCallback(self, id, pr, fn, param) end
function Mod_mt:RemoveCallback(id, fn) Isaac.RemoveCallback(self, id, fn) end
-- Mod data is kept in the game's save data (native.c: sdmc:/isaaclua/<name>.dat). That is mounted only
-- once the game has picked a user, after the mods load: until then data lives in memory, and the first
-- access after the mount reads the file (it wins over anything saved before, which was only defaults).
local moddata, synced = {}, {}
local function sync(name)
  if synced[name] or not core.save_ready() then return end
  synced[name] = true
  local s = core.save_read(name)
  if s then moddata[name] = s end
end
function Mod_mt:HasData() sync(self.Name); return moddata[self.Name] ~= nil end
function Mod_mt:LoadData() sync(self.Name); return moddata[self.Name] or "" end
function Mod_mt:SaveData(s)
  sync(self.Name)
  moddata[self.Name] = tostring(s)
  if synced[self.Name] and not core.save_write(self.Name, moddata[self.Name]) then
    core.log("could not save the data of " .. tostring(self.Name))
  end
end
function Mod_mt:RemoveData()
  sync(self.Name)
  moddata[self.Name] = nil
  if synced[self.Name] then core.save_write(self.Name, nil) end
end

function RegisterMod(name, apiversion)
  local mod = setmetatable({Name = name, ApiVersion = apiversion, Path = current_mod_dir}, Mod_mt)
  return mod
end

-- include("a.b") loads <mod>/a/b.lua every time (Repentance semantics).
-- The mod is the one the calling file belongs to (as on PC): Mod Config Menu's modconfig.lua, loaded
-- by another mod's require, includes its own scripts.
-- include("a/b.lua") (a file name, as some mods write it) works on PC too: loaded as is
function include(name)
  local rel = string.gsub(name, "\\", "/")
  if not rel:match("%.lua$") then rel = string.gsub(rel, "%.", "/") .. ".lua" end
  local info = debug.getinfo(2, "S")
  local src = info and info.source or ""
  local caller = string.match(src, "^@(rom:/mods/[^/]+/)")
  local chunk, err
  if caller then chunk, err = loadfile(caller .. rel) end
  if not chunk and caller ~= current_mod_dir then chunk, err = loadfile((current_mod_dir or "") .. rel) end
  -- include("json") with no such file in the mod: the game's own scripts/json.lua, as on PC
  if not chunk and package.preload[name] then return package.preload[name](name) end
  -- PC (include at 0x7fc640): a file that is not found is no error; include returns its argument.
  -- A file that is found but does not compile raises the error
  if not chunk and tostring(err):find("cannot open", 1, true) then return name end
  if not chunk then error(err, 2) end
  return chunk()
end

-- key: value the callback's optional parameter is compared with (entity type / variant, cache flag...).
-- mods whose screen drawing is laid out for PC's 480x270 at least: shrunk to fit the Switch's screen
-- isaaclua.ini pc_screen (on by default): every mod sees PC's 480x270 screen and its drawing is mapped onto
-- the Switch's 426x240 (sprite.c g_ui_pc); not the loader's own code and REP+ (drawn for the real screen)
local ui_scales = setmetatable({}, {__index = function(t, name)
  if core.pc_screen and core.pc_screen() and name ~= "isaaclua" and name ~= "isaac-repplus" then return "pc" end
  if name == "Mod Config Menu" then return core.menu_scale and core.menu_scale() end
end})
rawset(ui_scales, 0, false)  -- [0]: the running callback's scale, put back after a nested Run (no new local: the main chunk is at 200)
local running_mod = nil  -- the mod whose callback is running (api.lua: per-mod input quirks)
-- isaaclua.ini perf = 1: time spent in each mod's callbacks, read and reset by PerfReport
local perf_on = core.perf()
debug.getregistry()["isaaclua.set_perf"] = function(on) perf_on = core.perf(on and true or false) end
local perf_ticks = {}
local function PerfReport(ticks_per_ms, frames)
  local list = {}
  for name, t in pairs(perf_ticks) do list[#list + 1] = {name, t} end
  table.sort(list, function(a, b) return a[2] > b[2] end)
  local out = {}
  for i = 1, math.min(3, #list) do
    out[i] = string.format("%s %.1f", list[i][1]:sub(1, 14), list[i][2] / ticks_per_ms / frames)
  end
  perf_ticks = {}
  return table.concat(out, ", ")
end
-- as PC's resources/scripts/main.lua: a callback's param matches when either side is nil or -1 or both are
-- equal, and the first callback that returns a value ends the run (later mods' callbacks are skipped).
-- The loader's own callbacks (mod._isaaclua_internal: game behaviour done in Lua) still run after that.
local Run
local function HasCallbacks(id) local l = callbacks[id]; return l ~= nil and #l > 0 end
do  -- (the main chunk is at Lua's 200-local limit)
local function param_match(a, b) return a == nil or b == nil or a == -1 or b == -1 or a == b end
-- Run is called for every entity every frame (NPC_UPDATE, ENTITY_TAKE_DMG...): Fiend Folio + StageAPI register
-- hundreds of callbacks, and walking them all with the checks below was 27% of all Lua time (HW 0.99.42).
-- Each list (replaced on every Add/Remove, copy on write) keeps the callbacks matching each key, worked out once.
local by_key = setmetatable({}, {__mode = "k"})  -- list -> {[key] = matching callbacks}
local NIL_KEY = {}
local raw_type = type
local scale_of = {}  -- mod name -> ui scale or false
local function matching(list, key)
  local cache = by_key[list]
  if not cache then cache = {}; by_key[list] = cache end
  local k = key == nil and NIL_KEY or key
  local m = cache[k]
  if m then return m end
  m = {}
  for i = 1, #list do
    local cb = list[i]
    if param_match(cb.param, key) then
      if cb.internal == nil then
        cb.internal = raw_type(cb.mod) == "table" and rawget(cb.mod, "_isaaclua_internal") and true or false
      end
      m[#m + 1] = cb
    end
  end
  cache[k] = m
  return m
end
function Run(id, key, ...)
  local list = callbacks[id]
  if not list then return end
  local m = matching(list, key)
  local result
  for i = 1, #m do
    local cb = m[i]
    local internal = cb.internal
    if (result == nil or internal) and not (cb.dir and mods_off[cb.dir]) then
      local mod = cb.mod
      local scale
      if mod then
        local name = mod.Name
        scale = scale_of[name]
        if scale == nil then scale = ui_scales[name] or false; scale_of[name] = scale end
      end
      local outer_scale = rawget(ui_scales, 0)
      if scale ~= outer_scale then core.ui_scale(scale or 1); rawset(ui_scales, 0, scale) end
      local outer = running_mod
      running_mod = mod
      local t0 = perf_on and core.tick()
      local ok, r = xpcall(cb.fn, traceback, mod, ...)
      if t0 then
        local name = mod and mod.Name or "?"
        perf_ticks[name] = (perf_ticks[name] or 0) + core.tick() - t0
      end
      running_mod = outer
      if rawget(ui_scales, 0) ~= outer_scale then core.ui_scale(outer_scale or 1); rawset(ui_scales, 0, outer_scale) end
      if not ok then
        core.error((mod and mod.Name or "?") .. ": " .. tostring(r))
      elseif r ~= nil and result == nil and not internal then
        result = r
      end
    end
  end
  return result
end
-- mods' own callbacks (string ids or vanilla ones run by hand): the first non-nil result wins
function Isaac.RunCallbackWithParam(id, param, ...)
  local list = callbacks[id]
  if not list then return nil end
  for i = 1, #list do
    local cb = list[i]
    if param_match(cb.param, param) and not (cb.dir and mods_off[cb.dir]) then
      local r = cb.fn(cb.mod, ...)
      if r ~= nil then return r end
    end
  end
end
end
function Isaac.RunCallback(id, ...) return Isaac.RunCallbackWithParam(id, nil, ...) end
function Isaac.GetCallbacks(id)
  local out = {}
  for i, cb in ipairs(callbacks[id] or {}) do
    out[i] = {Mod = cb.mod, Function = cb.fn, Param = cb.param, Priority = cb.priority}
  end
  return out
end

-- require("a/b.lua") (a file name, as some mods write it) works on PC: found as "a/b"
table.insert(package.searchers, 2, function(name)
  if not name:match("%.lua$") then return nil end
  local f, err = package.searchpath(name:sub(1, -5):gsub("\\", "/"), package.path)
  if not f then return "\n\t" .. tostring(err) end
  local chunk, e = loadfile(f)
  if not chunk then error(e, 3) end
  return chunk, f
end)

local content_loaders = {}  -- api.lua: what a mod's content/ folder adds (read before its main.lua)
-- (the main chunk is at Lua's 200-locals limit: the Mods menu's state lives in the registry)
local function LoadMod(dir, name, all_mods_path)
  local st = debug.getregistry()["isaaclua.mods"]
  if not st then st = {loaded = {}, all = ""}; debug.getregistry()["isaaclua.mods"] = st end
  st.loaded[dir] = true
  st.all = all_mods_path
  for _, f in ipairs(content_loaders) do
    local ok, err = pcall(f, dir)
    if not ok then core.error(name .. ": content/: " .. tostring(err)) end
  end
  current_mod_dir = dir
  local own = dir .. "?.lua;" .. dir .. "?/init.lua"
  package.path = all_mods_path ~= "" and own .. ";" .. all_mods_path or own
  local chunk, err = loadfile(dir .. "main.lua")
  -- a mod with only resources/ (no main.lua) is fine, as on PC
  if not chunk and tostring(err):find("^cannot open") then package.path = all_mods_path ~= "" and all_mods_path or own; current_mod_dir = nil; return false end
  local ok = chunk ~= nil
  if chunk then ok, err = xpcall(chunk, traceback) end
  package.path = all_mods_path ~= "" and all_mods_path or own  -- require() from callbacks later
  current_mod_dir = nil
  if not ok then core.error(name .. ": " .. tostring(err)) end
  return ok
end

-- src/api.lua follows (same chunk) and returns these to the C side
local exports = {Run = Run, HasCallbacks = HasCallbacks, LoadMod = LoadMod, PerfReport = PerfReport,
  SetModOff = function(dir, off) mods_off[dir] = off or nil end,
  -- the Mods menu switched on a mod that was off at the start: its main.lua runs now, as on PC when the
  -- Mods menu is left (require() then finds it too). What the game itself reads from content/ (items,
  -- entities, players...) waits for the next start.
  EnableMod = function(dir, name)
    local st = debug.getregistry()["isaaclua.mods"] or {loaded = {}, all = ""}
    if st.loaded[dir] then return false end
    local own = dir .. "?.lua;" .. dir .. "?/init.lua"
    local all = st.all ~= "" and st.all .. ";" .. own or own
    local ok = LoadMod(dir, name, all)
    for _, f in ipairs({"items.xml", "entities2.xml", "players.xml", "pocketitems.xml", "costumes2.xml",
                        "music.xml", "sounds.xml", "challenges.xml", "itempools.xml"}) do
      if core.readfile(dir .. "content/" .. f) then
        core.log(name .. ": switched on; its content/ (" .. f .. "...) comes with the next start of the game")
        break
      end
    end
    return ok
  end}
