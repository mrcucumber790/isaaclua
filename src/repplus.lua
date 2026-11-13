---------------------------------------------------------------- Repentance+ layer (isaac-repplus)
-- Appended to the isaaclua prelude after enums_docs.lua (same chunk: peek/poke and the API are
-- visible). Brings the Repentance+ data (PC items.xml, items_metadata.xml, itempools.xml of the
-- latest build) to Switch 1.7.9b: item fields are written into ItemConfig in memory (offsets:
-- isaac-lua/re/itemconfig.md) and the pools are fixed up after the game merged the mod's
-- content/itempools.xml (every REP+ entry) into its own. The data is the mod folder's
-- repplus_data.lua (tools/gen_data.py), read once and dropped. Turning the mod off in the Mods menu
-- turns all of this off. Full change list and status: isaac-repplus/CHANGES.md.
(function()
  local VERSION = "0.6.110"
  REPPLUS_VERSION = VERSION
  local DIR, NAME = "rom:/mods/isaac-repplus/", "isaac-repplus"
  local LAST_VANILLA = 732

  -- items.xml / items_metadata.xml field -> ItemConfig::Item offset
  local FIELDS = {
    q = {0xc8, "i32"}, cq = {0xcc, "i32"}, mc = {0x74, "i32"}, cd = {0x78, "i32"}, ct = {0xb0, "i32"},
    dp = {0x7c, "i16"}, sp = {0x7e, "i16"}, h = {0x5c, "i32"}, mh = {0x58, "i32"}, sh = {0x60, "i32"},
    bh = {0x64, "i32"}, b = {0x68, "i32"}, k = {0x6c, "i32"}, c = {0x70, "i32"}, ic = {0xd0, "i32"},
  }
  local TAG_OFF, CACHE_OFF = 0xc0, 0x54  -- CacheFlags: REP+ flags are only ever added
  -- items_metadata.xml tag -> ItemConfig.TAG_*; tags missing here (REP+'s devilsacrifice) need code
  local TAGS = {
    dead = "DEAD", syringe = "SYRINGE", mom = "MOM", tech = "TECH", battery = "BATTERY", guppy = "GUPPY",
    fly = "FLY", bob = "BOB", mushroom = "MUSHROOM", baby = "BABY", angel = "ANGEL", devil = "DEVIL",
    poop = "POOP", book = "BOOK", spider = "SPIDER", quest = "QUEST", monstermanual = "MONSTER_MANUAL",
    nogreed = "NO_GREED", food = "FOOD", tearsup = "TEARS_UP", offensive = "OFFENSIVE", nokeeper = "NO_KEEPER",
    nolostbr = "NO_LOST_BR", stars = "STARS", summonable = "SUMMONABLE", nocantrip = "NO_CANTRIP",
    wisp = "WISP", uniquefamiliar = "UNIQUE_FAMILIAR", nochallenge = "NO_CHALLENGE", nodaily = "NO_DAILY",
    lazarusshared = "LAZ_SHARED", lazarussharedglobal = "LAZ_SHARED_GLOBAL", noeden = "NO_EDEN",
  }
  -- more changed items than this means the data does not fit this game build: leave it alone
  local MAX_ITEMS_CHANGED, MAX_TAGS_CHANGED = 200, 80

  local data, state = nil, "wait"   -- wait -> ready (items applied) | off
  local function enabled()
    local dirs = rawget(_G, "_isaaclua_content_dirs")
    if not dirs then return true end  -- isaaclua.ini content = 0: no list, items still apply
    for _, d in ipairs(dirs) do if d == NAME then return true end end
    return false
  end
  local function load_data()
    local text = C.readfile(DIR .. "repplus_data.lua")
    if not text then return nil, "no " .. DIR .. "repplus_data.lua" end
    local chunk, err = load(text, "=repplus_data.lua", "t", {})
    if not chunk then return nil, err end
    local ok, t = pcall(chunk)
    if not ok then return nil, t end
    return t
  end
  local function item_p(id)
    local it = Isaac.GetItemConfig():GetCollectible(id)
    return it and rawget(it, "_p")
  end
  local function tag_bits(names)
    local m = 0
    for _, n in ipairs(names) do
      local k = TAGS[n]
      if k then m = m | ItemConfig["TAG_" .. k] end
    end
    return m
  end

  -- every field of every vanilla item to its REP+ value; returns the log line
  local function apply_items()
    local writes, items_changed, tag_items, fields = {}, 0, {}, {}
    -- the tag bits REP+ uses anywhere; other bits (e.g. wisp, set from items.xml) stay as they are
    local mask = 0
    for _, d in pairs(data.items) do if d.tags then mask = mask | tag_bits(d.tags) end end
    for id, d in pairs(data.items) do
      local p = id <= LAST_VANILLA and item_p(id)
      if p then
        local changed = false
        for k, v in pairs(d) do
          local f = FIELDS[k]
          if f and peek(p, f[1], f[2]) ~= v then
            writes[#writes + 1] = {p, f[1], f[2], v}; changed = true; fields[k] = (fields[k] or 0) + 1
          end
        end
        if d.cf and peek(p, CACHE_OFF, "u32") | d.cf ~= peek(p, CACHE_OFF, "u32") then
          writes[#writes + 1] = {p, CACHE_OFF, "u32", peek(p, CACHE_OFF, "u32") | d.cf}; changed = true
          fields.cf = (fields.cf or 0) + 1
        end
        if changed then items_changed = items_changed + 1 end
        if d.tags then
          local cur = peek(p, TAG_OFF, "i64")
          local want = (cur & ~mask) | tag_bits(d.tags)
          if want ~= cur then tag_items[#tag_items + 1] = {p, want} end
        end
      end
    end
    -- trinkets: only the cache flags REP+ added (2Spooky-style stat changes are evaluated by the game)
    for id, d in pairs(data.trinkets or {}) do
      local t = Isaac.GetItemConfig():GetTrinket(id)
      local p = t and rawget(t, "_p")
      if p and peek(p, CACHE_OFF, "u32") | d.cf ~= peek(p, CACHE_OFF, "u32") then
        writes[#writes + 1] = {p, CACHE_OFF, "u32", peek(p, CACHE_OFF, "u32") | d.cf}
        fields.tcf = (fields.tcf or 0) + 1
      end
    end
    if items_changed > MAX_ITEMS_CHANGED then
      return string.format("items NOT changed: %d differ, the data does not fit this build", items_changed)
    end
    for _, w in ipairs(writes) do poke(w[1], w[2], w[3], w[4]) end
    local tags = #tag_items
    if tags <= MAX_TAGS_CHANGED then
      for _, t in ipairs(tag_items) do poke(t[1], TAG_OFF, "i64", t[2]) end
    else tags = "NOT changed (" .. tags .. " differ)" end
    local per = {}
    for k, n in pairs(fields) do per[#per + 1] = k .. " " .. n end
    table.sort(per)
    return string.format("%d items changed (%s), tags %s", items_changed, table.concat(per, ", "), tostring(tags))
  end

  -- Pools. ItemPool = Game+0x242c0: Pool[31] at +8, stride 0x48; Pool+0 total Weight (f32),
  -- Pool+8/+0x10 = vector<PoolItem>
  -- begin/end. PoolItem (0x18, ItemPool::load_pools 0x3c9bc0): id +0, Weight +4, current weight +8,
  -- DecreaseBy +0xc, RemoveOn +0x10. After the game's file the mod's content/itempools.xml adds every
  -- REP+ entry again: the first entry of an item gets the REP+ numbers, the others weight 0, and
  -- vanilla items REP+ has not in that pool weight 0. Items of other mods (id > 732) are not touched.
  -- A continued run keeps its saved weights: then entries are only ever lowered to 0.
  local function apply_pools(continued)
    local g = C.gameptr()
    if not g then return "no game" end
    local ip = offset(g, 0x242c0)
    local removed, set, missing = 0, 0, 0
    for t = 0, 30 do
      local want = {}
      for _, e in ipairs(data.pools[t] or {}) do want[e[1]] = want[e[1]] or e end
      local pool = offset(ip, 8 + t * 0x48)
      local b = peek(pool, 0x8, "p")
      local bi, ei = peek(pool, 0x8, "i64"), peek(pool, 0x10, "i64")
      local count = (b and bi and ei and ei > bi) and (ei - bi) // 0x18 or 0
      local seen = {}
      for i = 0, count - 1 do
        local it = offset(b, i * 0x18)
        local id = peek(it, 0, "i32")
        if id >= 1 and id <= LAST_VANILLA then
          local e = want[id]
          if e and not seen[id] then
            seen[id] = true
            if not continued then
              poke(it, 4, "f32", e[2]); poke(it, 8, "f32", e[2]); poke(it, 0xc, "f32", e[3]); poke(it, 0x10, "f32", e[4])
              set = set + 1
            end
          elseif peek(it, 4, "f32") ~= 0 or peek(it, 8, "f32") ~= 0 then
            poke(it, 4, "f32", 0); poke(it, 8, "f32", 0)
            if not e then removed = removed + 1 end
          end
        end
      end
      for id in pairs(want) do if not seen[id] then missing = missing + 1 end end
      -- Pool+0 = the sum of the entries' Weight, which load_pools (0x3c9cf8) fills once and
      -- GetCollectible (0x3c669c) multiplies its roll by: without it the roll lands past the last
      -- entry, pick_collectible finds nothing and the pedestal becomes Breakfast
      local sum = 0.0
      for i = 0, count - 1 do sum = sum + peek(offset(b, i * 0x18), 4, "f32") end
      if count > 0 then poke(pool, 0, "f32", sum) end
    end
    local s = string.format("pools: %d set, %d removed", set, removed)
    if missing > 0 then s = s .. string.format(", %d REP+ entries missing (mod content off?)", missing) end
    return s
  end

  -- pocketitems.xml / entities2.xml numbers REP+ changed (PC REP+ files vs 1.7.9b). Each write only
  -- when the field still has the 1.7.9b value. Card +0x64 / PillEffect +0x48 MimicCharge, PillEffect
  -- +0x38 GreedModeAllowed (re/itemconfig.md). EntityConfig::Entity (EntityConfig::Load 0x68dc8..
  -- 0x6b4ec, the stack entry at sp+0x110 is copied into the map node): shadowSize/100 +0x28 f32,
  -- collisionRadius +0x3c f32, baseHP +0x54 f32, stageHP +0x58 f32, tags +0x118, shieldStrength +0x120 f32,
  -- shieldFrames +0x124 i32 (Entity_NPC::load_entity_config 0x108a14 copies them to NPC+0xccc/+0xcd0,
  -- used by update_damage_shield 0x119708).
  local POCKET = {  -- {card|pill, id, offset, type, 1.7.9b, REP+}
    {"c", 38, 0x64, "i32", 2, 4}, {"c", 72, 0x64, "i32", 3, 4}, {"c", 84, 0x64, "i32", 3, 2},
    {"p", 21, 0x48, "i32", 6, 4}, {"p", 25, 0x38, "u8", 1, 0}, {"p", 31, 0x38, "u8", 1, 0},
  }
  local ENTITIES = {  -- {type, variant, subtype, offset, 1.7.9b, REP+}
    {9, 4, 0, 0x3c, 8, 5}, {9, 4, 0, 0x28, 0.08, 0.10},
    {219, 0, 0, 0x58, 4, 3}, {285, 0, 0, 0x58, 4, 3}, {286, 0, 0, 0x54, 30, 50},
    {293, 0, 0, 0x28, 0.14, 0.28}, {293, 1, 0, 0x28, 0.14, 0.28}, {293, 2, 0, 0x28, 0.14, 0.28},
    {293, 3, 0, 0x28, 0.14, 0.28},
    -- REP+ armor (damage shield) for final bosses that had none in 1.7.9b
    {84, 0, 0, 0x120, 0, 12}, {84, 10, 0, 0x120, 0, 12}, {102, 0, 0, 0x120, 0, 20},
    {102, 1, 0, 0x120, 0, 25}, {273, 0, 0, 0x120, 0, 25}, {273, 10, 0, 0x120, 0, 10},
  }
  local function apply_config()
    local n, skip = 0, 0
    local ic = Isaac.GetItemConfig()
    for _, w in ipairs(POCKET) do
      local o = w[1] == "c" and ic:GetCard(w[2]) or ic:GetPillEffect(w[2])
      local p = o and rawget(o, "_p")
      if p and peek(p, w[3], w[4]) == w[5] then poke(p, w[3], w[4], w[6]); n = n + 1 else skip = skip + 1 end
    end
    local get, m = C.sym("_ZNK15IsaacRepentance12EntityConfig9GetEntityENS_11eEntityTypeEjj"), C.managerptr()
    if get and m then
      local ec = offset(m, 0x36758)
      for _, w in ipairs(ENTITIES) do
        local key = w[1] << 20 | w[2] << 8 | w[3]
        local e = C.call(get, "ppiuu", ec, w[1], w[2], w[3])
        local cur = e and peek(e, -8, "u32") == key and peek(e, w[4], "f32")
        if cur and math.abs(cur - w[5]) < 1e-4 then poke(e, w[4], "f32", w[6]); n = n + 1 else skip = skip + 1 end
      end
      -- tags u64 +0x118 (EntityConfig::Load's map of names: fly 1, spider 2, poop 4, ghost 8, noreroll 0x10,
      -- cansacrifice 0x20; Entity_NPC::CanReroll 0x11faf4 tests 0x10)
      for _, w in ipairs({{3, 77, 1}, {96, 0, 0x10}, {409, 1, 0x10}, {409, 2, 0x10}, {802, 0, 0x10},
                          {802, 1, 0x10}, {951, 31, 0x10}, {951, 42, 0x10}}) do
        local e = C.call(get, "ppiuu", ec, w[1], w[2], 0)
        local cur = e and peek(e, -8, "u32") == (w[1] << 20 | w[2] << 8) and peek(e, 0x118, "i64")
        if cur and cur & w[3] == 0 then poke(e, 0x118, "i64", cur | w[3]); n = n + 1 else skip = skip + 1 end
      end
    end
    -- recipes.xml: eight bombs (component 15) give Mr. Mega (106) instead of Mr. Boom (37). The fixed recipes
    -- are a std::map<u64, int> at ItemConfig+0x1f8 (ItemConfig::GetCraftingOutput 0x3bdb1c: node left +0,
    -- right +8, key +0x20, value +0x28; key = one byte per component count, CraftingRecipeToU64 0x3bd9ec).
    if m then
      local node, key = peek(m, 0x36538 + 0x1f8, "p"), 0x0f0f0f0f0f0f0f0f
      for _ = 1, 64 do
        if not node then break end
        local k = peek(node, 0x20, "i64")
        if k == key then
          if peek(node, 0x28, "i32") == 37 then poke(node, 0x28, "i32", 106); n = n + 1 else skip = skip + 1 end
          break
        end
        node = peek(node, (k >= 0 and k < key) and 8 or 0, "p")
      end
    end
    -- music.xml: REP+ rebalanced the volume of 63 tracks. Music entries are a vector at Manager+0x36068+0x3d8
    -- (0x70 each, index = track id, id +0, mul f32 +0x54); layers are a new[] array at +0x60 (count at ptr-8,
    -- 0x38 each, layermul f32 +0x30) (music.xml loader 0x425210).
    if m then
    local MUSIC = {  -- {track, mul 1.7.9b, mul REP+, layermul 1.7.9b, layermul REP+}
      {1, 1, 0.94, 1, 0.94}, {2, 1, 0.95, 1, 0.95}, {4, 1, 0.8, 1, 0.8}, {5, 1, 0.91, 1, 0.91}, {6, 1, 0.87, 1, 0.87},
      {9, 1, 0.75, 1, 1}, {11, 1, 0.88, 1, 0.88}, {13, 1, 0.66, 1, 0.66}, {14, 1, 0.94, 1, 0.92}, {15, 1, 0.93, 1, 0.93},
      {16, 1, 0.96, 1, 0.96}, {17, 1, 0.92, 1, 0.92}, {18, 1, 0.98, 1, 0.98}, {19, 1, 0.92, 1, 0.92}, {20, 1, 0.75, 1, 1},
      {21, 1, 0.98, 1, 1}, {22, 1, 0.77, 1, 1}, {23, 1, 0.78, 1, 1}, {24, 1, 0.78, 1, 1}, {25, 1, 0.78, 1, 1},
      {26, 1, 0.77, 1, 1}, {27, 1, 0.83, 1, 1}, {36, 1, 0.95, 1, 1}, {38, 1, 0.88, 1, 1}, {39, 1, 0.75, 1, 1},
      {41, 1, 0.6, 1, 1}, {42, 2.2, 1.7, 1, 1}, {43, 1.25, 0.7, 1, 1}, {44, 1, 0.8, 1, 1}, {45, 1.5, 0.86, 1, 1},
      {47, 2.2, 1.7, 1, 1}, {48, 1.5, 0.95, 1, 1}, {49, 2.2, 1.7, 1, 1}, {52, 1.2, 0.95, 1, 0.95}, {60, 1, 0.9, 1, 1},
      {62, 1, 0.9, 1, 1}, {63, 1, 0.8, 1, 0.8}, {66, 1, 0.9, 1, 1}, {67, 1.5, 1, 1, 1}, {71, 1.9, 1.7, 1, 1},
      {83, 1, 0.9, 1, 1}, {87, 1, 1.25, 1, 1}, {88, 1, 1.1, 1, 1}, {89, 1, 1.25, 1, 1}, {90, 1, 1.25, 1, 1},
      {91, 1, 0.95, 1, 1}, {92, 1, 0.88, 1, 1}, {93, 1, 1.2, 1, 1}, {96, 1, 1.15, 1, 1}, {97, 1, 0.77, 1, 1},
      {98, 1, 0.83, 1, 1}, {102, 1, 0.95, 1, 1}, {103, 1, 0.97, 1, 1}, {104, 1, 0.7, 1, 0.6}, {105, 1.15, 0.9, 1.35, 0.69},
      {106, 1, 0.74, 1.2, 0.74}, {107, 1, 0.9, 1, 0.9}, {108, 1, 0.87, 1, 0.52}, {109, 1.55, 0.83, 1.75, 0.73}, {110, 1.5, 0.86, 2.1, 0.75},
      {114, 1, 0.6, 1, 1}, {115, 1.2, 0.92, 1, 1}, {117, 1, 0.82, 1, 1},
    }
    local mb = peek(m, 0x36068 + 0x3d8, "p")
      local mn = mb and ((peek(m, 0x36068 + 0x3e0, "i64") or 0) - peek(m, 0x36068 + 0x3d8, "i64")) // 0x70 or 0
      for _, w in ipairs(MUSIC) do
        local e = w[1] < mn and offset(mb, w[1] * 0x70)
        if e and peek(e, 0, "i32") == w[1] then
          local cur = peek(e, 0x54, "f32")
          if cur and math.abs(cur - w[2]) < 1e-4 and w[2] ~= w[3] then poke(e, 0x54, "f32", w[3]); n = n + 1 end
          local lp = w[4] ~= w[5] and peek(e, 0x60, "p")
          local ln = lp and peek(lp, -8, "i64") or 0
          for i = 0, (ln > 0 and ln <= 8) and ln - 1 or -1 do
            local lc = peek(lp, i * 0x38 + 0x30, "f32")
            if lc and math.abs(lc - w[4]) < 1e-4 then poke(lp, i * 0x38 + 0x30, "f32", w[5]); n = n + 1 end
          end
        else skip = skip + 1 end
      end
    end
    -- challenges.xml: ChallengeParam vector at Manager+0x36740 (0xf0 each, index = challenge id, name +0);
    -- Manager::LoadChallenges builds the entry at sp+0x78: startingitems vector<int> +0x30, achievements
    -- +0x70 (begin, end; +0x70 found on the console, PAY TO PLAY = 28,21). Both REP+ changes only drop one number, so the vector just gets shorter.
    local CHALLENGES = {  -- {id, name, offset, 1.7.9b, REP+}
      {24, "PAY TO PLAY", 0x70, {28, 21}, {21}},
      {36, "Scat Man", 0x30, {116, 9, 36, 236, 209, 209, 209, 247, 314, 576}, {116, 9, 36, 209, 209, 209, 247, 314, 576}},
    }
    local cp = m and peek(m, 0x36740, "p")
    local cn = cp and ((peek(m, 0x36748, "i64") or 0) - peek(m, 0x36740, "i64")) // 0xf0 or 0
    for _, w in ipairs(CHALLENGES) do
      local e = w[1] < cn and offset(cp, w[1] * 0xf0)
      local vb = e and peek(e, 0, "str") == w[2] and peek(e, w[3], "p")
      local same = vb and ((peek(e, w[3] + 8, "i64") or 0) - peek(e, w[3], "i64")) // 4 == #w[4]
      for i, v in ipairs(w[4]) do same = same and peek(vb, (i - 1) * 4, "i32") == v end
      if same then
        for i, v in ipairs(w[5]) do poke(vb, (i - 1) * 4, "i32", v) end
        poke(e, w[3] + 8, "i64", peek(e, w[3], "i64") + #w[5] * 4); n = n + 1
      else skip = skip + 1 end
    end
    -- wisps.xml: Entity_Familiar::GetWispConfig(int) 0xe30d8 (static): id >= 0x10000 -> special wisp
    -- (s14 = 0x1000e) from the vector at Manager+0x36710, else Item+0xd8; both fall back to item 584's.
    -- ItemConfig::LoadWispConfig 0x3bb6e0..: hp +0, damage +4, fireDelay +0x10 (f32). Colours/gfx not done.
    local WISPS = {  -- {id, offset, 1.7.9b, REP+}
      {441, 0, 2, 6}, {0x1000e, 4, 0.5, 3}, {0x1000e, 0, 2, 6}, {0x1000f, 0, 2, 6},
      {0x10010, 0x10, 60, 25}, {0x10010, 0, 2, 6}, {0x10011, 0, 2, 6},
    }
    local gw = C.sym("_ZN15IsaacRepentance15Entity_Familiar13GetWispConfigEi")
    local def = gw and C.call(gw, "pi", 584)
    for _, w in ipairs(WISPS) do
      local wp = def and C.call(gw, "pi", w[1])
      local cur = wp and wp ~= def and peek(wp, w[2], "f32")
      if cur and math.abs(cur - w[3]) < 1e-4 then poke(wp, w[2], "f32", w[4]); n = n + 1 else skip = skip + 1 end
    end
    -- colours: Color (tint +0, colorize +0x10, offset +0x20, 0x2c bytes) tearColor +0x24, coreColor +0xac;
    -- coreGfx std::string +0x178 (console: 577 = gfx/familiar/wisps/sword.png) (LoadWispConfig 0x3bb440..). Copied from a vanilla wisp with the same
    -- value: core_red from 441, tear_grey from 47, sword.png from 577 (705's own coreGfx is empty).
    local function copy(dst, src, off, len)
      for o = 0, len - 4, 4 do poke(dst, off + o, "u32", peek(src, off + o, "u32")) end
    end
    local function wisp(id) local x = def and C.call(gw, "pi", id); return x ~= def and x or nil end
    local w705, w441, w47, w577 = wisp(705), wisp(441), wisp(47), wisp(577)
    if w705 and w441 and peek(w705, 0xac, "f32") ~= peek(w441, 0xac, "f32") then copy(w705, w441, 0xac, 0x2c); n = n + 1 end
    if w705 and w577 and peek(w705, 0x178, "u8") == 0 and peek(w577, 0x178, "str") ~= "" then
      copy(w705, w577, 0x178, 24); n = n + 1
    end
    for _, id in ipairs({0x1000e, 0x10010}) do
      local x = wisp(id)
      if x and w47 then copy(x, w47, 0x24, 0x2c); n = n + 1 end
    end
    return string.format("pocket/entity/challenge/wisp fields %d changed, %d skipped", n, skip)
  end

  local function start()
    if state ~= "wait" then return end
    if not item_p(1) then return end  -- ItemConfig not loaded yet
    if not enabled() then state = "off"; return end
    -- native call-site hooks (src/repplus_hooks.inc): REP+ changes inside EvaluateItems
    if C.repplus then C.repplus(true) end
    local err
    data, err = load_data()
    if not data then state = "off"; C.log("isaac-repplus " .. VERSION .. ": " .. tostring(err)); return end
    state = "ready"
    C.log("isaac-repplus " .. VERSION .. ": " .. apply_items())
    C.log("isaac-repplus " .. VERSION .. ": " .. apply_config())
    data.items = nil  -- only the pools are needed again (every new run)
  end
  local pools_logged = false
  REPPLUS_START, REPPLUS_POOLS = start, apply_pools  -- host test
  local mod = {Name = NAME}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_GAME_STARTED, -10000, function(_, continued)
    start()
    if state ~= "ready" then return end
    local s = apply_pools(continued)
    if not pools_logged then
      pools_logged = true
      C.log("isaac-repplus: " .. s .. (C.repplus and (select(3, C.repplus()) and ", native on" or ", native OFF") or ""))
    end
  end)
  -- Starting items: REP+ players.xml has no items for Cain (46), Eve (117, 122), Samson (157), Lazarus Risen
  -- (214), Lilith (412 next to 357) and Bethany (584); the REP+ exe gives them in code (its own list of them:
  -- 0x7ce620). 1.7.9b gives them from players.xml, so a mod that replaces players.xml with a REP+-style one
  -- (extra-animated-portraits, which then gives them itself only without REPENTANCE_PLUS) left them out.
  -- A new run gives each player the ones missing (none when players.xml already gave them).
  local START_ITEMS = {[2] = {46}, [5] = {122, 117}, [6] = {157}, [11] = {214}, [13] = {412}, [18] = {584}}
  local function give_start(p)
    for _, id in ipairs(START_ITEMS[p:GetPlayerType()] or {}) do
      if not p:HasCollectible(id, true) then p:AddCollectible(id) end
    end
  end
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_GAME_STARTED, -10000, function(_, continued)
    if state ~= "ready" or continued then return end
    for i = 0, Game():GetNumPlayers() - 1 do give_start(Isaac.GetPlayer(i)) end
  end)
  -- Plan C (475), REP+ UseActiveItem 0x5be295: the death comes after 300 frames in Delirium's room (boss 70)
  -- and 345 in Mother's (88) instead of 90 (Switch Player+0x1d04, set to 90 by UseActiveItem 0x31087c)
  -- Ultra Greed's coins (293.0-3) devolve into a penny with D10 (REP+ 1.9.7.4: entities2.xml gives each a
  -- <devolve id="5.20.0"/>; 1.7.9b has no devolve entry, so Game::DevolveEnemy skips them).
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_ITEM, -10000, function(_, item)
    if state ~= "ready" or item ~= 285 then return end
    for _, e in ipairs(Isaac.FindByType(293, -1, -1, false, false)) do
      if e.Variant <= 3 and e:Exists() and not e:IsDead() then
        Isaac.Spawn(5, 20, 1, e.Position, Vector(0, 0), nil)
        e:Remove()
      end
    end
  end, 285)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_ITEM, -10000, function(_, _, _, p)
    if state ~= "ready" or not p then return end
    local boss = Game():GetRoom():GetBossID()
    local t = (boss == 70 and 300) or (boss == 88 and 345) or nil
    local ep = t and C.ptr(p)
    if ep and peek(ep, 0x1d04, "i32") == 90 then poke(ep, 0x1d04, "i32", t) end
  end, 475)
  -- Potato Peeler (487) with Ball of Bandages (207) and no Cube of Meat (73), REP+ UseActiveItem 0x5bfacc:
  -- the taken heart container levels up Ball of Bandages instead of Cube of Meat. REP+ keeps a new
  -- bandage counter (PC Player+0x1540, next to the meat one) that EvaluateItems adds to the Ball of
  -- Bandages count; 1.7.9b has none (Switch EvaluateItems 0x289938: held + temporary effect only). So the
  -- meat level the use added (Switch Player+0x1924, UseActiveItem 0x30a260) is taken back, the bandage
  -- levels are kept here (saved with the run seed) and the familiars are re-checked like EvaluateItems
  -- 0x2899a0: n // 4 level-4 balls (72) plus one ball of level n % 4 (69..71).
  local peeler_meat, bandages = {}, {}
  local BSTATE = setmetatable({Name = NAME .. "-state"}, Mod_mt)
  -- REP+ option JacobEsauControls (default 1 = Better; 0 = Classic, as 1.7.9b): kept in its own save slot
  local OPTS = setmetatable({Name = NAME .. "-options"}, Mod_mt)
  do
    local ok, d = pcall(function() return OPTS:LoadData() end)
    Options.JacobEsauControls = ok and tonumber(tostring(d):match("je=(%d)")) or 1
  end
  function RepPlusSetJacobEsauControls(v)
    Options.JacobEsauControls = v
    OPTS:SaveData("je=" .. tostring(v))
  end
  local function pidx(p)
    for i = 0, Game():GetNumPlayers() - 1 do
      if GetPtrHash(Isaac.GetPlayer(i)) == GetPtrHash(p) then return i end
    end
  end
  local function save_bandages()
    local t = {tostring(Game():GetSeeds():GetStartSeed())}
    for i = 0, 7 do t[#t + 1] = tostring(bandages[i] or 0) end
    BSTATE:SaveData(table.concat(t, ","))
  end
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_GAME_STARTED, -10000, function(_, continued)
    bandages = {}
    if not continued then return end
    local t = {}
    for v in BSTATE:LoadData():gmatch("[^,]+") do t[#t + 1] = tonumber(v) end
    if t[1] ~= Game():GetSeeds():GetStartSeed() then return end
    for i = 0, 7 do bandages[i] = t[i + 2] ~= 0 and t[i + 2] or nil end
    for i = 0, Game():GetNumPlayers() - 1 do
      local p = Isaac.GetPlayer(i)
      if bandages[i] then p:AddCacheFlags(CacheFlag.CACHE_FAMILIARS); p:EvaluateItems() end
    end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_EVALUATE_CACHE, 10000, function(_, p, flag)
    local i = flag == CacheFlag.CACHE_FAMILIARS and pidx(p)
    local extra = i and bandages[i]
    if not extra then return end
    local n = p:GetCollectibleNum(207) + p:GetEffects():GetCollectibleEffectNum(207) + extra
    local rng, item = p:GetCollectibleRNG(207), Isaac.GetItemConfig():GetCollectible(207)
    for k = 1, 3 do p:CheckFamiliar(68 + k, n % 4 == k and 1 or 0, rng, item) end
    p:CheckFamiliar(72, n // 4, rng, item)
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_PRE_USE_ITEM, -10000, function(_, _, _, p)
    local ep = p and C.ptr(p)
    if ep then peeler_meat[GetPtrHash(p)] = peek(ep, 0x1924, "i32") end
  end, 487)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_ITEM, -10000, function(_, _, _, p)
    if not p then return end
    local ep, before = C.ptr(p), peeler_meat[GetPtrHash(p)]
    peeler_meat[GetPtrHash(p)] = nil
    if state ~= "ready" or not ep or not before or p.Variant ~= 0 then return end
    if p:HasCollectible(73) or not p:HasCollectible(207) then return end
    if peek(ep, 0x1924, "i32") ~= before + 1 then return end
    local i = pidx(p)
    if not i then return end
    poke(ep, 0x1924, "i32", before)
    bandages[i] = (bandages[i] or 0) + 1
    save_bandages()
    p:AddCacheFlags(CacheFlag.CACHE_FAMILIARS)
    p:EvaluateItems()
  end, 487)
  -- Wooden Cross (trinket 121), REP+ trinket added/removed 0x7adc27 / 0x7adeef with 0x7db420 / 0x7db4e0: picking it
  -- up gives a shield per trinket level up to 2 (golden or Mom's Box) instead of always 1 (1.7.9b: one Holy
  -- Mantle effect 313 per pickup, checked on the console), and losing it takes back the unused ones.
  local cross = {}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PEFFECT_UPDATE, -10000, function(_, p)
    if state ~= "ready" or p.Variant ~= 0 then return end
    local h, c = GetPtrHash(p), nil
    c = cross[h]
    local has, fx = p:HasTrinket(121, false), p:GetEffects()
    local n = fx:GetCollectibleEffectNum(313)
    if has and not c then
      local want = math.min(p:GetTrinketMultiplier(121), 2)
      if want == 2 then fx:AddCollectibleEffect(313, false); n = n + 1 end
      cross[h] = {left = want, n = n}
    elseif has then
      if n < c.n then c.left = math.max(0, c.left - (c.n - n)) end
      c.n = n
    elseif c then
      -- 1.7.9b already took one back
      if c.left >= 2 and n > 0 then fx:RemoveCollectibleEffect(313, 1) end
      cross[h] = nil
    end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_LEVEL, -10000, function()
    for _, c in pairs(cross) do c.left, c.n = 2, math.huge end
  end)
  -- Holy Card (card 51, null effect 46) under other holy shields, REP+ TakeDamage 0x77446e..0x7744e9: a blocked
  -- hit takes one Holy Mantle effect 313 and spends the sources in the order Wooden Cross, Blanket, Holy Mantle
  -- item, Holy Card, so the card's marker 46 (which gives the shield back in every new room) is dropped only by
  -- the hit that has no other shield left. 1.7.9b TakeDamage 0x2a4a1c drops the marker on the first blocked hit.
  -- The HUD (REP+ PlayerHUD::RenderHearts 0x844f61..0x845252) names each shield by its source in that order
  -- (ui_hearts.anm2 HolyMantleWoodenCross / HolyMantleBlanket / HolyMantle / HolyMantleHolyCard, the rest plain
  -- HolyMantle), draws the first one in the heart slot after the last heart and, with two or more shields, the
  -- second one behind it at +6, -4 and half alpha.
  local holy = {}
  local function holy_sources(p, fx, n)
    local src, c = {}, cross[GetPtrHash(p)]
    if c and p:HasTrinket(121, false) then
      for _ = 1, math.min(c.left or 0, 2) do src[#src + 1] = "HolyMantleWoodenCross" end
    end
    if p:HasCollectible(535, false) and Game():GetRoom():GetType() == RoomType.ROOM_BOSS then src[#src + 1] = "HolyMantleBlanket" end
    if p:HasCollectible(313, false) then src[#src + 1] = "HolyMantle" end
    if fx:HasNullEffect(46) then src[#src + 1] = "HolyMantleHolyCard" end
    while #src > n do
      -- more sources than shields: the per-room ones are already spent
      local k = 1
      for i, v in ipairs(src) do if v == "HolyMantle" or v == "HolyMantleBlanket" then k = i; break end end
      table.remove(src, k)
    end
    return src
  end
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PEFFECT_UPDATE, -10001, function(_, p)
    if state ~= "ready" then return end
    local h, fx = GetPtrHash(p), p:GetEffects()
    local n, card = fx:GetCollectibleEffectNum(313), fx:HasNullEffect(46)
    local st = holy[h]
    if not st then
      st = {src = holy_sources(p, fx, n)}
      holy[h] = st
    elseif n < st.n then
      for _ = 1, st.n - n do
        local was = table.remove(st.src, 1)
        if was == "HolyMantleHolyCard" then card = false end
      end
      local keep = false
      for _, v in ipairs(st.src) do if v == "HolyMantleHolyCard" then keep = true end end
      if keep and not card then fx:AddNullEffect(46, false, 1); card = true end
    elseif n > st.n then
      local cardpos
      for i, v in ipairs(st.src) do if v == "HolyMantleHolyCard" then cardpos = i end end
      local c = cross[h]
      for _ = 1, n - st.n do
        if card and not st.card and not cardpos then
          st.src[#st.src + 1] = "HolyMantleHolyCard"; cardpos = #st.src
        elseif c and p:HasTrinket(121, false) and not st.wc then
          table.insert(st.src, 1, "HolyMantleWoodenCross"); if cardpos then cardpos = cardpos + 1 end
        else
          table.insert(st.src, cardpos or #st.src + 1, "HolyMantle"); if cardpos then cardpos = cardpos + 1 end
        end
      end
    end
    st.n, st.card, st.wc = n, card, p:HasTrinket(121, false)
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_ROOM, -10000, function() holy = {} end)
  local holy_spr
  _G.REPPLUS_HOLY_POS = _G.REPPLUS_HOLY_POS or {48, 12, 12, 10}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_RENDER, 10000, function()
    if state ~= "ready" or not rawget(_G, "Game") or not Isaac.GetPlayer then return end
    local p = Isaac.GetPlayer(0)
    if not p or not Game():GetHUD():IsVisible() then return end
    local st = holy[GetPtrHash(p)]
    if not st or (st.n or 0) < 1 then return end
    local first, second = st.src[1] or "HolyMantle", st.n > 1 and (st.src[2] or "HolyMantle") or nil
    if first == "HolyMantle" and not second then return end
    if not holy_spr then
      holy_spr = Sprite()
      holy_spr:Load("gfx/ui/rp_holy_shields.anm2", true)
    end
    local t = p:GetPlayerType()
    local cap = (t == 14 or t == 33) and 5 or math.max(p:GetHeartLimit() // 2, 12) - 1
    local slots = (p:GetEffectiveMaxHearts() + 1) // 2 + (p:GetSoulHearts() + 1) // 2 + p:GetBrokenHearts()
    local idx = math.min(slots, cap)
    local P = REPPLUS_HOLY_POS
    local off = Options.HUDOffset or 1
    local x = P[1] + off * 20 + (idx % 6) * P[3]
    local y = P[2] + off * 12 + (idx // 6) * P[4]
    if second then
      holy_spr:Play(second, true)
      holy_spr.Color = Color(1, 1, 1, 0.5)
      holy_spr:Render(Vector(x + 6, y - 4), Vector.Zero, Vector.Zero)
    end
    holy_spr:Play(first, true)
    holy_spr.Color = Color(1, 1, 1, 1)
    holy_spr:Render(Vector(x, y), Vector.Zero, Vector.Zero)
  end)
  -- Modeling Clay (166) / Error (75) in the Extra HUD (repplus_hooks.inc rp_hist_item_c): what they are changes
  -- with the room (Modeling Clay's item is rolled by TriggerNewRoom_TemporaryEffects, after MC_POST_NEW_ROOM),
  -- so on the first update of a room the item history of a player who has one is rebuilt, its sprite cache
  -- cleared (HUD::InvalidateItemHistory(true, true)).
  local inval_hist = C.sym("_ZN15IsaacRepentance3HUD21InvalidateItemHistoryEbb")
  local hist_dirty = false
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_ROOM, 10000, function() hist_dirty = true end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_UPDATE, 10000, function()
    if not hist_dirty then return end
    hist_dirty = false
    if state ~= "ready" or not inval_hist then return end
    local g = C.gameptr()
    if not g then return end
    for i = 0, Game():GetNumPlayers() - 1 do
      local p = Isaac.GetPlayer(i)
      if p and (p:HasTrinket(166, false) or p:HasTrinket(75, false)) then
        C.call(inval_hist, "_pbb", offset(g, 0xe8468), true, true)
        return
      end
    end
  end)
  -- horse I'm Drowsy (41) / I'm Excited!!! (42), REP+ UsePill 0x7c72e9 / 0x7c7334: the horse pill also sets
  -- Level state flag 24 / 25 (new in REP+, Game+0x215f0 on Switch, cleared with the floor), and every room
  -- of the floor starts like after the pill (REP+ 0x7f7a99..0x7f7c1c): Drowsy, broken-watch state 1 and the
  -- room's slowdown (Switch Room+0x78f0, set by UsePill 0x2e1548) at 0x7fffffff; Excited, state 2 and for each
  -- main player the Excited timer (Player+0x1d60) = (Random(2) + 2) * 900 on its pill RNG 42 and null effect 28.
  local LEVEL_FLAGS = 0x215f0
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_PILL, -10000, function(_, eff, _, _, color)
    if state ~= "ready" or not color or color & 0x800 == 0 then return end
    local g = C.gameptr()
    if g then poke(g, LEVEL_FLAGS, "i32", (peek(g, LEVEL_FLAGS, "i32") or 0) | 0x1000000) end
  end, 41)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_PILL, -10000, function(_, eff, _, _, color)
    if state ~= "ready" or not color or color & 0x800 == 0 then return end
    local g = C.gameptr()
    if g then poke(g, LEVEL_FLAGS, "i32", (peek(g, LEVEL_FLAGS, "i32") or 0) | 0x2000000) end
  end, 42)
  -- Hagalaz (rune 32), REP+ 0x5b28e3 (called from UseCard 0x7b4389 after the rocks are broken): every enemy of the
  -- first list is killed (ROCK_CRUMBLE), every enemy of the second loses its armour (Morph to the pair, subtype 0, no
  -- champion; REP+ first clears a Death's Head byte at +0x3b0, not ported: that offset is a pointer on the Switch
  -- and writing it crashed in ClearReferences) with DEATH_BURST_SMALL and 6-11 dust clouds (1000.59, 15-29
  -- frames). The Rune Shard call (0x7b4fbf) does each enemy only on RandomInt(4) == 0: not ported (the Lua side
  -- cannot tell which rune a shard rolled).
  local HAGA_KILL = {[821] = {[0] = 1}, [23] = {[3] = 1}, [859] = {[0] = 1}, [41] = {[3] = 1},
    [818] = {[0] = 1, [1] = 1, [2] = 1}, [302] = {[0] = 1, [10] = 1}, [826] = {[0] = 1}, [42] = {[0] = 1, [1] = 1, [2] = 1},
    [201] = {[0] = 1}, [202] = {[0] = 1, [10] = 1}, [203] = {[0] = 1}, [235] = {[0] = 1}, [236] = {[0] = 1},
    [804] = {[0] = 1}, [809] = {[0] = 1}, [44] = {[0] = 1, [1] = 1}, [877] = {[0] = 1}, [893] = {[0] = 1},
    [852] = {[0] = 1}, [915] = {[1] = 1}}
  local HAGA_MORPH = {[27] = {[0] = {27, 1}, [3] = {27, 1}}, [204] = {[0] = {247, 0}},
    [41] = {[0] = {32, 0}, [2] = {840, 0}, [4] = {840, 0}}, [254] = {[0] = {32, 0}}, [283] = {[0] = {32, 0}},
    [212] = {[0] = {286, 0}, [3] = {286, 0}}}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_CARD, -10000, function(_, _, p)
    if state ~= "ready" then return end
    local sfx, rng = SFXManager(), p and p:GetCardRNG(32) or RNG()
    for _, e in ipairs(Isaac.GetRoomEntities()) do
      local t, v = e.Type, e.Variant
      if t >= 10 and t <= 999 then
        if HAGA_KILL[t] and HAGA_KILL[t][v] then
          e:Kill()
          sfx:Play(SoundEffect.SOUND_ROCK_CRUMBLE, 1, 2, false, 1)
        elseif HAGA_MORPH[t] and HAGA_MORPH[t][v] then
          local to = HAGA_MORPH[t][v]
          e:ToNPC():Morph(to[1], to[2], 0, -1)
          sfx:Play(SoundEffect.SOUND_DEATH_BURST_SMALL, 1, 2, false, 1)
          for _ = 1, rng:RandomInt(6) + 6 do
            local a = rng:RandomFloat() * 2 * math.pi
            local off = Vector(math.cos(a), math.sin(a)) * (rng:RandomFloat() * 20)
            local d = Isaac.Spawn(1000, 59, 0, e.Position + off, off:Resized(rng:RandomFloat() * 5), nil):ToEffect()
            d.Timeout = rng:RandomInt(15) + 15
          end
        end
      end
    end
  end, 32)
  -- horse Percs! (28), REP+ UsePill 0x7c68ee: after The Wafer room effect (as in 1.7.9b), a horse pill also calls
  -- AddHearts(20) (wiki V1.9.7.13: "now full heals Isaac")
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_PILL, -10000, function(_, _, p, _, color)
    if state ~= "ready" or not p or not color or color & 0x800 == 0 then return end
    p:AddHearts(20)
  end, 28)
  -- horse Telepills (19), REP+ UsePill 0x7c5381 + 0x7ce2b0: instead of the random room, a random room of the
  -- floor's current dimension whose type is in the list below (treasure, shop, secret, super secret,
  -- Isaac's, chest, dice, library, challenge, planetarium, arcade, sacrifice), unvisited ones first, picked with pill RNG 19; teleport
  -- animation, LeaveDoor -1. The game's own random teleport has already started here; a second transition
  -- in the same frame replaces it. With no such room REP+ does not move the player (here: the plain one).
  local TELE_TYPES = {[4] = true, [2] = true, [7] = true, [8] = true, [18] = true, [20] = true, [21] = true,
    [12] = true, [11] = true, [24] = true, [9] = true, [13] = true}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_PILL, -10000, function(_, _, p, _, color)
    if state ~= "ready" or not p or not color or color & 0x800 == 0 then return end
    local level = Game():GetLevel()
    local rooms, fresh, seen, old = level:GetRooms(), {}, {}, {}
    for i = 0, rooms.Size - 1 do
      local d = rooms:Get(i)
      local idx = d and d.SafeGridIndex
      if idx and idx >= 0 and not seen[idx] and d.Data and TELE_TYPES[d.Data.Type] then
        seen[idx] = true
        if d.VisitedCount == 0 then fresh[#fresh + 1] = idx else old[#old + 1] = idx end
      end
    end
    local list = #fresh > 0 and fresh or old
    if #list == 0 then return end
    local idx = list[p:GetPillRNG(19):RandomInt(#list) + 1]
    level.LeaveDoor = -1
    Game():StartRoomTransition(idx, -1, 3, p)
  end, 19)
  -- Cursed Skull x3 (43), REP+ Entity_Player::TakeDamage 0x77858a: the health limit is 1 + (multiplier > 2) half
  -- hearts (0x7dd700), so x3 also teleports at one full red heart with no soul/black hearts. The 1.7.9b check
  -- (0x2a8718) only knows 1; that one case is added here, after the hit, with the special room list of the
  -- x2 change (repplus_hooks.inc rp_move_random_c).
  local SKULL_TYPES = {[2] = true, [4] = true, [6] = true, [7] = true, [8] = true, [9] = true, [11] = true,
    [12] = true, [13] = true, [18] = true, [19] = true, [20] = true, [21] = true, [24] = true}
  local skull_hit = setmetatable({}, {__mode = "k"})
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_ENTITY_TAKE_DMG, 10000, function(_, e, amount)
    if state ~= "ready" or amount <= 0 then return end
    local p = e:ToPlayer()
    if p and p:GetTrinketMultiplier(43) >= 3 then skull_hit[p] = p:GetHearts() end
  end, EntityType.ENTITY_PLAYER)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PEFFECT_UPDATE, 10000, function(_, p)
    local before = skull_hit[p]
    if not before then return end
    skull_hit[p] = nil
    if state ~= "ready" or p:GetHearts() ~= 2 or before <= 2 or p:GetSoulHearts() > 0 then return end
    local level = Game():GetLevel()
    local rooms, cur, list, seen = level:GetRooms(), level:GetCurrentRoomIndex(), {}, {}
    for i = 0, rooms.Size - 1 do
      local d = rooms:Get(i)
      local idx = d and d.SafeGridIndex
      if idx and idx >= 0 and idx ~= cur and not seen[idx] and d.Data and SKULL_TYPES[d.Data.Type] then
        seen[idx] = true; list[#list + 1] = idx
      end
    end
    if #list == 0 then return end
    level.LeaveDoor = -1
    Game():StartRoomTransition(list[p:GetTrinketRNG(43):RandomInt(#list) + 1], -1, 3, p)
  end)
  -- horse Amnesia (25), REP+ UsePill 0x7c66ce: after the curse, every pill colour becomes unidentified again
  -- (ItemPool identified flags 0..13 cleared), unless the player has False PHD (654), PHD (75), Virgo (303),
  -- Lucky Foot (46) or Zodiac (392) on Virgo.
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_PILL, -10000, function(_, _, p, _, color)
    if state ~= "ready" or not p or not color or color & 0x800 == 0 then return end
    if p:HasCollectible(654) or p:HasCollectible(75) or p:HasCollectible(303) or p:HasCollectible(46) then return end
    if p:HasCollectible(392) and p.GetZodiacEffect and p:GetZodiacEffect() == 303 then return end
    local pool = Game():GetItemPool()
    for c = 0, 13 do pool:UnidentifyPill(c) end
  end, 25)
  -- horse ??? (31), REP+ UsePill 0x7c694c: after Curse of the Maze, one more curse the floor does not have yet,
  -- picked from Darkness, Lost, Unknown, Blind (Level::AddCurse(c, false)). REP+ rolls the level's RNG
  -- (PC Game+0x182d4); here the pill RNG 31.
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_PILL, -10000, function(_, _, p, _, color)
    if state ~= "ready" or not p or not color or color & 0x800 == 0 then return end
    local level = Game():GetLevel()
    local have, list = level:GetCurses(), {}
    for _, c in ipairs({1, 4, 8, 0x40}) do if have & c == 0 then list[#list + 1] = c end end
    if #list > 0 then level:AddCurse(list[p:GetPillRNG(31):RandomInt(#list) + 1], false) end
  end, 31)
  -- horse Percs! (28), REP+ UsePill 0x7c68f7: also AddHearts(20); horse Addicted! (29), 0x7c68c8: also
  -- AddBrokenHearts(1) (0x7d2dc0). Both right after the pill's own effect, as here.
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_PILL, -10000, function(_, _, p, _, color)
    if state == "ready" and p and color and color & 0x800 ~= 0 then p:AddHearts(20) end
  end, 28)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_PILL, -10000, function(_, _, p, _, color)
    if state == "ready" and p and color and color & 0x800 ~= 0 then p:AddBrokenHearts(1) end
  end, 29)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_ROOM, -10000, function()
    if state ~= "ready" then return end
    local g = C.gameptr()
    local f = g and peek(g, LEVEL_FLAGS, "i32") or 0
    if f & 0x3000000 == 0 then return end
    local room = Game():GetRoom()
    if f & 0x1000000 ~= 0 then
      room:SetBrokenWatchState(1)
      local rp = peek(g, 0x21550, "p")
      if rp then poke(rp, 0x78f0, "i32", 0x7fffffff) end
    end
    if f & 0x2000000 ~= 0 then
      room:SetBrokenWatchState(2)
      for i = 0, Game():GetNumPlayers() - 1 do
        local p = Isaac.GetPlayer(i)
        local ep = p and p.Variant == 0 and C.ptr(p)
        if ep then
          poke(ep, 0x1d60, "i32", (p:GetPillRNG(42):RandomInt(2) + 2) * 900)
          p:GetEffects():AddNullEffect(28, false, 1)
        end
      end
    end
  end)
  -- golden Nuh Uh! (165; earlier notes called it Poker Chip), REP+ pickup pick 0x6dff64: with trinket multiplier 2 (10%) or 3 (20%), an award
  -- heart becomes a double heart (5.10.5) and an award bomb a double bomb (5.40.2). Lua side: the award's
  -- POST_PICKUP_INIT comes right after PRE_SPAWN_CLEAN_AWARD, in the same frame.
  local chip_frame = -1
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_PRE_SPAWN_CLEAN_AWARD, -10000, function()
    chip_frame = Game():GetFrameCount()
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PICKUP_INIT, -10000, function(_, e)
    if state ~= "ready" or chip_frame ~= Game():GetFrameCount() then return end
    chip_frame = -1
    if e.Variant ~= 10 and e.Variant ~= 40 then return end
    local m = 0
    for i = 0, Game():GetNumPlayers() - 1 do
      local p = Isaac.GetPlayer(i)
      if p:HasTrinket(165) then m = p:GetTrinketMultiplier(165); break end
    end
    local chance = (m == 2 and 10) or (m == 3 and 20) or 0
    if chance > 0 and e:GetDropRNG():RandomInt(100) < chance then
      e:ToPickup():Morph(5, e.Variant, e.Variant == 10 and 5 or 2, true, true, true)
    end
  end)
  -- golden Strange Key (175), REP+ Entity_Pickup player collision 0x6ecb8d: on the Blue Womb (stage 9) a locked,
  -- eternal or old chest (5.60 / 5.53 / 5.55, closed) opens without a key at multiplier 2+, as with Paper Clip.
  -- Lua side: next to such a chest the player gets the key it takes (the game skips the collision with 0 keys);
  -- a key the game did not take (Golden Key, chest not touched) is taken back after the frame.
  -- REP+ 0x7107f6 does the same for key doors on that floor (Blue Womb has none by default; not done)
  local skey = nil
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PEFFECT_UPDATE, -10000, function(_, p)
    if state ~= "ready" or skey or Game():GetLevel():GetStage() ~= 9 or p:HasTrinket(19)
        or p:GetTrinketMultiplier(175) < 2 then
      return
    end
    for _, v in ipairs({60, 53, 55}) do
      for _, e in ipairs(Isaac.FindByType(5, v)) do
        if e.SubType ~= 0 and e.Position:Distance(p.Position) < e.Size + p.Size + 12 then
          skey = {p = p, keys = p:GetNumKeys() + 1}
          p:AddKeys(1)
          return
        end
      end
    end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_UPDATE, -10000, function()
    if not skey then return end
    if skey.p:GetNumKeys() >= skey.keys then skey.p:AddKeys(-1) end
    skey = nil
  end)
  -- golden Gilded Key (159), REP+ clear award pick 0x7fb140 (Room::SpawnClearAward -> 0x7facb0): when an award
  -- pickup was chosen and the first Gilded Key owner has multiplier 2 (RNG % 10 == 0) or 3 (RandomInt(4) == 0),
  -- it becomes a chest (5.50); the game's own Gilded Key rule then makes chests golden (5.60)
  local gkey_frame = -1
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_PRE_SPAWN_CLEAN_AWARD, -10000, function()
    gkey_frame = Game():GetFrameCount()
  end)
  local AWARD_V = {[10] = true, [20] = true, [30] = true, [40] = true, [69] = true, [70] = true, [90] = true,
    [300] = true, [350] = true}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PICKUP_INIT, -9999, function(_, e)
    if state ~= "ready" or gkey_frame ~= Game():GetFrameCount() then return end
    gkey_frame = -1
    if not AWARD_V[e.Variant] then return end
    for i = 0, Game():GetNumPlayers() - 1 do
      local p = Isaac.GetPlayer(i)
      if p:HasTrinket(159) then
        local m, rng = p:GetTrinketMultiplier(159), p:GetTrinketRNG(159)
        if (m == 2 and rng:RandomInt(10) == 0) or (m > 2 and rng:RandomInt(4) == 0) then
          e:ToPickup():Morph(5, 60, 0, true, true, true)
        end
        return
      end
    end
  end)
  -- Void portal after Mother (REP+ 10, Room::SpawnClearAward 0x7fc2b2..0x7fc355; 1.7.9b 0x457a24 spawns none):
  -- after her big chest, when the Void is reachable (Level::IsStageAvailable(12, 0)), a Void portal (grid 17,
  -- variant 0, var data 1) goes on the free tile nearest the award spot + (0, 40). REP+ also asks a save field
  -- (PersistentGameData+0xa7c > 1) that has no counterpart found on Switch; it is not checked.
  local function stage_avail(stage, stype)
    local f, g = C.sym("_ZNK15IsaacRepentance5Level16IsStageAvailableENS_11eLevelStageENS_10eStageTypeE"), C.gameptr()
    return f and g and C.call(f, "bpii", g, stage, stype) or false
  end
  -- golden Devil's Crown (146), REP+ level room load 0x73e4d1: the first owner's multiplier 2 (trinket RNG
  -- Random(4) == 0) or 3+ (Random(3) == 0) turns the floor's treasure room into a devil room: its layout comes
  -- from RoomConfig::GetRandomRoom(level RNG, true, stage 0, type 14, shape any (13), variants 0-99,
  -- difficulty 1-10, the room's doors, subtype 1, mode -1) (REP+ 0x73e638). Switch RoomConfig = Game+0x21b78
  -- (Level::place_rooms 0x3d171c); the descriptor's Data (+0x10) is swapped before the room is first entered.
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_LEVEL, -10000, function()
    if state ~= "ready" then return end
    local owner
    for i = 0, Game():GetNumPlayers() - 1 do
      local p = Isaac.GetPlayer(i)
      if p:HasTrinket(146) then owner = p; break end
    end
    if not owner then return end
    local m = owner:GetTrinketMultiplier(146)
    if m < 2 then return end
    local get = C.sym("_ZN15IsaacRepentance10RoomConfig13GetRandomRoomEjbNS0_6eStageENS_9eRoomTypeENS_10eRoomShapeEjjNS0_11eDifficultyES4_RKNSt3__16bitsetILm8EEEjNS0_5eModeE")
    local g = C.gameptr()
    if not get or not g then return end
    local rooms = Game():GetLevel():GetRooms()
    for i = 0, rooms.Size - 1 do
      local d = rooms:Get(i)
      local dp = d and rawget(d, "_p")
      if dp and d.Data and d.Data.Type == RoomType.ROOM_TREASURE and d.VisitedCount == 0 then
        if owner:GetTrinketRNG(146):RandomInt(m == 2 and 4 or 3) == 0 then
          local doors = C.buf(8)
          poke(C.ptr(doors), 0, "u32", (d.AllowedDoors or 0) & 0xff)
          local r = C.call(get, "ppubiiiuuiipui", C.offset(g, 0x21b78), Random(), true, 0, RoomType.ROOM_DEVIL, 13,
            0, 99, 1, 10, doors, 1, -1)
          if r then
            poke(dp, 0x10, "p", r)
            C.log("isaac-repplus: golden Devil's Crown: treasure room " .. d.GridIndex .. " is a devil room")
          end
        end
      end
    end
  end)
  -- Purple Heart (trinket 5), champion loot x2, REP+ Entity_NPC::deathspawn_champion 0x6bfc25 / 0x6bfd8b / 0x6bffc4 /
  -- 0x6c0270: while anyone holds it, the loot loop of a champion runs twice as many times: the heart, battery, pill
  -- or coins of the red, yellow, green, orange and pulsing champions, the pill of tiny and giant ones, and the 2-3
  -- pickups (5.0.4) of the king. 1.7.9b spawns them once; the game passes the dying champion as the spawner, so
  -- each such pickup gets one more of its kind here (the drop comes with the death animation, after MC_POST_NPC_DEATH).
  local champ_loot = { [0] = 10, [1] = 90, [2] = 70, [3] = 20, [16] = 10, [20] = 10, [18] = 70, [19] = 70, [22] = 0 }
  local champ_new, champ_dead = {}, {}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NPC_DEATH, -10000, function(_, npc)
    if state ~= "ready" or not npc:IsChampion() then return end
    local want = champ_loot[npc:GetChampionColorIdx()]
    if want then champ_dead[#champ_dead + 1] = { t = npc.Type, want = want, pos = npc.Position, f = Game():GetFrameCount() } end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PICKUP_INIT, -10000, function(_, e)
    if state == "ready" then champ_new[#champ_new + 1] = e end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_UPDATE, -10000, function()
    local list = champ_new
    champ_new = {}
    if #list == 0 then return end
    local now = Game():GetFrameCount()
    local any = false
    for i = 0, Game():GetNumPlayers() - 1 do if Isaac.GetPlayer(i):HasTrinket(5) then any = true break end end
    for _, e in ipairs(list) do
      if any and e:Exists() and e.SpawnerType ~= 0 then
        local hit = false
        for _, d in ipairs(champ_dead) do
          if e.SpawnerType == d.t and (d.want == 0 or e.Variant == d.want) and e.Position:Distance(d.pos) < 120 then
            local v, st = e.Variant, e.SubType
            if d.want == 0 then v, st = 0, 4 end
            local room = Game():GetRoom()
            Isaac.Spawn(5, v, st, room:FindFreePickupSpawnPosition(e.Position, 0, true), Vector(0, 0), nil)
            C.log("isaac-repplus: Purple Heart: a champion drops one more " .. v .. "." .. st)
            hit = true
            break
          end
        end
        -- the champion's death callback can come after this update in the frame of the drop
        if not hit and e.FrameCount < 2 then champ_new[#champ_new + 1] = e end
      end
    end
    local keep = {}
    for _, d in ipairs(champ_dead) do if now - d.f < 150 then keep[#keep + 1] = d end end
    champ_dead = keep
  end)

  local mother_frame = -1
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_PRE_SPAWN_CLEAN_AWARD, -10000, function()
    local r = Game():GetRoom()
    if r:GetType() == RoomType.ROOM_BOSS and r:GetBossID() == 88 then mother_frame = Game():GetFrameCount() end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PICKUP_INIT, -10000, function(_, e)
    if mother_frame ~= Game():GetFrameCount() or e.Variant ~= 340 then return end
    mother_frame = -1
    if state ~= "ready" or not stage_avail(12, 0) then return end
    local r = Game():GetRoom()
    local i = r:GetGridIndex(r:FindFreePickupSpawnPosition(e.Position + Vector(0, 40), 80, true, false))
    if i >= 0 then
      r:SpawnGridEntity(i, 17, 0, Random(), 1)
      C.log("isaac-repplus: Void portal after Mother at " .. i)
    end
  end)
  -- Boss Rush wave counter (REP+ 10, HUD render 0x9a46bf..0x9a48a8): in the Boss Rush room (type 17) while its
  -- ambush runs, gfx/ui/ui_bosshealthbar_rush.png (64x32) at ((W - 64) / 2, 20 - 24 * (1 - HUD offset)) and
  -- "wave/waves" right-aligned at +53, +7. Ambush = Game+0x26faa8 on Switch (Room::Update 0x46e664; PC
  -- Game+0x265b0): +0x38 running, +0x3c current wave, +0x30 waves. REP+ moves it 20 lower for two HUD states
  -- (PC Game+0x2619c, +0x26630) not found on Switch; not done.
  local AMBUSH = 0x26faa8
  local rush_spr, rush_font
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_RENDER, 10000, function()
    if state ~= "ready" then return end
    local g = C.gameptr()
    if not g or not rawget(_G, "Game") or not Isaac.GetPlayer or not Isaac.GetPlayer(0) or Game():GetRoom():GetType() ~= RoomType.ROOM_BOSSRUSH then return end
    if peek(g, AMBUSH + 0x38, "u8") == 0 or not Game():GetHUD():IsVisible() then return end
    local wave, waves = peek(g, AMBUSH + 0x3c, "i32") or 0, peek(g, AMBUSH + 0x30, "i32") or 0
    if wave < 1 or waves < 1 then return end
    if not rush_spr then
      rush_spr = Sprite()
      rush_spr:Load("gfx/ui/repplus/bossrush_waves.anm2", true)
      rush_spr:Play("Idle", true)
      rush_font = Font()
      rush_font:Load("font/pftempestasevencondensed.fnt")
    end
    local x = (Isaac.GetScreenWidth() - 64) * 0.5
    local y = 20 - 24 * (1 - (Options.HUDOffset or 1))
    rush_spr:Render(Vector(x, y), Vector.Zero, Vector.Zero)
    local txt = wave .. "/" .. waves
    rush_font:DrawString(txt, x + 53 - rush_font:GetStringWidth(txt), y + 7, KColor(1, 1, 1, 1), 0, false)
  end)
  -- Evil Charm (632), REP+ 0x7dbdb0 (asked from Entity_Player::TakeDamage 0x773124): no damage from fire
  -- places (type 33); creep (effect variants below), type 0 variant 10000 and projectile variant 2 only
  -- hurt after 30 frames in a row of it. Not done: the room/grid callers (webs, slowdown).
  local EVIL_CREEP = {}
  for _, v in ipairs({22, 23, 24, 25, 26, 32, 37, 44, 45, 46, 53, 54, 56, 78, 90, 94, 141, 147, 155, 169, 204}) do
    EVIL_CREEP[v] = true
  end
  local evil_streak = {}  -- by GetPtrHash: the callback gets a new userdata each time
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_ENTITY_TAKE_DMG, -10000, function(_, e, _, _, src)
    if state ~= "ready" then return end
    local p = e:ToPlayer()
    if not p or not (p:HasCollectible(632) or p:GetEffects():HasCollectibleEffect(632)) then return end
    local t, v = src and src.Type or -1, src and src.Variant or -1
    if t == 33 then return false end
    if (t == 1000 and EVIL_CREEP[v]) or (t == 0 and v == 10000) or (t == 9 and v == 2) then
      local f = Game():GetFrameCount()
      local key = GetPtrHash(p)
      local s = evil_streak[key]
      if not s or f - s.last > 10 or f < s.last then s = {first = f}; evil_streak[key] = s end
      s.last = f
      if f - s.first < 30 then return false end
    end
  end, 1)
  -- Orphan Socks (571), REP+ Entity_Effect::Update creep cases (variants 22-26, 44-46, 53, 54, 56, 92, 94 at
  -- 0x5ebdfe..0x5ed2a4; 32, 37, 78, 90 at 0x5f15b0): branches that 1.7.9b runs without a Socks check now skip
  -- the player when it has Evil Charm's immunity (0x7dbdb0) or Socks. Ported for the damage; the white creep
  -- slowdown (0x5ec172) already skips Socks in 1.7.9b (Switch 0x9a670).
  local SOCKS_CREEP = {}
  for _, v in ipairs({22, 23, 24, 25, 26, 32, 37, 44, 45, 46, 53, 54, 56, 78, 90, 92, 94}) do SOCKS_CREEP[v] = true end
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_ENTITY_TAKE_DMG, -10000, function(_, e, _, _, src)
    if state ~= "ready" or not src or src.Type ~= 1000 or not SOCKS_CREEP[src.Variant] then return end
    local p = e:ToPlayer()
    if p and p:HasCollectible(571) then return false end
  end, 1)
  -- Necromancer, REP+ player form 15 (playerforms.xml #TRANSFORMATION_NECROMANCER, no costume or cache): its
  -- counter is recounted on each IncrementPlayerFormCounter(15) (0x75e6fe: +1 each for Necronomicon 35,
  -- Missing Page 2 262 and the Missing Page trinket 48), the form is on at 3. With it a Necronomicon slot gets
  -- "powered" (slot VarData 1) on each new floor (Entity_Player::TriggerNewStage 0x7a5def..0x7a5f1d: the
  -- powered slot is fully charged with sound 1049 and a charge bar flash; if none was, slot 0 becomes so). A
  -- powered Necronomicon has charge type 2 (0x791880: rooms and batteries do not charge it), max charge 10
  -- (0x79163c) and min usable charge 1 (0x791733; both native hooks, repplus_hooks.inc), each use takes 1
  -- (UseActiveItem 0x5b4977) and at 0 it turns back to normal (VarData 0) for the rest of the floor. Losing the
  -- form unpowers it (0x75e962). The purple charge bar (HUD 0x846f2f) is native (repplus_hooks.inc).
  local necro = {}
  local function necro_on(p)
    return p:HasCollectible(35) and p:HasCollectible(262) and p:HasTrinket(48, false)
  end
  local SLOT_VAR = 0x1964 + 0x18  -- Switch Player active slots (0x1c each): item at 0x1964, VarData at +0x18
  local function slot_var(p, slot)
    local ep = C.ptr(p)
    return ep and peek(ep, SLOT_VAR + slot * 0x1c, "i32") or 0
  end
  local function set_slot_var(p, slot, v)
    local ep = C.ptr(p)
    if ep then poke(ep, SLOT_VAR + slot * 0x1c, "i32", v) end
  end
  local necro_item_type  -- ItemConfig Item+0xb0 (charge type) of the Necronomicon, 2 while any slot is powered
  local function necro_charge_type(powered)
    local it = Isaac.GetItemConfig():GetCollectible(35)
    local ip = it and rawget(it, "_p")  -- ItemConfig proxies keep the Item* in _p (api.lua config_class)
    if not ip then return end
    if necro_item_type == nil then necro_item_type = peek(ip, 0xb0, "i32") end
    poke(ip, 0xb0, "i32", powered and 2 or necro_item_type)
  end
  local function necro_snd()
    local snd = Isaac.GetSoundIdByName("Necromancer")
    if snd and snd > 0 then SFXManager():Play(snd) end
  end
  -- giantbook 47 (GiantBook_047_Necronomicon_Powered.png, layers as entry 1) when a Necronomicon gets powered
  -- (REP+ ItemOverlay::Show(47, 3, nil) at 0x7a5e6b / 0x7a5f15 on a new floor and 0x75e9f5 when the form comes
  -- back with a powered slot). Switch ItemOverlay (Game+0x25db0) has a fixed table of 47 entries (Show
  -- 0x3c5034 rejects id > 46), so entry 1 (Necronomicon) is shown with its gfx string (entry +0x198, entries
  -- 0x68 apart from +0x0) pointed at the powered picture for the call; Show reads it at once.
  local POWERED_GFX = "gfx/ui/giantbook/GiantBook_047_Necronomicon_Powered.png"
  local function necro_giantbook()
    local g = C.gameptr()
    local show = C.sym("_ZN15IsaacRepentance11ItemOverlay4ShowENS0_10eOverlayIDEiPNS_13Entity_PlayerE")
    local new = C.sym("_Znwm")
    if not (g and show and new) then return end
    local ov, str = offset(g, 0x25db0), offset(g, 0x25db0 + 0x68 + 0x198)
    local save = {peek(str, 0, "i64"), peek(str, 8, "i64"), peek(str, 16, "i64")}
    local n = #POWERED_GFX
    local buf = C.call(new, "pu", n + 1)
    if not buf then return end
    for i = 1, n do poke(buf, i - 1, "u8", POWERED_GFX:byte(i)) end
    poke(buf, n, "u8", 0)
    poke(str, 0, "i64", (n + 1) | 1); poke(str, 8, "i64", n); poke(str, 16, "p", buf)
    pcall(C.call, show, "_piip", ov, 1, 3, nil)
    poke(str, 0, "i64", save[1]); poke(str, 8, "i64", save[2]); poke(str, 16, "i64", save[3])
  end
  local necro_use = {}  -- player hash -> {slot, charge before the use}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PEFFECT_UPDATE, -10000, function(_, p)
    if state ~= "ready" or p.Variant ~= 0 then return end
    local h, on = GetPtrHash(p), necro_on(p)
    if on and not necro[h] then
      Game():GetHUD():ShowItemText("Necromancer!", "", false, true)
      necro_snd()
      for slot = 0, 2 do  -- the form came back with a powered slot (0x75e9a2)
        if p:GetActiveItem(slot) == 35 and slot_var(p, slot) == 1 then necro_giantbook(); break end
      end
    end
    local u = necro_use[h]
    if u then
      necro_use[h] = nil
      local left = u[2] - 1
      p:SetActiveCharge(math.max(left, 0), u[1])
      if left <= 0 then set_slot_var(p, u[1], 0); Game():GetHUD():InvalidateActiveItem(p, u[1]) end
    end
    local powered = false
    for slot = 0, 2 do
      if p:GetActiveItem(slot) == 35 and slot_var(p, slot) == 1 then
        if not on then set_slot_var(p, slot, 0) else powered = true end
      end
    end
    if (on or false) ~= (necro[h] or false) then
      for slot = 0, 3 do
        if p:GetActiveItem(slot) == 35 then Game():GetHUD():InvalidateActiveItem(p, slot) end
      end
    end
    necro[h] = on or nil
    necro_charge_type(powered)
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_PRE_USE_ITEM, -10000, function(_, _, _, p, _, slot)
    if state ~= "ready" or not p or not slot or slot < 0 or slot > 2 then return end
    if p:GetActiveItem(slot) == 35 and slot_var(p, slot) == 1 and necro_on(p) then
      necro_use[GetPtrHash(p)] = {slot, p:GetActiveCharge(slot)}
    end
  end, 35)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_LEVEL, -10000, function()
    if state ~= "ready" then return end
    for i = 0, Game():GetNumPlayers() - 1 do
      local p = Isaac.GetPlayer(i)
      if p.Variant == 0 and necro_on(p) then
        local done
        for slot = 0, 2 do
          if p:GetActiveItem(slot) == 35 then
            if done then set_slot_var(p, slot, 0)
            elseif slot_var(p, slot) == 1 then done = slot end
          end
        end
        if not done and p:GetActiveItem(0) == 35 then set_slot_var(p, 0, 1); done = 0 end
        if done then
          necro_charge_type(true)
          p:SetActiveCharge(10, done)
          Game():GetHUD():FlashChargeBar(p, done)
          Game():GetHUD():InvalidateActiveItem(p, done)
          necro_snd()
          necro_giantbook()
        end
      end
    end
  end)
  -- Black Powder (420), REP+ Entity_NPC kill 0x6acd3e: a fireplace (33, variants 0, 1, 10) put out by a player
  -- (the kill's EntityRef resolved to a player, 0x6a9420) who has Black Powder explodes: BombExplosionEffects
  -- (0x700330) with damage 20, radius 1, line check, DAMAGE_EXPLOSION, the player as source but not hurt. The
  -- bigger bombs (GetBombScale 0x781263) are native (repplus_hooks.inc).
  local fire_hit = {}
  local function ref_player(src)
    local e = src and src.Entity
    for _ = 1, 4 do
      if not e then return nil end
      local p = e:ToPlayer()
      if p then return p end
      local f = e:ToFamiliar()
      if f and f.Player then return f.Player end
      e = e.SpawnerEntity
    end
  end
  -- A put-out fireplace stays with 1 HP and no kill (HW: POST_ENTITY_KILL does not fire for it on Switch), so the
  -- explosion comes on the first update at 1 HP after a hit while lit; the kill path is kept for removed ones.
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_ENTITY_TAKE_DMG, 10000, function(_, e, _, _, src)
    if state ~= "ready" or e.Type ~= 33 or e.HitPoints <= 1 then return end
    fire_hit[GetPtrHash(e)] = ref_player(src) or false
  end)
  local function fire_out(e)
    local h = GetPtrHash(e)
    local p = fire_hit[h]
    fire_hit[h] = nil
    if not p or not p:Exists() or not p:HasCollectible(420) then return end
    if e.Variant ~= 0 and e.Variant ~= 1 and e.Variant ~= 10 then return end
    Game():BombExplosionEffects(e.Position, 20, TearFlags.TEAR_NORMAL, Color.Default, p, 1, true, false,
      DamageFlag.DAMAGE_EXPLOSION)
  end
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_NPC_UPDATE, -10000, function(_, e)
    if state == "ready" and e.HitPoints <= 1 and fire_hit[GetPtrHash(e)] ~= nil then fire_out(e) end
  end, 33)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_ENTITY_KILL, -10000, function(_, e)
    if state == "ready" and e.Type == 33 then fire_out(e) end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_ROOM, -10000, function() fire_hit = {} end)
  -- The Candle (164) / Red Candle (289) with Car Battery, REP+ throw code 0x77e7d5 / 0x77e9c4: the throw spawns
  -- two flames (1000.10) instead of one, with speeds {14, 7} (the second at half the first's speed). 1.7.9b:
  -- one flame at 14. MC_USE_ITEM fires when the candle is lifted, the flame comes with the throw.
  local candle_held, candle_busy = {}, false
  local function candle_use(_, id, _, p)
    if p then candle_held[GetPtrHash(p)] = Game():GetFrameCount() end
  end
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_ITEM, -10000, candle_use, 164)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_ITEM, -10000, candle_use, 289)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_ROOM, -10000, function() candle_held = {} end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_EFFECT_INIT, -10000, function(_, e)
    if state ~= "ready" or candle_busy then return end
    local p = e.SpawnerEntity and e.SpawnerEntity:ToPlayer()
    local h = p and GetPtrHash(p)
    if not h or not candle_held[h] then return end
    candle_held[h] = nil
    if not p:HasCollectible(356) then return end
    candle_busy = true
    local f = Isaac.Spawn(e.Type, e.Variant, e.SubType, e.Position, e.Velocity * 0.5, p)
    candle_busy = false
    if f then f.CollisionDamage = e.CollisionDamage end
  end, 10)
  -- Hushy (familiar 107), REP+ Entity_Familiar::Update 0x61f807..0x61f92b: while it stands (SubType 1, the
  -- player firing; Hushy keeps its stop state in SubType on PC +0x30 and Switch +0x40) a counter (Familiar+0xd4c, set to 30 when it stops) runs down and at 0 fires a ring of 8 tears
  -- (FireProjectile 0x6399e0 at i*45 degrees) and restarts at 30, so every 31 frames; FireProjectile gives
  -- variant 107 tears 6 damage (0x639fa9). 1.7.9b Hushy only stops and blocks.
  local hushy = {}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_FAMILIAR_UPDATE, -10000, function(_, f)
    if state ~= "ready" then return end
    local h = GetPtrHash(f)
    if f.SubType ~= 1 then hushy[h] = nil return end
    local c = hushy[h] or 30
    if c > 0 then hushy[h] = c - 1 return end
    hushy[h] = 30
    for i = 0, 7 do
      local t = f:FireProjectile(Vector.FromAngle(i * 45))
      if t then t.CollisionDamage = 6 end
    end
  end, 107)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_ROOM, -10000, function() hushy = {} end)
  -- Milk! (familiar 100), REP+: it blocks shots (CanBlockProjectiles 0x612b80, native hook) and counts hits in
  -- SubType (Entity_Familiar::TakeDamage 0x62d4b4: flash and +1 up to Familiar+0xf38 = 10, set in Init
  -- 0x60d644); the 10th hit plays the shatter, whose event gives the Milk effect (+1 fire rate) as in 1.7.9b
  -- (Update 0x61e5d8, Switch 0xbd0d0). It stays broken and the effect stays for the rest of the floor (REP+
  -- items.xml makes it persistent); 1.7.9b breaks on the first hit and comes back in the next room. Here the
  -- hits are counted per player, the first 9 cancelled with a flash, the 10th let through to the 1.7.9b break.
  local milk_hits, milk_broken = {}, {}
  local function milk_owner(f) local p = f:ToFamiliar() and f:ToFamiliar().Player return p, p and GetPtrHash(p) end
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_ENTITY_TAKE_DMG, -10000, function(_, e)
    if state ~= "ready" or e.Variant ~= 100 then return end
    local p, h = milk_owner(e)
    if not h then return end
    if milk_broken[h] then return false end
    local n = (milk_hits[h] or 0) + 1
    milk_hits[h] = n
    if n < 10 then
      e:SetColor(Color(1, 1, 1, 1, 0.5, 0, 0), 4, 1, true, false)
      return false
    end
    milk_broken[h] = true
  end, EntityType.ENTITY_FAMILIAR)
  -- a blocked shot is a hit too (1.7.9b's projectile collision with a blocking familiar does not damage Milk!)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_PRE_PROJECTILE_COLLISION, -10000, function(_, pr, col)
    if state ~= "ready" or col.Type ~= EntityType.ENTITY_FAMILIAR or col.Variant ~= 100 or col.SubType ~= 0 then return end
    col:TakeDamage(pr.CollisionDamage, 0, EntityRef(pr), 0)
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_FAMILIAR_UPDATE, -10000, function(_, f)
    if state ~= "ready" then return end
    local p, h = milk_owner(f)
    if not h or not milk_broken[h] then return end
    if f.SubType == 0 then f.SubType = 1; f.Visible = false end
    local te = p:GetEffects()
    if not te:HasCollectibleEffect(436) then te:AddCollectibleEffect(436, true, 1) end
  end, 100)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_LEVEL, -10000, function()
    milk_hits, milk_broken = {}, {}
    for _, f in ipairs(Isaac.FindByType(EntityType.ENTITY_FAMILIAR, 100)) do
      f.SubType = 0; f.Visible = true
      local p = f:ToFamiliar().Player  -- the effect may have been re-added before this callback
      if p then p:GetEffects():RemoveCollectibleEffect(436, -1); p:AddCacheFlags(CacheFlag.CACHE_FIREDELAY); p:EvaluateItems() end
    end
  end)
  -- Dataminer (481), REP+ UseActiveItem 0x5c04f0..0x5c0642: after the 1.7.9b stat swap, every collectible
  -- pedestal in the room (5.100, not empty, not a quest item: tag 0x8000) becomes a new glitched item
  -- (ProceduralItemManager::CreateProceduralItem with the player's Dataminer RNG, as TMTRAINER pedestals).
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_ITEM, -10000, function(_, _, rng, p)
    if state ~= "ready" or not p or p.Variant ~= 0 or not C.repplus_proc_item then return end
    local cfg = Isaac.GetItemConfig()
    local r = p:GetCollectibleRNG(481)
    for _, e in ipairs(Isaac.FindByType(5, 100)) do
      local it = e.SubType ~= 0 and cfg:GetCollectible(e.SubType)
      if e.SubType ~= 0 and not (it and it:HasTags(ItemConfig.TAG_QUEST)) then
        local id = C.repplus_proc_item(r:Next())
        if id and id ~= 0 then e:ToPickup():Morph(5, 100, id, true, true, true) end
      end
    end
  end, 481)
  -- Mega Bean (351), REP+ UseActiveItem 0x5bc52b: no rock wave (1.7.9b 1000.72) any more; instead a room-wide
  -- Mega Bean Explosion 1000.202 at the player, which shares init/update with Mama Mega! 1000.127 (hits each
  -- entity once, does not hurt the player, opens Boss Rush/Hush doors). Switch has no 202 logic, so a 1000.127
  -- from the player with 100 damage (wiki) stands in for it; the 1.7.9b petrify + fart stay as they were.
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_ITEM, -10000, function(_, _, rng, p)
    if state ~= "ready" or not p then return end
    for _, e in ipairs(Isaac.FindByType(1000, 72)) do
      if e.FrameCount == 0 and e.SpawnerEntity and GetPtrHash(e.SpawnerEntity) == GetPtrHash(p) then e:Remove() end
    end
    local x = Isaac.Spawn(1000, 127, 0, p.Position, Vector(0, 0), p)
    if x then x.CollisionDamage = 100 end
  end, 351)
  -- Vasculitis (657), REP+ 0x6bd23a..0x6bda23: the tears a dying enemy bursts into are Booger tears (variant 26)
  -- tinted Color(2, 0, 0, 1, 0.2, 0, 0) ("blood clots") instead of 1.7.9b's blood tears (variant 1); burning (5)
  -- and frozen (41) stay. On Switch those tears are the blood tears with no spawner yet at POST_TEAR_INIT.
  local vasc = {}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_TEAR_INIT, -10000, function(_, t)
    if state == "ready" and t.Variant == 1 and t.SpawnerType == 0 then vasc[#vasc + 1] = t end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_UPDATE, -10000, function()
    if #vasc == 0 then return end
    local list = vasc
    vasc = {}
    local any = false
    for i = 0, Game():GetNumPlayers() - 1 do if Isaac.GetPlayer(i):HasCollectible(657) then any = true end end
    if not any then return end
    for _, t in ipairs(list) do
      if t:Exists() and t.Variant == 1 and t.SpawnerType == 1 then
        t:ChangeVariant(26)
        t:SetColor(Color(2, 0, 0, 1, 0.2, 0, 0), -1, 255, false, true)
      end
    end
  end)
  -- Golden Bat Wing (118), REP+ 0x7a07c8 (the player's enemy-kill handler 0x7a03f0): the flight chance is 1 in 10
  -- at multiplier 2 and 1 in 7 at 3 (1.7.9b 1 in 20 always); the extra rolls make up the difference.
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_ENTITY_KILL, -10000, function(_, e)
    if state ~= "ready" or not e:IsEnemy() or e:HasEntityFlags(EntityFlag.FLAG_FRIENDLY) then return end
    for i = 0, Game():GetNumPlayers() - 1 do
      local p = Isaac.GetPlayer(i)
      local m = p:HasTrinket(118) and p:GetTrinketMultiplier(118) or 0
      if m >= 2 and not p:GetEffects():HasTrinketEffect(118) then
        local want = m == 2 and 1 / 10 or 1 / 7
        if p:GetTrinketRNG(118):RandomFloat() < (want - 1 / 20) / (1 - 1 / 20) then
          p:GetEffects():AddTrinketEffect(118, true)
        end
      end
    end
  end)
  -- Golden Filigree Feather (123), REP+ 0x8006af: an angel (Uriel 271 / Gabriel 272) that drops its item also
  -- drops a half soul heart (multiplier 2: 5.10.8) or a soul heart (3: 5.10.3) next to it
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NPC_DEATH, -10000, function(_, npc)
    if state ~= "ready" or (npc.Type ~= 271 and npc.Type ~= 272) then return end
    for i = 0, Game():GetNumPlayers() - 1 do
      local p = Isaac.GetPlayer(i)
      if p:HasTrinket(123) then
        local m = p:GetTrinketMultiplier(123)
        local sub = m == 2 and 8 or m >= 3 and 3 or nil
        if sub then
          local pos = Game():GetRoom():FindFreePickupSpawnPosition(npc.Position, 0, true)
          Isaac.Spawn(5, 10, sub, pos, Vector(0, 0), npc)
        end
        return
      end
    end
  end)
  -- Short Fuse (133), REP+ Entity_Player bomb placing 0x781d41: next to the 1.7.9b fuse of 22, the placed bomb's
  -- ExplosionDamage (PC Bomb+0x418) gets +15 / +30 / +50 at multiplier 1 / 2 / 3 (wiki: before Bomber Boy's and
  -- Scatter Bombs' multipliers; not for Dr. Fetus bombs, 0x7a0fcb only shortens their fuse as 1.7.9b did)
  -- (done on the bomb's first update, before the bomb applies its own damage multipliers on Switch)
  local fuse_done = {}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_ROOM, -10000, function() fuse_done = {} end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_BOMB_UPDATE, -10000, function(_, bomb)
    if state ~= "ready" or bomb.FrameCount > 1 then return end
    local h = GetPtrHash(bomb)
    if fuse_done[h] == bomb.InitSeed then return end
    local p = bomb.SpawnerEntity and bomb.SpawnerEntity:ToPlayer()
    if not p then return end
    fuse_done[h] = bomb.InitSeed
    if not bomb.IsFetus and p:HasTrinket(133) then
      local m = p:GetTrinketMultiplier(133)
      bomb.ExplosionDamage = bomb.ExplosionDamage + (m >= 3 and 50 or m == 2 and 30 or 15)
    end
  end)
  -- Extension Cord (125), REP+ Entity_Player 0x79d9a5: the electric beam between Isaac and his familiars
  -- (laser 2.4, spawned by the familiar) deals 6 * trinket multiplier instead of a flat 6
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_LASER_UPDATE, -10000, function(_, l)
    if state ~= "ready" or l.Variant ~= 2 or l.SubType ~= 4 or l.CollisionDamage ~= 6 then return end
    local f = l.SpawnerEntity and l.SpawnerEntity:ToFamiliar()
    local p = f and f.Player
    if not p then return end
    local m = p:GetTrinketMultiplier(125)
    if m > 1 then l.CollisionDamage = 6 * m end
  end)
  -- Brain Worm (144), REP+ Entity_Player tear params 0x7c2c33: at multiplier 2 (RNG Next & 3 == 0, 25%) or 3+
  -- (RNG Next & 1 == 0, 50%) the tear gets TEAR_PIERCING (0xc34b50) and the Cupid's Arrow look (variant 11,
  -- blood tears 12); 1.7.9b and multiplier 1 have no such roll
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_FIRE_TEAR, -10000, function(_, tear)
    if state ~= "ready" then return end
    local p = tear.SpawnerEntity and tear.SpawnerEntity:ToPlayer()
    if not p or not p:HasTrinket(144) then return end
    local m = p:GetTrinketMultiplier(144)
    if m < 2 then return end
    local r = p:GetTrinketRNG(144):Next()
    if (m == 2 and r % 4 ~= 0) or (m > 2 and r % 2 ~= 0) then return end
    tear:AddTearFlags(TearFlags.TEAR_PIERCING)
    if tear.Variant == 0 then tear:ChangeVariant(11) elseif tear.Variant == 1 then tear:ChangeVariant(12) end
  end)
  -- Karma (trinket 85), REP+ Entity_Slot player collision 0x662a70 (variant switch 0x663d18): with Karma, after
  -- paying, 1 in 3 (slot RNG Next % 3 == 0) then a roll f in [0,1), m = trinket multiplier:
  --   Beggar (4): f < 0.4 m coins back, < 0.8 heal 2*m (one red heart), < 0.95 DonateLuck(m)   (0x662c0a)
  --   Shop Restock Machine (10): f < 0.15 DonateLuck(m)                                        (0x662c9c)
  --   Blood Donation (2), Devil Beggar (5), Hell Game (15), Confessional (17), paid with damage:
  --     f < 0.4 a Beggar 6.4 near the slot, < 0.8 heal 2*m, < 0.95 DonateLuck(m)              (0x662de9)
  local karma_pay = {}
  local function karma_roll(p, slot, kind)
    if not p:HasTrinket(85) then return end
    local r = p:GetTrinketRNG(85)
    if r:RandomInt(3) ~= 0 then return end
    local f, m = r:RandomFloat(), math.max(p:GetTrinketMultiplier(85), 1)
    if kind == "restock" then
      if f < 0.15 then p:DonateLuck(m) end
    elseif f < 0.4 then
      if kind == "beggar" then p:AddCoins(m)
      else
        local pos = Game():GetRoom():FindFreePickupSpawnPosition(slot.Position + Vector(0, 40), 0, true)
        Isaac.Spawn(6, 4, 0, pos, Vector(0, 0), slot)
      end
    elseif f < 0.8 then p:AddHearts(2 * m)
    elseif f < 0.95 then p:DonateLuck(m) end
  end
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_PRE_PLAYER_COLLISION, -10000, function(_, p, col)
    if state == "ready" and col.Type == 6 and (col.Variant == 4 or col.Variant == 10) and p:HasTrinket(85) then
      karma_pay[#karma_pay + 1] = {p = p, slot = col, coins = p:GetNumCoins(), kind = col.Variant == 4 and "beggar" or "restock"}
    end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_ENTITY_TAKE_DMG, -10000, function(_, e, _, _, src)
    local s = src and src.Entity
    if state == "ready" and e.Type == 1 and s and s.Type == 6 and (s.Variant == 2 or s.Variant == 5 or s.Variant == 15 or s.Variant == 17) then
      local p = e:ToPlayer()
      if p and p:HasTrinket(85) then karma_pay[#karma_pay + 1] = {p = p, slot = s, kind = "blood"} end
    end
  end, EntityType.ENTITY_PLAYER)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_UPDATE, -10000, function()
    if #karma_pay == 0 then return end
    local list = karma_pay
    karma_pay = {}
    for _, k in ipairs(list) do
      if k.kind == "blood" or k.p:GetNumCoins() < k.coins then karma_roll(k.p, k.slot, k.kind) end
    end
  end)
  -- Mars (593), REP+ Entity_Player new fn 0x7c85c0, called where a dash (effect 593) hits a wall (0x79bc69,
  -- Update after grid collision), the room edge (0x7896b1) or an enemy it bounces off (0x78fab5): the dash ends
  -- (RemoveCollectibleEffect 593) and 8 friendly fire jets 1000.147 spawn 40 px around the player (every 45 deg,
  -- only inside the room) with 10 damage, and enemies within 80 px (+ sizes) are pushed away (velocity / 2 + 7 * dir).
  local mars_hit, mars_on = {}, {}
  -- 0x7c85c0 also sets Player+0x13bc to at least 30; slot collision 0x662d69 ignores Blood Donation Machines and
  -- Devil Beggars (and the other paid-with-health slots) while it is >= 15: about half a second after the blast
  local mars_safe = {}
  -- (the Switch slot pays out from its own collision code: the damage is what can be refused here)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_ENTITY_TAKE_DMG, -10000, function(_, e, _, _, src)
    local col = src and src.Entity
    if state ~= "ready" or not col or col.Type ~= 6 then return end
    local v = col.Variant
    if v ~= 2 and v ~= 5 and v ~= 15 and v ~= 17 then return end
    local f = mars_safe[GetPtrHash(e)]
    if f and Game():GetFrameCount() - f < 15 then return false end
  end, EntityType.ENTITY_PLAYER)
  local function mars_blast(p)
    mars_safe[GetPtrHash(p)] = Game():GetFrameCount()
    local room = Game():GetRoom()
    for i = 0, 7 do
      local pos = p.Position + Vector.FromAngle(i * 45) * 40
      if room:IsPositionInRoom(pos, 0) then
        local f = Isaac.Spawn(1000, 147, 0, pos, Vector(0, 0), p)
        if f then f.CollisionDamage = 10; f:AddEntityFlags(EntityFlag.FLAG_FRIENDLY) end  -- flag 0x20000000
      end
    end
    for _, e in ipairs(Isaac.FindInRadius(p.Position, 80 + p.Size, EntityPartition.ENEMY)) do
      local d = e.Position - p.Position
      local l = d:Length()
      if l > 0 and e:IsVulnerableEnemy() and not e:HasEntityFlags(EntityFlag.FLAG_FRIENDLY) then
        e.Velocity = e.Velocity * 0.5 + d / l * 7
      end
    end
  end
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PLAYER_UPDATE, -10000, function(_, p)
    if state ~= "ready" then return end
    local h = GetPtrHash(p)
    local fx = p:GetEffects()
    local was = mars_on[h]
    mars_on[h] = fx:HasCollectibleEffect(593) or nil
    if not mars_on[h] then  -- 1.7.9b itself ends the dash at a wall, before this callback sees the collision
      if was and p:CollidesWithGrid() then mars_blast(p) end
      mars_hit[h] = nil
      return
    end
    if mars_hit[h] or p:CollidesWithGrid() then
      mars_hit[h], mars_on[h] = nil, nil
      fx:RemoveCollectibleEffect(593, -1)
      mars_blast(p)
    end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_PRE_PLAYER_COLLISION, -10000, function(_, p, col)
    if state == "ready" and col:IsVulnerableEnemy() and not col:HasEntityFlags(EntityFlag.FLAG_FRIENDLY)
      and p:GetEffects():HasCollectibleEffect(593) then mars_hit[GetPtrHash(p)] = true end
  end)
  -- Portable Slot (177), REP+ UseActiveItem 0x5b7539 / 0x5b75a0..0x5b7a53: new sounds 898 (portableslot_coin, on
  -- each use) and 899 (portableslot_prize, with every prize); the prizes are those of 1.7.9b (Switch 0x308590).
  local slot_frame
  local function slot_snd(name)
    local id = Isaac.GetSoundIdByName(name)
    if id and id > 0 then SFXManager():Play(id) end
  end
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_ITEM, -10000, function()
    if state ~= "ready" then return end
    slot_snd("Portable Slot Coin")
    slot_frame = Game():GetFrameCount()
  end, 177)
  local function slot_prize(_, e)
    if slot_frame and Game():GetFrameCount() - slot_frame <= 8 then  -- HW: the prize spawns 6 frames after the use
      slot_frame = nil
      slot_snd("Portable Slot Prize")
    end
  end
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PICKUP_INIT, -10000, slot_prize)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NPC_INIT, -10000, slot_prize)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_FAMILIAR_INIT, -10000, slot_prize)
  -- Gello (728) with Car Battery, REP+ throw code 0x77fc72: the throw spawns two Gellos (3.240), their throw
  -- direction turned by -10 and +10 degrees; 1.7.9b throws one. The second is a copy of the thrown one made on
  -- its first update (flags, state and velocity copied).
  local gello_held = {}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_ITEM, -10000, function(_, _, _, p)
    if p then gello_held[GetPtrHash(p)] = true end
  end, 728)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_ROOM, -10000, function() gello_held = {} end)
  local gello_busy = false
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_FAMILIAR_UPDATE, -10000, function(_, f)
    if state ~= "ready" or gello_busy or f.FrameCount > 8 then return end  -- thrown: moves from its 3rd-4th frame (HW)
    local p = f.Player
    local h = p and GetPtrHash(p)
    if not h or not gello_held[h] or f.Velocity:Length() < 1 then return end
    gello_held[h] = nil
    if not p:HasCollectible(356) then return end
    gello_busy = true
    local v = f.Velocity
    f.Velocity = v:Rotated(-10)
    local g = Isaac.Spawn(3, 240, f.SubType, f.Position, v:Rotated(10), p):ToFamiliar()
    gello_busy = false
    if g then
      g:AddEntityFlags(f:GetEntityFlags())
      g.State = f.State
      -- the throw marks each Gello as thrown (REP+ 0x5b1c10(1) = Familiar+0xd76, Switch control_shooting
      -- 0x2b62ac: Familiar+0xd8a = 1); without it the copy is removed when the fire button is let go (HW)
      local ep = C.ptr(g)
      if ep then poke(ep, 0xd8a, "u8", 1) end
    end
  end, 240)
  -- Mega Blast (441) with Car Battery, REP+ UseActiveItem 0x5bdc9b: the beam lasts 1.5 times longer (int(450 * 1.5));
  -- 1.7.9b sets the timer (Switch Player+0x1d6c, UseActiveItem 0x3089d8) to 450.
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_ITEM, -10000, function(_, _, _, p)
    local ep = state == "ready" and p and p:HasCollectible(356) and C.ptr(p)
    if ep and peek(ep, 0x1d6c, "i32") == 450 then poke(ep, 0x1d6c, "i32", 675) end
  end, 441)
  -- New REP+ item sounds (sounds 1027/1028/1040/1048, files from REP+ sfx in content/sounds.xml):
  -- Esau Jr (703) plays SOUND_ESAUJR on the swap (0x7ad349, after the 703 checks at 0x7acf63);
  -- Book of Shadows (58) plays the sigil sound when used (0x7a0c51) and SOUND_BOOK_SHADOWS_END when its
  -- temporary effect runs out (TemporaryEffects::Update 0x930d91); the Mega Mush (625) effect ending plays
  -- SOUND_MEGA_MUSH_SHRINK (0x794d18).
  local function rp_snd(name)
    local id = Isaac.GetSoundIdByName(name)
    if id and id > 0 then SFXManager():Play(id) end
  end
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_ITEM, 10000, function()
    if state == "ready" then rp_snd("Esau Jr") end
  end, 703)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_ITEM, 10000, function()
    if state == "ready" then rp_snd("Book of Shadows Sigil") end
  end, 58)
  -- Forget Me Now (127), REP+ UseActiveItem 0x5b73b4: the use schedules a callback (0x5c7df0) for the next frame
  -- that plays SOUND_FORGET_ME_NOW (1054) for a player.
  local fmn_pending = false
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_ITEM, 10000, function()
    if state == "ready" then fmn_pending = true end
  end, 127)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_UPDATE, 10000, function()
    if fmn_pending then fmn_pending = false rp_snd("Forget Me Now") end
  end)
  -- Familiar sounds, REP+ Entity_Familiar init 0x60f150 / 0x60f1e6: Bumbo (3.88) plays SOUND_BUMBO_LEVELUP_1 (842)
  -- and Sprinkler (3.120) SOUND_SPRINKLER (1070) when they appear; Bumbo's update (0x62fcc9..0x62fe31) plays level
  -- up 2 / 3 / 4 (843 / 844 / 845) as its coins reach 6 / 12 / 24.
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_FAMILIAR_INIT, 10000, function(_, f)
    if state ~= "ready" then return end
    if f.Variant == 88 then rp_snd("Bumbo Level Up 1") elseif f.Variant == 120 then rp_snd("Sprinkler") end
  end)
  local bumbo_coins = {}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_FAMILIAR_UPDATE, 10000, function(_, f)
    if state ~= "ready" then return end
    local h = GetPtrHash(f)
    local was, now = bumbo_coins[h], f.Coins
    bumbo_coins[h] = now
    if not was or now <= was then return end
    if was < 24 and now >= 24 then rp_snd("Bumbo Level Up 4")
    elseif was < 12 and now >= 12 then rp_snd("Bumbo Level Up 3")
    elseif was < 6 and now >= 6 then rp_snd("Bumbo Level Up 2") end
  end, 88)
  -- Little Chubby (3.3) / Big Chubby (3.104), REP+ 0x618b93 / 0x618bdf: the launch (0x617760, called when the
  -- player fires) plays SOUND_LITTLE_CHUBBY_ATTACK (849) / SOUND_BIG_CHUBBY_ATTACK (835). The launch takes the
  -- familiar from rest to a speed of about 10 (Little) or 3 (Big) in one frame (checked on the console), which is
  -- what is caught here.
  local chubby_speed = {}
  local function chubby(f, snd, fast)
    if state ~= "ready" then return end
    local h = GetPtrHash(f)
    local was, now = chubby_speed[h], f.Velocity:Length()
    chubby_speed[h] = now
    if was and was < 0.5 and now >= fast then rp_snd(snd) end
  end
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_FAMILIAR_UPDATE, 10000, function(_, f) chubby(f, "Little Chubby Attack", 8) end, 3)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_FAMILIAR_UPDATE, 10000, function(_, f) chubby(f, "Big Chubby Attack", 2.5) end, 104)
  -- Infamy (242), REP+ 0x65be77: a blocked projectile plays SOUND_INFAMY_DEFLECT (863). The block itself (and its
  -- REP+ greed chance) is native; its 32nd count says how many blocks there were.
  local infamy_seen
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_UPDATE, 10000, function()
    if state ~= "ready" or not C.repplus then return end
    local n = select(32, C.repplus())
    if n and infamy_seen and n > infamy_seen then rp_snd("Infamy Deflect") end
    infamy_seen = n
  end)
  -- Thrown poop (Mom's Bracelet and other lifts), REP+ 0x7d296d: throwing an entity of type 245 plays
  -- SOUND_POOP_THROW (865). The throw sets FLAG_THROWN (checked on the console: HELD until the throw, then THROWN).
  local poop_thrown = {}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_NPC_UPDATE, 10000, function(_, e)
    if state ~= "ready" then return end
    local h = GetPtrHash(e)
    local t = e:HasEntityFlags(EntityFlag.FLAG_THROWN)
    if t and poop_thrown[h] == false then rp_snd("Poop Throw") end
    poop_thrown[h] = t
  end, 245)
  -- Suplex! (709): the grab (suplex state -> 2, REP+ 0x78f066) plays SOUND_SUPLEX_GRAB (1031) and the jump
  -- (state -> 3, REP+ 0x7d48e4) SOUND_SUPLEX_JUMP (1032). The state is Player+0x2678 on Switch
  -- (Entity_Player::handle_collision 0x2dbc6c; PC +0x1f2c).
  local suplex_st = {}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PLAYER_UPDATE, 10000, function(_, pl)
    if state ~= "ready" or not pl:HasCollectible(709) then return end
    local ep = C.ptr(pl)
    if not ep then return end
    local h = GetPtrHash(pl)
    local st = peek(ep, 0x2678, "u32")
    local was = suplex_st[h]
    if was and st ~= was then
      if st == 2 then rp_snd("Suplex Grab") elseif st == 3 then rp_snd("Suplex Jump") end
    end
    suplex_st[h] = st
  end)
  -- Ocular Rift (606): a tear that rolls the rift flag (REP+ 0x7c218f) plays SOUND_OCCULAR_RIFT_SHOOT (1087), and
  -- each rift it opens (effect 1000.180, REP+ 0x649af4 / 0x64a4d3 / 0x677995 / 0x96cd7e / 0x96f2f2)
  -- SOUND_OCCULAR_RIFT_PORTAL (1088).
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_FIRE_TEAR, 10000, function(_, t)
    if state == "ready" and t:HasTearFlags(TearFlags.TEAR_RIFT) then rp_snd("Ocular Rift Shoot") end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_EFFECT_INIT, 10000, function()
    if state == "ready" then rp_snd("Ocular Rift Portal") end
  end, 180)
  -- Butter! (trinket 122), Entity_Player::DropTrinket REP+ 0x771e85..0x771f04: dropping it plays SOUND_BUTTER_DROP
  -- (901). The dropped trinket is spawned with the player as spawner (checked on the console).
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PICKUP_INIT, 10000, function(_, e)
    if state == "ready" and e.SubType & 0x7fff == 122 and e.SpawnerType == 1 then rp_snd("Butter Drop") end
  end, 350)
  -- Cracked Orb (675), Entity_Player::TakeDamage REP+ 0x77651f -> 0x7d80e2: when the hit reveals a room
  -- (DisplayFlags |= 5), SOUND_CRACKED_ORB (850). Checked here as one more fully shown room on the frame after
  -- the hit.
  local function orb_shown()
    local rooms, n = Game():GetLevel():GetRooms(), 0
    for i = 0, rooms.Size - 1 do
      local d = rooms:Get(i)
      if d and d.DisplayFlags & 5 == 5 then n = n + 1 end
    end
    return n
  end
  local orb_before
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_ENTITY_TAKE_DMG, -10000, function(_, e)
    if state == "ready" and e:ToPlayer() and e:ToPlayer():HasCollectible(675) then orb_before = orb_shown() end
  end, 1)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_UPDATE, 10000, function()
    if not orb_before then return end
    if orb_shown() > orb_before then rp_snd("Cracked Orb") end
    orb_before = nil
  end)
  -- Member Card's ladder to the secret shop, REP+ Room::TrySpawnSecretShop 0x7f8644: spawning the tall ladder
  -- (1000.156) plays SOUND_SHOVEL_DIG2 (916); Room::Init's respawn of it on a later visit plays nothing, so here
  -- only the first ladder of a room in a floor sounds
  local ladder_rooms, ladder_seed = {}, nil
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_EFFECT_INIT, 10000, function()
    if state ~= "ready" then return end
    local level = Game():GetLevel()
    local seed = Game():GetSeeds():GetStageSeed(level:GetStage())
    if seed ~= ladder_seed then ladder_rooms, ladder_seed = {}, seed end
    local idx = level:GetCurrentRoomIndex()
    if not ladder_rooms[idx] then
      ladder_rooms[idx] = true
      rp_snd("Shovel Dig2")
    end
  end, 156)
  -- Boss deaths, REP+ ai_little_horn 0x49210f (SOUND_LITTLE_HORN_DEATH 944), ai_the_haunt 0x482b82
  -- (SOUND_HAUNT_DEATH 996) and ai_big_horn 0x454bb4 (SOUND_BIGHORN_DEATH 878): played when the boss itself
  -- (variant 0) dies (the AI checks the killed flag +0x173)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_ENTITY_KILL, 10000, function(_, e)
    if state == "ready" and e.Variant == 0 then rp_snd("REP+ Little Horn Death") end
  end, 404)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_ENTITY_KILL, 10000, function(_, e)
    if state == "ready" and e.Variant == 0 then rp_snd("REP+ Haunt Death") end
  end, 260)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_ENTITY_KILL, 10000, function(_, e)
    if state == "ready" and e.Variant == 0 then rp_snd("REP+ Bighorn Death") end
  end, 411)
  -- Steven (79.1) and Little Steven (79.11), REP+ 0x6c2d9e / 0x6c2e57: dying plays SOUND_STEVEN_DEATH_BIG (961)
  -- or SOUND_STEVEN_DEATH_SMALL (962) with the tear burst
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_ENTITY_KILL, 10000, function(_, e)
    if state ~= "ready" then return end
    if e.Variant == 1 then rp_snd("REP+ Steven Death Big") elseif e.Variant == 11 then rp_snd("REP+ Steven Death Small") end
  end, 79)
  -- Trapdoor jump, REP+ GridEntity_TrapDoor::Update 0x720c53 / 0x720e7a and 0x6e68c3: right before the first
  -- player's PlayExtraAnimation("Trapdoor") SOUND_TRAP_DOOR_LEVEL (1052) plays, once for all players
  local trapdoor_anim, trapdoor_snd = {}, -100
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PLAYER_UPDATE, 10000, function(_, pl)
    if state ~= "ready" then return end
    local h = GetPtrHash(pl)
    local on = pl:GetSprite():IsPlaying("Trapdoor")
    if on and not trapdoor_anim[h] then
      local f = Game():GetFrameCount()
      if f - trapdoor_snd > 10 then rp_snd("Trapdoor Jump") end
      trapdoor_snd = f
    end
    trapdoor_anim[h] = on
  end)
  -- Immaculate Conception (413), REP+ TriggerHeartPickedUp 0x7c84ec: when the 15th heart gives a stat bit not
  -- owned yet (Switch Player+0x1db0, 0x2fc330) SOUND_IMMACULATE_CONCEPTION (1010) plays
  local ic_bits = {}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PLAYER_UPDATE, 10000, function(_, pl)
    if state ~= "ready" or not pl:HasCollectible(413) then return end
    local ep = C.ptr(pl)
    if not ep then return end
    local h = GetPtrHash(pl)
    local b = peek(ep, 0x1db0, "u32")
    local was = ic_bits[h]
    if was and b ~= was and (b & ~was) ~= 0 then rp_snd("Immaculate Conception") end
    ic_bits[h] = b
  end)
  -- Boss sounds: REP+ plays new sounds (ids 874-1078, REPPLUS_SOUNDS.md) where a boss AI starts an animation
  -- (Sprite::Play / SetAnimation right before the sound) or on an animation event (IsEventTriggered). Each row is
  -- {variant or nil, "a" = animation started / "e" = event, name, sound}; found from the REP+ exe by the string
  -- pushed before each new sound (site list: isaac-repplus/REPPLUS_SOUNDS.md).
  local BOSS_SND = {
    [411] = {{nil, "e", "Hide", "REP+ Bighorn Close Big"}, {nil, "e", "Crack", "REP+ Bighorn Crack Big"}, {nil, "e", "Open", "REP+ Bighorn Open Big"}, {nil, "a", "HandSlap", "REP+ Bighorn Hand Appear"}, {nil, "e", "Hide", "REP+ Bighorn Hide"}, {nil, "a", "Charge", "REP+ Bighorn Pre Spit"}, {nil, "a", "SmallHoleClose", "REP+ Bighorn Close Small"}, {nil, "a", "DizzyEnd", "REP+ Bighorn Dizzy Shake"}},
    [265] = {{nil, "a", "Jumping", "REP+ Cage Jump"}, {nil, "a", "Attack1", "REP+ Cage Ribs"}, {nil, "a", "RollStart", "REP+ Cage Roll Start"}, {nil, "a", "Puking", "REP+ Cage Prep Shoot"}, {nil, "a", "Taunt", "REP+ Cage Roll Stop"}, {nil, "a", "RollLoop", "REP+ Cage Roll Bounce"}},
    [66] = {{nil, "a", "Attack02", "REP+ Death Lean"}, {nil, "e", "Vox", "REP+ Death Vox"}, {nil, "a", "Attack01", "REP+ Death Spawn Prep"}, {nil, "a", "Attack04", "REP+ Death Spawn Prep"}, {nil, "a", "Attack", "REP+ Death Spawn Prep"}, {nil, "a", "Throw", "REP+ Death Sickle"}},  -- Throw: the scythes (66.10) SetAnimation 0x45b00e, sound 0x45b039
    [62] = {{nil, "e", "Dive", "REP+ Pin Dive"}, {nil, "e", "Popup", "REP+ Pin Popup"}},
    [43] = {{1, "a", "Walk", "REP+ Gish Jump"}, {1, "a", "JumpUp", "REP+ Gish Jump High"}},
    [36] = {{nil, "e", "FaceAttackAppear", "REP+ Gurdy Face Attack Appear"}, {nil, "e", "FaceAttackHide", "REP+ Gurdy Face Attack Hide"}, {nil, "e", "SmileAppear", "REP+ Gurdy Face Smile Appear"}, {nil, "e", "SmileHide", "REP+ Gurdy Face Smile Hide"}},
    [404] = {{nil, "e", "BombDrop", "REP+ Little Horn Bomb Drop"}, {nil, "e", "Dive", "REP+ Little Horn Dive"}, {nil, "e", "HoleOpen", "REP+ Little Horn Hole Open"}, {nil, "e", "HoleExit", "REP+ Little Horn Hole Exit"}},
    [69] = {{nil, "e", "Giggle", "REP+ Loki Giggle"}, {nil, "e", "Jump", "REP+ Loki Jump Out"}, {nil, "e", "JumpIn", "REP+ Loki Jump In"}},
    [264] = {{nil, "e", "EndSucking", "REP+ Megafatty Gulp"}, {nil, "e", "StartSucking", "REP+ Megafatty Suckin"}, {nil, "e", "Shoot", "REP+ Megafatty Vomit"}},
    [401] = {{nil, "a", "Attack2Shooting", "REP+ Stain Attack Vox", true}},  -- once per attack (HW: 90 a minute on loop restarts)
    [410] = {{nil, "e", "ScareSFX", "REP+ Sisters Vis Scare"}},
    [81] = {{nil, "e", "FlapChase", "REP+ Fallen Flap Chase"}, {nil, "e", "Flap", "REP+ Fallen Flap"}, {nil, "e", "OpenWings", "REP+ Fallen Open Wings"}},
    [403] = {{nil, "e", "ArmsUp", "REP+ Forsaken Arms Up"}},
    [914] = {{nil, "e", "Shoot", "REP+ Clog Poopsmoke"}},
    [259] = {{nil, "a", "Vanish", "REP+ Imp Warp Out"}},
  }
  local boss_anim = {}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_NPC_UPDATE, 10000, function(_, npc)
    if state ~= "ready" then return end
    local rules = BOSS_SND[npc.Type]
    if not rules then return end
    local sp = npc:GetSprite()
    local h = GetPtrHash(npc)
    local an, fr = sp:GetAnimation(), sp:GetFrame()
    local prev = boss_anim[h]
    local started = prev and (prev[1] ~= an or fr < prev[2])
    boss_anim[h] = {an, fr}
    for _, r in ipairs(rules) do
      if r[1] == nil or r[1] == npc.Variant then
        if r[2] == "e" then
          if sp:IsEventTriggered(r[3]) then rp_snd(r[4]) end
        elseif started and an == r[3] and not (r[5] and prev[1] == an) then rp_snd(r[4]) end
      end
    end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_ROOM, 10000, function() boss_anim = {} end)
  -- Abyss locusts (3.231, SubType = the item): REP+ rewrote locusts.xml. The parts 1.7.9b's parser knows (count,
  -- scale, speed, damage, plain tear flags, colours, png gfx) are in the mod's resources-dlc3/locusts.xml
  -- (tools/repplus/gen_locusts.py); the REP+ anm2 sprites ("anm2=locust_bomb.anm2&animation=locust_bomb&
  -- spritesheet=0-x.png", REP+ parser 0x72b530) come from locusts_data.lua and are loaded here on init.
  -- Abyss's own locusts (706) take one of its 16 kinds by the locust's seed; other items take their rows in order.
  local locust_rows, locust_err
  local locust_n, locust_frame = {}, -1
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_FAMILIAR_INIT, 10000, function(_, f)
    if state ~= "ready" then return end
    if locust_rows == nil and not locust_err then
      local text = C.readfile(DIR .. "locusts_data.lua")
      local chunk = text and load(text, "=locusts_data.lua", "t", {})
      local ok, t = false, nil
      if chunk then ok, t = pcall(chunk) end
      if ok and type(t) == "table" then locust_rows = t else locust_err = true; C.log("isaac-repplus: no locusts_data.lua") end
    end
    local rows = locust_rows and locust_rows[f.SubType]
    if not rows then return end
    local fr = Game():GetFrameCount()
    if fr ~= locust_frame then locust_n, locust_frame = {}, fr end
    local i
    if f.SubType == 706 then i = f.InitSeed % #rows + 1
    else i = (locust_n[f.SubType] or 0) % #rows + 1; locust_n[f.SubType] = i end
    local g = rows[i].gfx
    if not g then return end
    local sp = f:GetSprite()
    sp:Load("gfx/familiar/locusts/" .. g.anm2, false)
    for layer, png in pairs(g.sheets) do sp:ReplaceSpritesheet(layer, "gfx/familiar/locusts/" .. png) end
    sp:LoadGraphics()
    sp:Play(g.anim or sp:GetDefaultAnimation(), true)
  end, 231)
  -- TEAR_EGG on a hit, as REP+ ApplyTearFlagEffects does it (0x67cfe1..0x67d3f1): white creep (1000.44) at the hit
  -- position, BOIL_HATCH (vol 0.5, delay 2, pitch 1-1.2), then on a coin flip 1-2 blue flies, else 1-2 blue spiders
  -- thrown from the player
  local function rp_egg_hit(pl, e, pos, rng)
    local c = Isaac.Spawn(1000, 44, 0, pos, Vector.Zero, pl)
    c:ToEffect().Timeout = 60
    SFXManager():Play(SoundEffect.SOUND_BOIL_HATCH, 0.5, 2, false, 1 + rng:RandomFloat() * 0.2)
    if rng:RandomInt(2) == 0 then
      for _ = 1, rng:RandomInt(2) + 1 do pl:AddBlueFlies(1, pos, e) end
    else
      for _ = 1, rng:RandomInt(2) + 1 do pl:ThrowBlueSpider(pl.Position, pos) end
    end
  end
  -- Parasitoid (461): in REP+ Technology and Brimstone no longer override it (wiki, Parasitoid, Overrides
  -- "except in Repentance+"; Technology 2 "rapid damage ticks can cause a large amount of flies and spiders").
  -- 1.7.9b's egg roll is Random(max(7 - luck, 2)) == 0 in GetTearHitParams (Switch 0x2aefc4, REP+ 0x7c1a82, same
  -- in both); here a player's laser hit on an enemy takes the same roll and does what an egg sack tear does:
  -- slowing white creep and 1-2 blue flies or spiders.
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_ENTITY_TAKE_DMG, 10000, function(_, e, amount, flags, src)
    if state ~= "ready" or not e:IsVulnerableEnemy() then return end
    local l = src and src.Entity
    if not l then return end
    -- a player's laser hits with the player as the source and DAMAGE_LASER (HW: Brimstone ticks came as 1.0)
    local pl
    if l.Type == 1 and flags & DamageFlag.DAMAGE_LASER ~= 0 then pl = l:ToPlayer()
    elseif l.Type == 7 then pl = l.SpawnerEntity and l.SpawnerEntity:ToPlayer() end
    if not pl or not pl:HasCollectible(461) then return end
    local rng = pl:GetCollectibleRNG(461)
    if rng:RandomInt(math.max(7 - math.floor(pl.Luck), 2)) ~= 0 then return end
    rp_egg_hit(pl, e, e.Position, rng)
  end)
  -- Enemies killed while frozen (FLAG_ICE: Uranus...), REP+ 0x6ca814..0x6ca851: on that death REP+ also calls
  -- Entity_Player::TriggerEnemyDeath(npc) for every player (the on-kill items: Lusty Blood, Charm of the Vampire,
  -- Jar of Flies, Samson's Lock, Bird's Foot, Bat Wing, Sigil of Baphomet, Vasculitis, Death's List, Berserk!,
  -- Red Stew, 4.5 Volt, Jumper Cables...); 1.7.9b only calls it from Entity_NPC::Update (0x115904), which a frozen
  -- enemy never reaches (HW: Lusty Blood count stays). The function itself skips FLAG_NO_REWARD enemies.
  -- Gold Hearts and The Lost / T.Lost / Keeper / T.Keeper (REP+ 1.9.7.13): REP+ AddGoldenHearts 0x7ca0c2 sends these
  -- characters straight to the golden-heart break (0x7ca650(1) = Switch Entity_Player::trigger_golden_hearts(1):
  -- sound 427 + coins) instead of storing a heart; 1.7.9b let Keeper store it and the Lost could not take it.
  local trigger_gold = C.sym("_ZN15IsaacRepentance13Entity_Player21trigger_golden_heartsEi")
  local GOLD_BREAK = {[10] = true, [31] = true, [14] = true, [33] = true}
  local gold_broken = setmetatable({}, {__mode = "k"})
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_ROOM, -10000, function() gold_broken = {} end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_PRE_PICKUP_COLLISION, 10000, function(_, pk, coll)
    if state ~= "ready" or not trigger_gold or pk.Variant ~= PickupVariant.PICKUP_HEART or pk.SubType ~= HeartSubType.HEART_GOLDEN then return end
    local p = coll and coll:ToPlayer()
    if not p or not GOLD_BREAK[p:GetPlayerType()] or (pk.Price or 0) ~= 0 or pk:IsDead() or (pk.Wait or 0) > 0 then return end
    local pp, h = C.ptr(p), GetPtrHash(pk)
    if not pp or gold_broken[h] then return gold_broken[h] and true or nil end
    gold_broken[h] = true  -- Remove takes effect at the end of the frame; collision can repeat before
    pk:Remove()
    C.call(trigger_gold, "_pi", pp, 1)
    return true
  end)
  -- Tainted Bethany stores Rotten Hearts (REP+ 1.9.7.13, Entity_Pickup::handle_collision 0x6e9ae2): a rotten heart
  -- (5.10.12) touched by T.Bethany with fewer than 99 blood charges gives AddHearts(1) (= 1 blood charge for her)
  -- and is collected; 1.7.9b left it on the floor for her.
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_PRE_PICKUP_COLLISION, 10000, function(_, pk, coll)
    if state ~= "ready" or pk.Variant ~= PickupVariant.PICKUP_HEART or pk.SubType ~= HeartSubType.HEART_ROTTEN then return end
    local p = coll and coll:ToPlayer()
    if not p or p:GetPlayerType() ~= 36 or (pk.Price or 0) ~= 0 or (pk.Wait or 0) > 0 then return end
    local h = GetPtrHash(pk)
    if gold_broken[h] then return true end
    if p:GetBloodCharge() >= 99 then return end
    gold_broken[h] = true
    p:AddHearts(1)
    pk:GetSprite():Play("Collect", true)
    pk:PlayPickupSound()
    pk:Die()
    return true
  end)
  -- Tainted Cain in the Ultra Secret Room (REP+ 1.9.7.13): an item he touches there always breaks into at least one
  -- Cracked Key (78). REP+ does it inside SalvageCollectible (0x7d5c70); here the pickups spawned by the salvage
  -- (POST_PICKUP_INIT the same frame) are checked and one of them becomes a Cracked Key if none is.
  local salvage = nil
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_PRE_PICKUP_COLLISION, 10000, function(_, pk, coll)
    if state ~= "ready" or pk.Variant ~= PickupVariant.PICKUP_COLLECTIBLE or pk.SubType <= 0 then return end
    local p = coll and coll:ToPlayer()
    if not p or p:GetPlayerType() ~= 23 or Game():GetRoom():GetType() ~= RoomType.ROOM_ULTRASECRET then return end
    if (pk.Price or 0) ~= 0 or (pk.Wait or 0) > 0 then return end
    salvage = {frame = Game():GetFrameCount(), list = {}}
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PICKUP_INIT, 10000, function(_, pk)
    if salvage and salvage.frame == Game():GetFrameCount() and pk.Variant ~= PickupVariant.PICKUP_COLLECTIBLE then
      salvage.list[#salvage.list + 1] = pk
    end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_UPDATE, 10000, function()
    local sv = salvage
    if not sv or sv.frame == Game():GetFrameCount() then return end
    salvage = nil
    if #sv.list == 0 then return end
    for _, pk in ipairs(sv.list) do
      if pk.Variant == PickupVariant.PICKUP_TAROTCARD and pk.SubType == Card.CARD_CRACKED_KEY then return end
    end
    local pk = sv.list[1]
    if pk:Exists() then pk:Morph(5, PickupVariant.PICKUP_TAROTCARD, Card.CARD_CRACKED_KEY, true, true, true) end
  end)
  -- The Bean / Kidney Bean wisps (Book of Virtues) expire on room change (REP+ 1.9.7.11).
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_ROOM, -10000, function()
    if state ~= "ready" then return end
    for _, w in ipairs(Isaac.FindByType(EntityType.ENTITY_FAMILIAR, FamiliarVariant.WISP)) do
      if w.SubType == 111 or w.SubType == 421 then w:Remove() end
    end
  end)
  -- 1% boss drops (REP+ 1.9.7.11, Room 0x80e6c0 called from the clear award 0x7fd79a): in a boss room, a 1-in-100
  -- roll gives the boss's thematic item, by Room boss id (switch at 0x80e780, table 0x80ec84), as a choice pedestal
  -- tied to the normal boss item, or a trinket that can be taken freely. Only when still in its pool.
  local BOSS_DROP = {[1] = 86, [3] = 88, [4] = 384, [5] = 229, [13] = 10, [14] = 578, [15] = 87, [16] = 537,
    [17] = 318, [18] = 525, [23] = 646, [27] = 683, [28] = 384, [29] = 148, [30] = 155, [31] = 435, [32] = 612,
    [33] = 645, [34] = 211, [35] = 242, [36] = 266, [41] = 170, [42] = 89, [43] = 277, [44] = 36, [45] = 369,
    [46] = 375, [47] = 314, [48] = 610, [49] = 384, [50] = 259, [51] = 572, [52] = 658, [53] = 167, [56] = 384,
    [57] = 682, [58] = 576, [59] = 545, [60] = 503, [61] = 288, [64] = 680, [65] = 378, [66] = 549, [67] = 3,
    [68] = 225, [69] = 503, [72] = 473, [73] = 453, [74] = 358, [75] = 308, [76] = 270, [77] = 469, [78] = 676,
    [80] = 663, [81] = 216, [82] = 256, [83] = 210, [85] = 642, [86] = 729, [87] = 639, [91] = 264, [92] = 291,
    [93] = 616, [95] = 209, [96] = 641, [97] = 504, [102] = 677}
  local BOSS_TRINKET = {[9] = 115, [10] = 114, [11] = 113, [12] = 116, [38] = 117, [79] = 141, [101] = 183}
  local WORMS = {12, 26, 9, 11, 27, 10, 64, 65, 96, 144, 66}
  local WORM_BOSS = {[2] = true, [7] = true, [26] = true, [37] = true}
  local boss_drop = nil
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_PRE_SPAWN_CLEAN_AWARD, -10000, function(_, rng)
    local room = Game():GetRoom()
    if state ~= "ready" or room:GetType() ~= RoomType.ROOM_BOSS then return end
    local id = room:GetBossID()
    if not (BOSS_DROP[id] or BOSS_TRINKET[id] or WORM_BOSS[id]) then return end
    local r = RNG(); r:SetSeed(room:GetAwardSeed() ~ 0x5a17, 35)
    if r:RandomInt(100) ~= 0 and not rawget(_G, "REPPLUS_FORCE_BOSS_DROP") then return end  -- test hook
    local pool = Game():GetItemPool()
    local item, trinket = BOSS_DROP[id], BOSS_TRINKET[id]
    if WORM_BOSS[id] then trinket = WORMS[r:RandomInt(#WORMS) + 1] end
    if item and not pool:RemoveCollectible(item) then item = nil end
    if trinket and not pool:RemoveTrinket(trinket) then trinket = nil end
    if item or trinket then boss_drop = {frame = Game():GetFrameCount(), item = item, trinket = trinket} end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_UPDATE, 10000, function()
    local bd = boss_drop
    if not bd or Game():GetFrameCount() == bd.frame then return end
    boss_drop = nil
    local room = Game():GetRoom()
    local ped
    for _, e in ipairs(Isaac.FindByType(5, PickupVariant.PICKUP_COLLECTIBLE)) do
      if e.FrameCount <= 2 and e.SubType > 0 then ped = e:ToPickup() break end
    end
    local base = ped and ped.Position or room:GetCenterPos()
    if bd.item then
      local idx = ped and ped.OptionsPickupIndex or 0
      if ped and idx == 0 then idx = 0x5a17; ped.OptionsPickupIndex = idx end
      local pos = room:FindFreePickupSpawnPosition(base + Vector(80, 0), 0, true, false)
      local it = Isaac.Spawn(5, PickupVariant.PICKUP_COLLECTIBLE, bd.item, pos, Vector(0, 0), nil):ToPickup()
      it.OptionsPickupIndex = idx
    end
    if bd.trinket then
      local pos = room:FindFreePickupSpawnPosition(base + Vector(-80, 0), 0, true, false)
      Isaac.Spawn(5, PickupVariant.PICKUP_TRINKET, bd.trinket, pos, Vector(0, 0), nil)
    end
  end)
  -- Eden's Blessing: REP+ grants at most 2 items on the next run (1.9.7.9). 1.7.9b Entity_Player::InitPostLevelInitStats
  -- 0x2a15c0 gives min(Manager+0x488, 10) random items and then zeroes the counter; clamp the counter to 2 before.
  local function clamp_blessings()
    if not enabled() then return end
    local m = C.managerptr()
    if m and (peek(m, 0x488, "i32") or 0) > 2 then C.poke(m, 0x488, "i32", 2) end
  end
  for _, cb in ipairs({ModCallbacks.MC_POST_PLAYER_INIT, ModCallbacks.MC_POST_GAME_END, ModCallbacks.MC_PRE_GAME_EXIT}) do
    Isaac.AddPriorityCallback(mod, cb, -10000, clamp_blessings)
  end
  local trigger_enemy_death = C.sym("_ZN15IsaacRepentance13Entity_Player17TriggerEnemyDeathEPNS_10Entity_NPCE")
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_ENTITY_KILL, -10000, function(_, e)
    if state ~= "ready" or not trigger_enemy_death or not e:HasEntityFlags(EntityFlag.FLAG_ICE) then return end
    local npc = e:ToNPC()
    if not npc or npc:IsBoss() then return end
    local ep = C.ptr(e)
    if not ep then return end
    for i = 0, Game():GetNumPlayers() - 1 do
      local pp = C.ptr(Isaac.GetPlayer(i))
      if pp then C.call(trigger_enemy_death, "_pp", pp, ep) end
    end
  end)
  -- Jar of Wisps (685): REP+ AddCollectible (0x7621de) raises the jar's VarData (wisps on the next use) to at least
  -- 2, 1.7.9b (0x295d24) to at least 1, so the first use gives two wisps; the special-wisp roll is native
  -- (repplus_hooks.inc rp_jar_wisp)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PEFFECT_UPDATE, -10000, function(_, p)
    if state ~= "ready" then return end
    local pp = C.ptr(p)
    for slot = 0, 3 do
      if pp and C.peek(pp, 0x1964 + slot * 0x1c, "i32") == 685 and C.peek(pp, 0x197c + slot * 0x1c, "i32") < 2 then
        C.poke(pp, 0x197c + slot * 0x1c, "i32", 2)
      end
    end
  end)
  -- Sticky Bombs (367): GetBombFlags gives the bombs TEAR_EGG (bit 49) besides TEAR_STICKY (REP+ 0x780af9, the
  -- Switch the same, HW-checked), and REP+
  -- ApplyTearFlagEffects (0x67cfb8) handles TEAR_EGG for any hit, BombDamage included (the Switch only in
  -- Entity_Tear::handle_collision): every enemy the bomb damages gets the egg effect (rp_egg_hit)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_ENTITY_TAKE_DMG, 10000, function(_, e, amount, flags, src)
    if state ~= "ready" or flags & DamageFlag.DAMAGE_EXPLOSION == 0 or not e:IsVulnerableEnemy() then return end
    local b = src and src.Entity
    b = b and b.Type == 4 and b:ToBomb()
    if not b or b.Flags & TearFlags.TEAR_EGG == TearFlags.TEAR_NORMAL then return end
    local pl = b.SpawnerEntity and b.SpawnerEntity:ToPlayer()
    if not pl then return end
    rp_egg_hit(pl, e, b.Position, pl:GetCollectibleRNG(367))
  end)
  -- Delirious (510), REP+ (wiki, Delirious, "Added in Repentance+"): the friendly boss stays with the player from
  -- room to room until it is killed; a new use kills the previous boss and the friendly monsters it spawned; those
  -- monsters lose health steadily and turn gray down to 1 HP. REP+ also keeps item 510 in the active-item varData
  -- list (0x791563, with 286/263/348). The drain speed is not on the wiki: here 1/300 of max HP per update (~10 s).
  local deli, deli_pending = nil, -1
  local function deli_live()
    local b = deli and deli.ent
    if b and b:Exists() and not b:IsDead() then return b end
  end
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_USE_ITEM, -10000, function()
    if state ~= "ready" then return end
    local b = deli_live()
    if b then
      for _, m in ipairs(Isaac.GetRoomEntities()) do
        if m.SpawnerEntity and GetPtrHash(m.SpawnerEntity) == GetPtrHash(b) and m:HasEntityFlags(EntityFlag.FLAG_FRIENDLY) then m:Kill() end
      end
      b:Kill()
    end
    deli, deli_pending = nil, Game():GetFrameCount()
  end, 510)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_NPC_UPDATE, -10000, function(_, n)
    if state ~= "ready" or not n:HasEntityFlags(EntityFlag.FLAG_FRIENDLY) then return end
    if deli_pending >= 0 and n:IsBoss() and Game():GetFrameCount() - deli_pending <= 3 and not deli then
      deli = {ent = n, t = n.Type, v = n.Variant, s = n.SubType, flags = n:GetEntityFlags()}
      deli_pending = -1
    end
    local b = deli_live()
    if b and GetPtrHash(n) == GetPtrHash(b) then
      deli.t, deli.v, deli.s, deli.hp = n.Type, n.Variant, n.SubType, n.HitPoints
      return
    end
    local sp = n.SpawnerEntity
    if b and sp and GetPtrHash(sp) == GetPtrHash(b) then
      if n.HitPoints > 1 then n.HitPoints = math.max(1, n.HitPoints - n.MaxHitPoints / 300) end
      local g = 1 - n.HitPoints / math.max(n.MaxHitPoints, 1)
      n:SetColor(Color(1 - 0.5 * g, 1 - 0.5 * g, 1 - 0.5 * g, 1, 0.25 * g, 0.25 * g, 0.25 * g), 2, 0, false, false)
    end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NPC_DEATH, -10000, function(_, n)
    local b = deli and deli.ent
    if b and GetPtrHash(n) == GetPtrHash(b) then deli = nil end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_ROOM, -10000, function()
    if state ~= "ready" or not deli or not deli.hp then return end
    if deli_live() then return end
    local pl = Isaac.GetPlayer(0)
    local pos = Game():GetRoom():FindFreePickupSpawnPosition(pl.Position + Vector(0, 40), 0, true)
    local e = Isaac.Spawn(deli.t, deli.v, deli.s, pos, Vector.Zero, pl)
    e:AddEntityFlags(deli.flags | EntityFlag.FLAG_FRIENDLY)
    e.HitPoints = math.min(deli.hp, e.MaxHitPoints)
    deli.ent = e
  end)
  -- Rosary Bead (7), REP+ (wiki: "the deal is always an Angel Room"; the exe drops the 1.7.9b +50% angel chance
  -- at 0x7f9a80 and the HUD shows the whole deal chance as angel, 0x84c9c2). Where REP+ makes the room an angel
  -- room is not found yet; here a boss room clear with a Rosary Bead holder makes a not yet chosen deal room angel.
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_PRE_SPAWN_CLEAN_AWARD, -10000, function()
    if state ~= "ready" then return end
    local room, level = Game():GetRoom(), Game():GetLevel()
    if room:GetType() ~= RoomType.ROOM_BOSS then return end
    local has = false
    for i = 0, Game():GetNumPlayers() - 1 do if Isaac.GetPlayer(i):HasTrinket(7) then has = true end end
    if has and level:GetRoomByIdx(-1).Data == nil then level:InitializeDevilAngelRoom(true, false) end
  end)
  -- Mom's Toenail (16), REP+ (wiki: "During combat, dropping this trinket will cause Mom's foot to stomp on its
  -- location", golden "Adds 1 more foot stomp per stack"; the timed stomp is removed in repplus_hooks.inc).
  local toe_stomps = {}
  -- the dropped trinket's spawner is set after its init (SpawnEx), so this looks at its first update; "in
  -- combat" = a live enemy in the room (HW: an enemy spawned into a cleared room left IsClear true)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PICKUP_UPDATE, -10000, function(_, pk)
    if state ~= "ready" or pk.FrameCount ~= 1 or pk.SubType & 0x7fff ~= 16 then return end
    if Isaac.CountEnemies() == 0 then return end
    local pl = pk.SpawnerEntity and pk.SpawnerEntity:ToPlayer()
    if not pl then return end
    local n = pk.SubType & 0x8000 ~= 0 and 2 or 1
    if pl:HasCollectible(439) then n = n + 1 end  -- Mom's Box
    for i = 0, n - 1 do toe_stomps[#toe_stomps + 1] = {pos = pk.Position, at = Game():GetFrameCount() + i * 20} end
  end, 350)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_UPDATE, -10000, function()
    if #toe_stomps == 0 then return end
    local fr = Game():GetFrameCount()
    for i = #toe_stomps, 1, -1 do
      local t = toe_stomps[i]
      if fr >= t.at then
        Isaac.Spawn(1000, 29, 0, t.pos, Vector.Zero, nil)
        table.remove(toe_stomps, i)
      end
    end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_ROOM, -10000, function() toe_stomps = {} end)
  -- Left Hand (61) golden / Mom's Box, REP+ (wiki: "Red chests drop an additional pickup" and no enemy spiders;
  -- the exe's new multiplier checks are 0x6dbfcf, 0x6dc5e7 in the chest loot code). Here: when a red chest opens
  -- while a player's Left Hand multiplier is 2+, one more random pickup, and spiders (85) it spawns are removed.
  local lh_open = {}  -- GetPtrHash(chest) -> subtype last update
  local lh_spider_until, lh_pos = -1, nil
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PICKUP_UPDATE, -10000, function(_, pk)
    if state ~= "ready" then return end
    local h = GetPtrHash(pk)
    local prev = lh_open[h]
    lh_open[h] = pk.SubType
    if prev == nil or prev == 0 or pk.SubType ~= 0 then return end
    local m = 0
    for i = 0, Game():GetNumPlayers() - 1 do m = math.max(m, Isaac.GetPlayer(i):GetTrinketMultiplier(61)) end
    if m < 2 then return end
    Isaac.Spawn(5, 0, 0, Game():GetRoom():FindFreePickupSpawnPosition(pk.Position, 0, true), RandomVector() * 2, pk)
    lh_spider_until, lh_pos = Game():GetFrameCount() + 2, pk.Position
    for _, e in ipairs(Isaac.FindByType(85, -1, -1)) do
      if e.FrameCount <= 2 and e.Position:Distance(pk.Position) < 80 then e:Remove() end
    end
  end, 360)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NPC_INIT, -10000, function(_, n)
    if Game():GetFrameCount() <= lh_spider_until and lh_pos and n.Position:Distance(lh_pos) < 80 then n:Remove() end
  end, 85)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_ROOM, -10000, function() lh_open = {} end)
  -- Lil Portal (3.232), part of the REP+ rework (wiki): the portal opens after 1 + stage pickups, the 4 sizes are
  -- spread over that count, damage 2 x size. Switch 1.7.9b ai_chubby (0xb44f4, 0xb46e0): each eaten pickup adds
  -- 100 to Coins (+0xd8c), size = min(Coins / 100, 3), contact damage 2 + 2 x size (= 2 x size counted from 1),
  -- portal when Coins >= 301. Here Coins is rewritten each update from our own pickup count, so the engine shows
  -- the REP+ size and opens the portal at the REP+ count. The bedroom and hiding the familiar are below.
  local lp = {}  -- GetPtrHash(familiar) -> {n = pickups, set = Coins we wrote}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_FAMILIAR_UPDATE, -10000, function(_, f)
    if state ~= "ready" then return end
    local h = GetPtrHash(f)
    local t = lp[h]
    if not t then t = {n = 0, set = f.Coins}; lp[h] = t end
    if t.open then  -- waiting for the engine to open the portal (it then sets Coins to 0)
      if f.Coins < 301 then t.open, t.n, t.set = false, 0, f.Coins else f.Coins = 400 end
      return
    end
    if f.Coins < t.set then t.n, t.set = 0, f.Coins end
    local d = f.Coins - t.set
    if d >= 100 then t.n = t.n + d // 100 end
    local need = 1 + Game():GetLevel():GetStage()
    if t.n >= need then t.open = true; f.Coins = 400; return end
    t.set = math.min(3, t.n * 4 // need) * 100
    f.Coins = t.set
  end, 232)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_LEVEL, -10000, function() lp = {} end)
  -- Launch by double tap (REP+, wiki: "The portal will launch itself forward after double-tapping"): Switch
  -- 1.7.9b ai_chubby launches whenever its owner shoots (Familiar+0xd60 goes from >= 0 to -1, the flight state,
  -- with speed 10 in the fire direction, checked on the console). A launch is let through only within a few
  -- updates after the owner pressed the same fire direction twice in a row (second press within 10 updates);
  -- otherwise the flight is cancelled on the spot (+0xd60 back to its old value, velocity 0).
  -- in-room item info box (drawn by the My Stuff block below, which has the REP+ text data and font)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_RENDER, 0, function()
    if state == "ready" and _rp_item_info_render then _rp_item_info_render() end
  end)
  local LP_TAP_WINDOW, LP_ALLOW = 10, 6
  local taps = {}  -- GetPtrHash(player) -> {dir, last = dir held last update, at = update of the last press, ok = until}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PEFFECT_UPDATE, -10000, function(_, p)
    if state ~= "ready" or not p:HasCollectible(681, false) then return end
    local h, now = GetPtrHash(p), Game():GetFrameCount()
    local t = taps[h]
    if not t then t = {last = -1, dir = -1, at = -100, ok = -1}; taps[h] = t end
    local d = p:GetFireDirection()
    if d ~= -1 and d ~= t.last then  -- a new press
      if d == t.dir and now - t.at <= LP_TAP_WINDOW then t.ok = now + LP_ALLOW; t.at = -100
      else t.dir, t.at = d, now end
    end
    t.last = d
  end)
  local lp_fly = {}  -- GetPtrHash(familiar) -> +0xd60 before
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_FAMILIAR_UPDATE, -10002, function(_, f)
    if state ~= "ready" then return end
    local h = GetPtrHash(f)
    local v = C.peek(f, 0xd60, "i32") or 0
    local before = lp_fly[h]
    lp_fly[h] = v
    if v ~= -1 or not before or before < 0 then return end
    local p = f.Player
    local t = p and taps[GetPtrHash(p)]
    if t and Game():GetFrameCount() <= t.ok then t.ok = -1; return end
    C.poke(f, 0xd60, "i32", before)
    lp_fly[h] = before
    f.Velocity = Vector.Zero
  end, 232)
  -- Lil Portal bedroom (REP+ Room::Init 0x803756, off-grid room ROOM_LIL_PORTAL_IDX -20, backdrop 62). The portal
  -- the familiar opens leads there instead of to a random room, and the familiar is gone for the rest of the floor.
  -- Switch 1.7.9b has no room -20 and no backdrop 62: the debug room -3 gets the start room's layout, and the
  -- backdrop config slot 55 (reserved, unused) is given portal_bedroom.png with the REP+ backdrops.xml values
  -- (walls 1, wall variants 4, floors 1, floor variants 1; Switch config entries 0x148 apart at Backdrop+0x1e0,
  -- gfx std::string at +0, the four counts at +0x60) before Backdrop::Init(55, true).
  -- Portal effect 1000.161 (Switch Entity_Effect::Update 0x9c618..0xa5128): SubType >= 900 leads to room
  -- SubType - 1000; a negative room makes the engine only start the trapdoor animation of the player (collision
  -- 0, Entity_Player::AnimateTrapdoor), so the entry portal gets SubType 997 and the transition to -3 is started
  -- here when that happens. In the bedroom an exit portal 1000.161 with SubType 1000 + the previous room leads
  -- back, as in REP+. Contents on the first visit of the run, three xorshift(5, 9, 7) rolls of the room's
  -- spawn seed: top left chest (< 4 eternal 5.53, < 9 spiked 5.52, < 19 bomb 5.51, < 29 red 5.360, < 54 locked
  -- 5.60, else 5.50), bottom right (< 25 Dice Shard, < 50 Butter!, < 60 Soul of Eden, < 61 Moving Box; the last
  -- three when unlocked), bottom left a Lil Battery 5.90 when roll % 4 == 0.
  -- Room -3 is per level on Switch: its pickups are snapshotted and put back on later floors.
  local BED_IDX, BED_SUB, BED_BACKDROP = -3, 997, 55
  local bed = {prev = nil, go = false, hidden = {}, filled = false, level = 0, snap = nil, snap_level = -1}
  -- the room keeps its contents for the run (wiki): Switch room -3 is rebuilt each floor, so the pickups in it
  -- are noted while Isaac is there and put back when he comes in on a later floor (opened chests, empty
  -- pedestals and shop items are not kept)
  local function bed_snapshot()
    local t = {}
    for _, e in ipairs(Isaac.FindByType(5, -1, -1, false, false)) do
      local pk = e:ToPickup()
      local chest = e.Variant == 50 or e.Variant == 51 or e.Variant == 52 or e.Variant == 53 or e.Variant == 54
        or e.Variant == 55 or e.Variant == 56 or e.Variant == 57 or e.Variant == 58 or e.Variant == 60 or e.Variant == 360
      local keep = not (pk and pk.Price ~= 0) and not (e.Variant == 100 and e.SubType == 0) and not (chest and e.SubType == 0)
      if keep then t[#t + 1] = {e.Variant, e.SubType, e.Position.X, e.Position.Y} end
    end
    bed.snap, bed.snap_level = t, bed.level
  end
  local function bed_backdrop()
    local r = C.roomptr()
    local init = C.sym("_ZN15IsaacRepentance8Backdrop4InitENS0_9eBackdropEb")
    local new = C.sym("_Znwm")
    if not r or not init or not new then return end
    local b = C.offset(r, 0x1fe0)
    local e = C.offset(b, 0x1e0 + 0x148 * BED_BACKDROP)
    if not bed.gfx then
      local path = "gfx/backdrop/portal_bedroom.png"
      local m = C.call(new, "pi", 48)
      if not m then return end
      for i = 1, #path do C.poke(m, i - 1, "u8", path:byte(i)) end
      C.poke(m, #path, "u8", 0)
      bed.gfx = m
    end
    -- the old string of the reserved slot is left allocated (written once per bedroom entry, same values)
    C.poke(e, 0, "i64", 0x31)
    C.poke(e, 8, "i64", 31)
    C.poke(e, 16, "p", bed.gfx)
    C.poke(e, 0x60, "i32", 1); C.poke(e, 0x64, "i32", 4); C.poke(e, 0x68, "i32", 1); C.poke(e, 0x6c, "i32", 1)
    C.call(init, "_pib", b, BED_BACKDROP, true)
  end
  local function bed_roll(x)
    x = x ~ (x >> 5); x = (x ~ (x << 9)) & 0xffffffff; x = x ~ (x >> 7)
    return x
  end
  local function bed_avail(kind, id)
    local ok, v = pcall(function()
      local ic = Isaac.GetItemConfig()
      local it = kind == "t" and ic:GetTrinket(id) or kind == "c" and ic:GetCard(id) or ic:GetCollectible(id)
      return not it or it:IsAvailable()
    end)
    return not ok or v ~= false
  end
  local function bed_fill()
    local room, lv = Game():GetRoom(), Game():GetLevel()
    local d = lv:GetCurrentRoomDesc()
    local x = (d and d.SpawnSeed or Random()) & 0xffffffff
    if x == 0 then x = 1 end
    local function spawn(t, v, st, idx)
      local pos = room:FindFreePickupSpawnPosition(room:GetGridPosition(idx), 0, true)
      Isaac.Spawn(t, v, st, pos, Vector.Zero, nil)
    end
    x = bed_roll(x)
    local c = x % 100
    local chest = c < 4 and 53 or c < 9 and 52 or c < 19 and 51 or c < 29 and 360 or c < 54 and 60 or 50
    spawn(5, chest, chest == 50 and 1 or 0, 16)
    x = bed_roll(x)
    c = x % 100
    if c < 25 then spawn(5, 300, 49, 118)
    elseif c < 50 then if bed_avail("t", 122) then spawn(5, 350, 122, 118) end
    elseif c < 60 then if bed_avail("c", 90) then spawn(5, 300, 90, 118) end
    elseif c < 61 then if bed_avail("i", 523) then spawn(5, 100, 523, 118) end end
    x = bed_roll(x)
    if x % 4 == 0 then spawn(5, 90, 0, 106) end
  end
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_EFFECT_INIT, -10000, function(_, e)
    if state ~= "ready" or e.SubType < 900 or Game():GetLevel():GetCurrentRoomIndex() == BED_IDX then return end
    -- the familiar's portal: ai_chubby (0xb45b4..0xb469c) spawns it right after clearing Coins, while our
    -- counter above still waits for that (t.open)
    local f
    for _, x in ipairs(Isaac.FindByType(3, 232, -1, false, false)) do
      local t = lp[GetPtrHash(x)]
      if t and t.open then f = x end
    end
    if not f then return end
    e.SubType = BED_SUB
    bed.hidden[f.InitSeed] = true
  end, 161)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_EFFECT_UPDATE, -10000, function(_, e)
    if state ~= "ready" or e.SubType ~= BED_SUB or bed.go then return end
    for i = 0, Game():GetNumPlayers() - 1 do
      local p = Isaac.GetPlayer(i)
      if p and p.EntityCollisionClass == 0 and p.Position:Distance(e.Position) < 40 then
        local lv = Game():GetLevel()
        local d, start = lv:GetRoomByIdx(BED_IDX), lv:GetRoomByIdx(lv:GetStartingRoomIndex())
        if not (d and start and start.Data) then return end
        d.Data = start.Data
        bed.prev, bed.go = lv:GetCurrentRoomIndex(), true
        Game():StartRoomTransition(BED_IDX, Direction.NO_DIRECTION, 16, p)
        return
      end
    end
  end, 161)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_FAMILIAR_UPDATE, -10001, function(_, f)
    if state ~= "ready" or not bed.hidden[f.InitSeed] then return end
    f.Visible = false
    f.EntityCollisionClass = 0
    f.Coins = 0
  end, 232)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_PRE_FAMILIAR_COLLISION, -10000, function(_, f)
    if state == "ready" and bed.hidden[f.InitSeed] then return true end
  end, 232)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_LEVEL, -10000, function()
    bed.hidden, bed.go, bed.level = {}, false, bed.level + 1
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_UPDATE, -10000, function()
    if state ~= "ready" or not bed.filled or Game():GetLevel():GetCurrentRoomIndex() ~= BED_IDX then return end
    if Game():GetFrameCount() % 10 == 0 then bed_snapshot() end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_GAME_STARTED, -10000, function(_, cont)
    if not cont then bed.filled, bed.prev, bed.snap, bed.snap_level = false, nil, nil, -1 end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_ROOM, -10000, function()
    if state ~= "ready" then return end
    local lv = Game():GetLevel()
    if lv:GetCurrentRoomIndex() ~= BED_IDX or not bed.go then bed.go = false; return end
    bed.go = false
    pcall(bed_backdrop)
    for i = 0, Game():GetNumPlayers() - 1 do
      local p = Isaac.GetPlayer(i)
      if p and p.EntityCollisionClass == 0 then p.EntityCollisionClass = 4 end
    end
    local room = Game():GetRoom()
    -- no doors: the start room's layout brings its door slots along, REP+'s room has none
    for slot = 0, 7 do if room:GetDoor(slot) then room:RemoveDoor(slot) end end
    if bed.filled and bed.snap and bed.snap_level ~= bed.level then
      for _, e in ipairs(Isaac.FindByType(5, -1, -1, false, false)) do e:Remove() end
      for _, x in ipairs(bed.snap) do Isaac.Spawn(5, x[1], x[2], Vector(x[3], x[4]), Vector.Zero, nil) end
      bed.snap_level = bed.level
    end
    for _, e in ipairs(Isaac.FindByType(1000, 161, -1, false, false)) do
      if bed.prev and e.SubType ~= 1000 + bed.prev then e.SubType = 1000 + bed.prev end
    end
    if #Isaac.FindByType(1000, 161, -1, false, false) == 0 and bed.prev then
      Isaac.Spawn(1000, 161, 1000 + bed.prev, room:GetCenterPos() + Vector(0, -40), Vector.Zero, nil)
    end
    if not bed.filled then
      bed.filled = true
      local ok, err = pcall(bed_fill)
      if not ok then C.log("isaac-repplus: Lil Portal bedroom: " .. tostring(err)) end
      bed_snapshot()
    end
  end)
  -- Flat File (151) golden / Mom's Box, REP+ (wiki: "Bosses spike attacks, Grudge, and Ball and Chain have their
  -- spikes retracted"). REP+ NPC updates ask FirstTrinketOwner(151) and its multiplier > 1 (Grudge 0x4e7eee,
  -- Ball and Chain 0x4f60ec, Singe's ball 0x58dd64) and on a change swap spritesheet 0 to *_retracted.png
  -- (shipped from REP+ afterbirthp.a) and set CollisionDamage 0 (else Grudge / Ball and Chain 2.0, ball 1.0).
  -- Not done: the 0x5ce71d effect ("SummonWombDisabled" animation, not in the 1.7.9b anm2).
  local FF_SPIKES = {[877] = {nil, "gfx/monsters/repentance/grudge", 2.0},
                     [893] = {nil, "gfx/monsters/repentance/ball_and_chain", 2.0},
                     [915] = {1, "gfx/bosses/repentance/singe_ball", 1.0}}
  local ff_state = {}  -- GetPtrHash(npc) -> retracted
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_ROOM, -10000, function() ff_state = {} end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_NPC_UPDATE, -10000, function(_, n)
    if state ~= "ready" then return end
    local r = FF_SPIKES[n.Type]
    if not r or (r[1] and n.Variant ~= r[1]) then return end
    local m = 0
    for i = 0, Game():GetNumPlayers() - 1 do m = math.max(m, Isaac.GetPlayer(i):GetTrinketMultiplier(151)) end
    local on = m > 1
    local h = GetPtrHash(n)
    if (ff_state[h] or false) == on then
      if on then n.CollisionDamage = 0 end
      return
    end
    ff_state[h] = on
    local sp = n:GetSprite()
    sp:ReplaceSpritesheet(0, r[2] .. (on and "_retracted.png" or ".png"))
    sp:LoadGraphics()
    n.CollisionDamage = on and 0 or r[3]
  end)
  -- Linger Bean cloud (EFFECT 105), REP+ Entity_Effect::Update case 105 (0x6010ac..0x6017c7): after PoofStart the
  -- cloud counts its PoofLoop frames (+0x450); t = n / 300 scales the sprite lerp(1.0, 2.1, t) (the damage radius
  -- is 50 * SpriteScale.X, as in 1.7.9b), the damage lerp(1.7, 0.58, t) x the player's damage, and at 300 frames
  -- it ends with PoofEnd (+0x173). 1.7.9b keeps scale 1 and full damage until the effect's 1000-frame life ends
  -- (on the console the cloud lasted about 900 frames).
  local clouds, cloud_hit = {}, false
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_EFFECT_UPDATE, 10000, function(_, e)
    if state ~= "ready" then return end
    local s, h = e:GetSprite(), GetPtrHash(e)
    local c = clouds[h]
    if not c then
      if not s:IsPlaying("PoofLoop") then return end
      c = {n = 0}; clouds[h] = c
    end
    if c.ending then
      if s:IsFinished("PoofEnd") then clouds[h] = nil; e:Remove() end
      return
    end
    c.n = c.n + 1
    local t = math.min(c.n / 300, 1)
    local k = 1.0 + (2.1 - 1.0) * t
    e.SpriteScale = Vector(k, k)
    c.mult = 1.7 + (0.58 - 1.7) * t
    if c.n >= 300 then c.ending = true; s:Play("PoofEnd", true) end
  end, 105)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_ROOM, 0, function() clouds = {} end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_ENTITY_TAKE_DMG, -9000, function(_, e, amount, flags, src, cd)
    if state ~= "ready" or cloud_hit then return end
    local l = src and src.Entity
    if not l or l.Type ~= 1000 or l.Variant ~= 105 then return end
    local c = clouds[GetPtrHash(l)]
    if not c then return end
    if c.ending then return false end
    cloud_hit = true
    e:TakeDamage(amount * c.mult, flags, src, cd)
    cloud_hit = false
    return false
  end)
  -- Spawn Pentagram (EFFECT 203, content/entities2.xml of the mod), REP+ 0x6fef00: challenge / Boss Rush waves
  -- (Ambush::SpawnWave 0x833ff0) and Greed waves (0x8339b0) put a pentagram where the enemy will be and spawn it
  -- when its "Summon" animation (32 frames) ends (Entity_Effect::Update case 203, 0x5d44e0); the pentagram is
  -- scaled by the enemy's collision radius / 16, kept within 0.8 .. 2.0. Here the wave enemy spawns as in 1.7.9b
  -- but stays hidden, frozen and untouchable under the pentagram until the animation ends, so the wave counts
  -- (the room is not clear) work as before.
  local pent_var, pending, pending_npc, pent_snd_frame = nil, {}, {}, -1
  local function wave_spawn(n)
    if n.SpawnerType ~= 0 or n.Type < 10 or n.Type >= 1000 or n:HasEntityFlags(EntityFlag.FLAG_FRIENDLY) then return false end
    local g, room = Game(), Game():GetRoom()
    if room:GetFrameCount() < 1 then return false end
    if room:IsAmbushActive() then return true end
    if g:IsGreedMode() then
      local lv = g:GetLevel()
      return lv:GetCurrentRoomIndex() == lv:GetStartingRoomIndex()
    end
    return false
  end
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NPC_INIT, 10000, function(_, n)
    if state ~= "ready" or not wave_spawn(n) then return end
    pent_var = pent_var or Isaac.GetEntityVariantByName("Spawn Pentagram")
    if not pent_var or pent_var < 0 then return end
    local e = Isaac.Spawn(1000, pent_var, 0, n.Position, Vector.Zero, nil):ToEffect()
    local k = math.max(0.8, math.min(n.Size / 16, 2.0))
    e.SpriteScale = Vector(k, k)
    e:GetSprite():Play("Summon", true)
    pending[GetPtrHash(e)] = {e = e, n = n, cls = n.EntityCollisionClass, pos = n.Position}
    pending_npc[GetPtrHash(n)] = true
    -- the wave plays SOUND_SUMMON_PENTA (871) once after its spawns (0x833eeb, 0x4b2181)
    local f = Game():GetFrameCount()
    if pent_snd_frame ~= f then pent_snd_frame = f; rp_snd("REP+ Summon Penta") end
    n.Visible = false
    n.EntityCollisionClass = EntityCollisionClass.ENTCOLL_NONE
    n:AddEntityFlags(EntityFlag.FLAG_FREEZE | EntityFlag.FLAG_NO_TARGET)
  end)
  local function reveal(w)
    local n = w.n
    pending_npc[GetPtrHash(n)] = nil
    if n:Exists() then
      n.Visible = true
      n.EntityCollisionClass = w.cls
      n:ClearEntityFlags(EntityFlag.FLAG_FREEZE | EntityFlag.FLAG_NO_TARGET)
    end
  end
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_EFFECT_UPDATE, 10000, function(_, e)
    local h = GetPtrHash(e)
    local w = pending[h]
    if not w then return end
    if not w.n:Exists() then pending[h] = nil; pending_npc[GetPtrHash(w.n)] = nil; e:Remove(); return end
    w.n.Visible = false
    w.n.Position, w.n.Velocity = w.pos, Vector.Zero
    if e:GetSprite():IsFinished("Summon") then pending[h] = nil; reveal(w); e:Remove() end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_ENTITY_TAKE_DMG, -9500, function(_, e)
    if pending_npc[GetPtrHash(e)] then return false end
  end)
  -- the enemy is not there yet: its AI does not run under the pentagram (FLAG_FREEZE alone left some enemies
  -- shooting and spawning while hidden, HW 0.99.127)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_PRE_NPC_UPDATE, -10000, function(_, n)
    if pending_npc[GetPtrHash(n)] then return true end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_ROOM, 0, function()
    for _, w in pairs(pending) do reveal(w) end
    pending, pending_npc = {}, {}
  end)
  -- 1% thematic boss drop (REP+ 1.9.7.11), Room::SpawnClearAward 0x7fd79a -> 0x80e6c0: in the boss item branch a
  -- 1 in 100 roll (the award RNG) picks a collectible or a trinket by the room's boss id; a collectible must be
  -- available in the item pool, a trinket still in the trinket pool (it is taken out). The collectible is one more
  -- pedestal that shares the boss item's option index (take one, the other goes), the trinket (5.350) lies
  -- next to the boss item. A room whose descriptor has FLAG_ROTGUT_CLEARED (1 << 16) gives Yuck Heart (639).
  -- The worm bosses (Larry Jr. 2, Pin 7, The Hollow 26, Scolex 37) roll one of 11 worm trinkets.
  local BOSS_ITEM = {[1] = 86, [3] = 88, [4] = 384, [5] = 229, [13] = 10, [14] = 578, [15] = 87, [16] = 537,
    [17] = 318, [18] = 525, [23] = 646, [27] = 683, [28] = 384, [29] = 148, [30] = 155, [31] = 435, [32] = 612,
    [33] = 645, [34] = 211, [35] = 242, [36] = 266, [41] = 170, [42] = 89, [43] = 277, [44] = 36, [45] = 369,
    [46] = 375, [47] = 314, [48] = 610, [49] = 384, [50] = 259, [51] = 572, [52] = 658, [53] = 167, [56] = 384,
    [57] = 682, [58] = 576, [59] = 545, [60] = 503, [61] = 288, [64] = 680, [65] = 378, [66] = 549, [67] = 3,
    [68] = 225, [69] = 503, [72] = 473, [73] = 453, [74] = 358, [75] = 308, [76] = 270, [77] = 469, [78] = 676,
    [80] = 663, [81] = 216, [82] = 256, [83] = 210, [85] = 642, [86] = 729, [87] = 639, [91] = 264, [92] = 291,
    [93] = 616, [95] = 209, [96] = 641, [97] = 504, [102] = 677}
  local BOSS_TRINKET = {[9] = 115, [10] = 114, [11] = 113, [12] = 116, [38] = 117, [79] = 141, [101] = 183}
  local BOSS_WORMS = {12, 26, 9, 11, 27, 10, 64, 65, 96, 144, 66}
  local WORM_BOSS = {[2] = true, [7] = true, [26] = true, [37] = true}
  local boss_award  -- {frame, first boss item pedestal}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_PRE_SPAWN_CLEAN_AWARD, 10000, function()
    boss_award = nil
    if state ~= "ready" then return end
    local g = Game()
    if g:GetRoom():GetType() ~= RoomType.ROOM_BOSS or g:IsGreedMode() or Isaac.GetChallenge() == 43 then return end
    boss_award = {frame = g:GetFrameCount()}
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PICKUP_INIT, 10000, function(_, e)
    if boss_award and not boss_award.item and Game():GetFrameCount() == boss_award.frame then boss_award.item = e end
  end, 100)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_UPDATE, 10000, function()
    local a = boss_award
    if not a then return end
    boss_award = nil
    local it = a.item
    if not it or not it:Exists() then return end
    local g = Game()
    local room, lv = g:GetRoom(), g:GetLevel()
    local rng = RNG()
    rng:SetSeed(room:GetAwardSeed() ~ 0x1d872b41, 35)
    if rng:RandomInt(100) ~= 0 and not rawget(_G, "REPPLUS_TEST_BOSS_DROP") then return end
    local id = room:GetBossID()
    local item, trinket = BOSS_ITEM[id], BOSS_TRINKET[id]
    if lv:GetCurrentRoomDesc().Flags & (1 << 16) ~= 0 then item, trinket = 639, nil
    elseif WORM_BOSS[id] then trinket = BOSS_WORMS[rng:RandomInt(11) + 1] end
    local ic, pool = Isaac.GetItemConfig(), g:GetItemPool()
    local pos = room:FindFreePickupSpawnPosition(it.Position + Vector(80, 0), 0, true)
    if item then
      local c = ic:GetCollectible(item)
      if c and c:IsAvailable() then
        pool:RemoveCollectible(item)
        local e = Isaac.Spawn(5, 100, item, pos, Vector(0, 0), nil):ToPickup()
        local p = it:ToPickup()
        if p.OptionsPickupIndex == 0 then p.OptionsPickupIndex = 0x7b05 + (g:GetFrameCount() & 0xfff) end
        e.OptionsPickupIndex = p.OptionsPickupIndex
        C.log("isaac-repplus: 1% boss drop, boss " .. id .. " item " .. item)
      end
    elseif trinket then
      local t = ic:GetTrinket(trinket)
      if t and t:IsAvailable() then
        pool:RemoveTrinket(trinket)
        Isaac.Spawn(5, 350, trinket, pos, Vector(0, 0), nil)
        C.log("isaac-repplus: 1% boss drop, boss " .. id .. " trinket " .. trinket)
      end
    end
  end)
  -- Cube Baby (3.239), REP+ 0x96a743: a kick (rest -> speed about 20 in one frame, checked on the console)
  -- plays SOUND_CUBE_BABY_KICK (852).
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_FAMILIAR_UPDATE, 10000, function(_, f) chubby(f, "Cube Baby Kick", 8) end, 239)
  -- Glitter Bombs, REP+ 0x781bb9: when a bomb gets its flags and they include TEAR_GLITTER_BOMB,
  -- SOUND_GLITTER_FUSE (867). Played once per bomb on its first update (flags are final by then).
  local glit_done = {}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_BOMB_UPDATE, 10000, function(_, b)
    if state ~= "ready" then return end
    local h = GetPtrHash(b)
    if glit_done[h] then return end
    glit_done[h] = true
    if b.FrameCount <= 2 and b:HasTearFlags(TearFlags.TEAR_GLITTER_BOMB) then rp_snd("Glitter Bomb Fuse") end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_ROOM, 10000, function() glit_done = {} end)
  -- Lil Haunt (3.63), REP+ 0x61bbc7..0x61bc36: when its target search finds a target and it had none,
  -- SOUND_LIL_HAUNT_CHASE (859).
  local haunt_had = {}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_FAMILIAR_UPDATE, 10000, function(_, f)
    if state ~= "ready" then return end
    local h = GetPtrHash(f)
    local has = f.Target ~= nil
    if has and haunt_had[h] == false then rp_snd("Lil Haunt Chase") end
    haunt_had[h] = has
  end, 63)
  -- Dark Bum (3.64), REP+ 0x61b3e4: each payout (pickup or blue spiders) plays SOUND_DARK_BUM_PAYOUT (853).
  -- The payout's spawns carry the Dark Bum as spawner; one sound a frame.
  local dark_frame = -1
  local function dark_pay(e)
    if state ~= "ready" or e.SpawnerType ~= 3 or e.SpawnerVariant ~= 64 then return end
    local fr = Game():GetFrameCount()
    if fr ~= dark_frame then dark_frame = fr rp_snd("Dark Bum Payout") end
  end
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PICKUP_INIT, 10000, function(_, e) dark_pay(e) end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_FAMILIAR_INIT, 10000, function(_, e) dark_pay(e) end, 73)
  local fx_end = {[58] = "Book of Shadows End", [625] = "Mega Mush Shrink"}
  local fx_had = {}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PEFFECT_UPDATE, 10000, function(_, p)
    if state ~= "ready" then return end
    local h = GetPtrHash(p)
    local had = fx_had[h] or {}
    fx_had[h] = had
    local fx = p:GetEffects()
    for id, snd in pairs(fx_end) do
      local on = fx:HasCollectibleEffect(id)
      if had[id] and not on then rp_snd(snd) end
      had[id] = on
    end
  end)
  -- Devil deals paid with spikes (REP+ 10). Room::GetShopItemPrice 0x815b45: a devil-priced collectible with the
  -- new item tag `devilsacrifice` (bit 33; 126 157 186 225 569 672 692 695 704) gets price -10 instead of hearts
  -- (not with A Pound of Flesh / coin prices). The pickup stands on a new spike grid (type 28, anim PaymentSpike);
  -- player spike damage there (0x70b24c) opens the nearest -10 item: on the first hit with 50% (RandomInt(2) == 0)
  -- or Judas' Tongue x3, always on the next. Opening (Entity_Pickup::SetPrice 0x6e2f12) sets price 0 (free),
  -- plays sound 80 and spawns a poof (1000.15). Switch has no grid 28 and no price -10: the item gets price 0
  -- with AutoUpdatePrice off, ordinary spikes (grid 8) under it, and its pickup is refused until it is opened.
  local SACRIFICE = {[126] = true, [157] = true, [186] = true, [225] = true, [569] = true, [672] = true,
    [692] = true, [695] = true, [704] = true}
  local HEART_PRICE = {[-1] = true, [-2] = true, [-3] = true, [-4] = true, [-7] = true, [-8] = true, [-9] = true}
  local sac = {}  -- "room:grid" -> {hits, open}; this floor only
  local function sac_key(pk)
    return Game():GetLevel():GetCurrentRoomIndex() .. ":" .. Game():GetRoom():GetGridIndex(pk.Position)
  end
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_LEVEL, -10000, function() sac = {} end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PICKUP_UPDATE, -10000, function(_, pk)
    if state ~= "ready" then return end
    local key = sac_key(pk)
    local s = sac[key]
    if s and s.open then
      if pk.Price ~= 0 and SACRIFICE[pk.SubType] then pk.AutoUpdatePrice = false; pk.Price = 0 end
      return
    end
    if not SACRIFICE[pk.SubType] then
      if s then  -- rerolled away: the engine prices it again
        sac[key] = nil
        pk.AutoUpdatePrice = true
        local r = Game():GetRoom()
        local gr = r:GetGridEntity(s.idx)
        if gr and gr:GetType() == 8 then r:RemoveGridEntity(s.idx, 0, false) end
      end
      return
    end
    if not s then
      if not HEART_PRICE[pk.Price] then return end
      s = {hits = 0, idx = Game():GetRoom():GetGridIndex(pk.Position)}
      sac[key] = s
    end
    s.pk = GetPtrHash(pk)
    if pk.Price ~= 0 or pk.AutoUpdatePrice then pk.AutoUpdatePrice = false; pk.Price = 0 end
    local r = Game():GetRoom()
    if s.idx >= 0 and not r:GetGridEntity(s.idx) then r:SpawnGridEntity(s.idx, 8, 0, pk.InitSeed, 0) end
  end, 100)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_PRE_PICKUP_COLLISION, -10000, function(_, pk)
    if state ~= "ready" then return end
    local s = sac[sac_key(pk)]
    if s and not s.open then return true end
  end, 100)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_ENTITY_TAKE_DMG, -10000, function(_, e, _, flags)
    if state ~= "ready" or flags & DamageFlag.DAMAGE_SPIKES == 0 then return end
    local p = e:ToPlayer()
    if not p then return end
    local r = Game():GetRoom()
    local gi = r:GetGridIndex(p.Position)
    local prefix = Game():GetLevel():GetCurrentRoomIndex() .. ":"
    local hit
    for k, s in pairs(sac) do
      if not s.open and s.idx == gi and k:sub(1, #prefix) == prefix then hit = s end
    end
    if not hit then return end
    -- the nearest locked item to this spike
    local gp, best, bd = r:GetGridPosition(gi), nil, math.huge
    for _, ent in ipairs(Isaac.FindByType(5, 100)) do
      local pk = ent:ToPickup()
      local s = pk and sac[sac_key(pk)]
      if s and not s.open then
        local d = pk.Position:DistanceSquared(gp)
        if d < bd then best, bd = pk, d end
      end
    end
    if not best then return end
    hit.hits = hit.hits + 1
    local tongue = 0
    for i = 0, Game():GetNumPlayers() - 1 do tongue = tongue + Isaac.GetPlayer(i):GetTrinketMultiplier(56) end
    if hit.hits > 1 or best:GetDropRNG():RandomInt(2) == 0 or tongue >= 3 then
      local s = sac[sac_key(best)]
      s.open = true
      best.AutoUpdatePrice = false
      best.Price = 0
      SFXManager():Play(80)
      Isaac.Spawn(1000, 15, 0, best.Position, Vector.Zero, nil)
      if s.idx >= 0 then r:RemoveGridEntity(s.idx, 0, false) end
    end
  end, 1)
  -- Cursed Penny (172), REP+ (rewritten): the coin pickup (Entity_Pickup::handle_collision 0x6eb08d) keeps the
  -- coin subtype (Player+0x13b8, not with Black Candle) instead of the 1.7.9b teleport flag, and the player update
  -- (0x784a3e) teleports by subtype. Chance bonus b: golden trinket x2 +25, x3 +50; rolls are the trinket RNG.
  --   penny: < 50+b an uncleared unvisited normal room, else any room of types 1 2 4 5 6 7 8 9 11 12 13 20 21 22 24 18
  --   nickel: < 50+b types 2 4 9 11 12 24, else 1 5 6 7 8 13 20 21 22 18
  --   dime: < 25+b the deal room (50% angel / devil, Level::InitializeDevilAngelRoom, index -1), else
  --         types 1 2 4..13 20 21 24 18 plus the I AM ERROR room (-2) and the black market (-6)
  --   double: < 50+b types 20 21 18, else 1 2 4..9 11 12 13 24 + black market;  lucky: < 50+b 7 8, else
  --         1 2 4..9 11 12 13 20 21 24 18 + black market;  golden: < 10+b the I AM ERROR room, else
  --         1 2 4..9 11 12 13 24 20 21 18 + black market;  sticky nickel: 50% curse room (10), else boss (5)
  -- One room per grid index of the current dimension; then a random one of the list, LeaveDoor -1, teleport
  -- animation. No room (or index 0): the 1.7.9b teleport (Teleport!, Teleport 2.0 with Broken Remote > 1).
  -- Switch: the 1.7.9b teleport is UseActiveItem 44/419 from the player update, so it is cancelled in
  -- MC_PRE_USE_ITEM right after a cursed coin pickup and replaced. Not done: sticky nickel making a curse room
  -- when the floor has none (REP+ 0x748810).
  local PENNY_ALL = {1, 2, 4, 5, 6, 7, 8, 9, 11, 12, 13, 20, 21, 22, 24, 18}
  local penny_coin = {}  -- GetPtrHash(player) -> {sub, frame}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_PRE_PICKUP_COLLISION, -10000, function(_, pk, other)
    if state ~= "ready" then return end
    local p = other and other:ToPlayer()
    if p and p:HasTrinket(172) and not p:HasCollectible(260) then
      penny_coin[GetPtrHash(p)] = {sub = pk.SubType, frame = Game():GetFrameCount()}
    end
  end, 20)
  local function penny_target(p, sub)
    local rng = p:GetTrinketRNG(172)
    local m = p:GetTrinketMultiplier(172)
    local b = (m == 2 and 25) or (m >= 3 and 50) or 0
    local types, list, fresh_only = {}, {}, false
    local function t(...) for _, v in ipairs({...}) do types[v] = true end end
    if sub == 1 then
      if rng:RandomInt(100) < 50 + b then t(1); fresh_only = true else t(table.unpack(PENNY_ALL)) end
    elseif sub == 2 then
      if rng:RandomInt(100) < 50 + b then t(2, 4, 9, 11, 12, 24) else t(1, 5, 6, 7, 8, 13, 20, 21, 22, 18) end
    elseif sub == 3 then
      if rng:RandomInt(100) < 25 + b then
        if rng:RandomInt(100) < 50 then Game():GetLevel():InitializeDevilAngelRoom(true, false)
        else Game():GetLevel():InitializeDevilAngelRoom(false, true) end
        return -1
      end
      t(1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 20, 21, 24, 18); list = {-2, -6}
    elseif sub == 5 then
      if rng:RandomInt(100) < 50 + b then t(7, 8) else t(1, 2, 4, 5, 6, 7, 8, 9, 11, 12, 13, 20, 21, 24, 18); list = {-6} end
    elseif sub == 4 then
      if rng:RandomInt(100) < 50 + b then t(20, 21, 18) else t(1, 2, 4, 5, 6, 7, 8, 9, 11, 12, 13, 24); list = {-6} end
    elseif sub == 7 then
      if rng:RandomInt(100) < 10 + b then return -2 end
      t(1, 2, 4, 5, 6, 7, 8, 9, 11, 12, 13, 24, 20, 21, 18); list = {-6}
    elseif sub == 6 then
      t(rng:RandomInt(100) < 50 and 10 or 5)
    else
      return nil
    end
    local level = Game():GetLevel()
    local rooms, seen = level:GetRooms(), {}
    for i = 0, rooms.Size - 1 do
      local d = rooms:Get(i)
      local idx = d and d.SafeGridIndex
      if idx and idx >= 0 and not seen[idx] and d.Data and types[d.Data.Type] then
        seen[idx] = true
        if not fresh_only or (not d.Clear and d.VisitedCount == 0) then list[#list + 1] = idx end
      end
    end
    if #list == 0 then return nil end
    local idx = list[rng:RandomInt(#list) + 1]
    if idx == 0 then return nil end
    return idx
  end
  local function penny_use(_, _, _, p)
    if state ~= "ready" or not p then return end
    local key = GetPtrHash(p)
    local c = penny_coin[key]
    if not c then return end
    penny_coin[key] = nil
    if Game():GetFrameCount() - c.frame > 2 then return end
    local idx = penny_target(p, c.sub)
    if not idx then return end
    Game():GetLevel().LeaveDoor = -1
    Game():StartRoomTransition(idx, -1, 3, p)
    return true
  end
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_PRE_USE_ITEM, -10000, penny_use, 44)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_PRE_USE_ITEM, -10000, penny_use, 419)
  -- Locusts of Wrath/Pestilence/Famine/Death (113-116) with golden trinkets, REP+ 0x7c8bb0 (from TriggerNewRoom
  -- 0x7a7ecc, room not clear): count = n * multiplier of the locust trinket, n = 2 with Fish Tail (94), else 1;
  -- golden Fish Tail: 1 in 5 (Random() % 5 == 0) n = 3 (x2) or 4 (x3). Switch TriggerNewRoom (0x2f35d4) spawns n
  -- with n = 2 / 1 only, so the difference is spawned here (3.43, subtype 1..4, no appear flag). Not done:
  -- the REP+ calls on new waves (0x817446, 0x833f70, 0x834680).
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_NEW_ROOM, -10000, function()
    if state ~= "ready" then return end
    local room = Game():GetRoom()
    if room:IsClear() then return end
    for i = 0, Game():GetNumPlayers() - 1 do
      local p = Isaac.GetPlayer(i)
      for t = 113, 116 do
        local m = p:GetTrinketMultiplier(t)
        if m > 0 then
          local fish = p:GetTrinketMultiplier(94)
          local n = fish > 0 and 2 or 1
          if (fish == 2 or fish >= 3) and Random() % 5 == 0 then n = fish == 2 and 3 or 4 end
          local extra = n * m - (fish > 0 and 2 or 1)
          for _ = 1, extra do
            local f = Isaac.Spawn(3, 43, t - 112, p.Position, Vector.Zero, p)
            f:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
          end
        end
      end
      -- Locust of Conquest (117), REP+ 0x7c8ff1..0x7c9136: count = m + Random(m == 2 and 5 or 4) * n, m = its
      -- multiplier, n = 1 (no Fish Tail), 2 (Fish Tail), golden Fish Tail 1 in 5: 3 (x2) / 4 (x3). Switch
      -- TriggerNewRoom (0x2f38b4) spawns (Random(4) + 1) * (Fish Tail ? 2 : 1); the room's new ones are replaced
      -- when the counts differ (same for multiplier 1 without Fish Tail)
      local m = p:GetTrinketMultiplier(117)
      local fish = p:GetTrinketMultiplier(94)
      if m > 0 and (m > 1 or fish > 0) then
        local old = {}
        for _, e in ipairs(Isaac.FindByType(3, 43, 5)) do
          local f = e:ToFamiliar()
          if f and f.FrameCount == 0 and f.Player and GetPtrHash(f.Player) == GetPtrHash(p) then old[#old + 1] = e end
        end
        if #old > 0 then
          local n = fish > 0 and 2 or 1
          if fish >= 2 and Random() % 5 == 0 then n = fish == 2 and 3 or 4 end
          local want = m + p:GetTrinketRNG(117):RandomInt(m == 2 and 5 or 4) * n
          for i = want + 1, #old do old[i]:Remove() end
          for _ = #old + 1, want do
            local f = Isaac.Spawn(3, 43, 5, p.Position, Vector.Zero, p)
            f:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
          end
        end
      end
    end
  end)
  -- Equality (103), REP+ Entity_Player::Update 0x7834d2: the tears bonus (repplus_hooks.inc) is re-evaluated
  -- whenever "holds it with coins == bombs == keys" changes.
  local eq_state = {}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PEFFECT_UPDATE, -10000, function(_, p)
    if state ~= "ready" then return end
    local c = p:GetNumCoins()
    local on = p:GetTrinketMultiplier(103) > 0 and c == p:GetNumBombs() and c == p:GetNumKeys()
    local key = GetPtrHash(p)
    if (eq_state[key] or false) ~= on then
      eq_state[key] = on
      p:AddCacheFlags(CacheFlag.CACHE_FIREDELAY)
      p:EvaluateItems()
    end
    local worm = p:GetTrinketMultiplier(66)  -- Lazy Worm damage (below): its cache flag is shot speed only
    if (eq_state[key .. "w"] or 0) ~= worm then
      eq_state[key .. "w"] = worm
      p:AddCacheFlags(CacheFlag.CACHE_DAMAGE)
      p:EvaluateItems()
    end
  end)
  -- Lazy Worm (66), REP+ Entity_Player::EvaluateItems 0x76747b: +0.5 damage per multiplier, added after the
  -- damage multipliers and the EVALUATE_CACHE callbacks (0x864230), so this runs last.
  -- the Found Soul's damage is evaluated when it spawns, before its Parent (the owner) is set: evaluate it
  -- again once the owner is known and whenever his Found Soul multiplier changes
  local soul_mult = {}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_PEFFECT_UPDATE, -10000, function(_, p)
    if state ~= "ready" or p.Variant ~= 1 or p:GetBabySkin() ~= 59 then return end
    local o = p.Parent and p.Parent:ToPlayer()
    local f = o and o:GetTrinketMultiplier(180) or -1
    local h = GetPtrHash(p)
    if soul_mult[h] ~= f then
      soul_mult[h] = f
      p:AddCacheFlags(CacheFlag.CACHE_DAMAGE)
      p:EvaluateItems()
    end
  end)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_EVALUATE_CACHE, 10000, function(_, p, flag)
    if state ~= "ready" or flag ~= CacheFlag.CACHE_DAMAGE then return end
    local m = p:GetTrinketMultiplier(66)
    if m > 0 then p.Damage = p.Damage + 0.5 * m end
    -- Found Soul (180), REP+ 0x767524: the soul (player variant 1, baby skin 59) does 0.75x / 1x damage
    -- instead of 0.5x when its owner's multiplier is 2 / 3+
    if p.Variant == 1 and p:GetBabySkin() == 59 then
      local o = (p.Parent and p.Parent:ToPlayer()) or (p.SpawnerEntity and p.SpawnerEntity:ToPlayer())
      local f = o and o:GetTrinketMultiplier(180) or 0
      if f == 2 then p.Damage = p.Damage * 1.5 elseif f >= 3 then p.Damage = p.Damage * 2 end
    end
  end)
  -- Bob's Bladder (71), REP+ bomb explosion 0x6a438f: the bomb's green creep (1000.46, spawner = the player)
  -- is 1.5x bigger with multiplier 2; 2x bigger and lasts 1.5x longer with 3+.
  local bladder = {}
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_EFFECT_INIT, -10000, function(_, e)
    if state ~= "ready" then return end
    local p = e.SpawnerEntity and e.SpawnerEntity:ToPlayer()
    if not p then return end
    local m = p:GetTrinketMultiplier(71)
    if m < 2 then return end
    for _, b in ipairs(Isaac.FindByType(4)) do
      if b.Position:DistanceSquared(e.Position) < 4 then bladder[GetPtrHash(e)] = m; return end
    end
  end, 46)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_EFFECT_UPDATE, -10000, function(_, e)
    local key = GetPtrHash(e)
    local m = bladder[key]
    if not m then return end
    bladder[key] = nil
    if m >= 3 then
      e.SpriteScale = e.SpriteScale * 2
      e.Timeout = math.floor(e.Timeout * 1.5); e.LifeSpan = e.Timeout
    else
      e.SpriteScale = e.SpriteScale * 1.5
    end
  end, 46)
  -- Lemon Mishap creep (1000.32), REP+ Entity_Effect::Update 0x5f13ad: once grown (Timeout <= LifeSpan - 15),
  -- creep spawned by a player gets Timeout + 1 every frame, so it stays until the room is left
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_EFFECT_UPDATE, -10000, function(_, e)
    if state ~= "ready" then return end
    local sp = e.SpawnerEntity
    if sp and sp.Type == 1 and e.Timeout <= e.LifeSpan - 15 then e:SetTimeout(e.Timeout + 1) end
  end, 32)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_UPDATE, -10000, start)
  Isaac.AddPriorityCallback(mod, ModCallbacks.MC_POST_RENDER, -10000, start)
end)()

---------------------------------------------------------------- Repentance+ Lua API
-- From the PC Repentance+ exe and its resources/scripts/main.lua (isaac-repplus/REPPLUS_ALL_CHANGES.md 12).
-- main.c calls _isaaclua_repplus_api() right after it sets REPENTANCE_PLUS (the isaac-repplus mod is on),
-- before any mod's main.lua: mods test REPENTANCE_PLUS or FontRenderSettings, so both appear together.
function _isaaclua_repplus_api()
  _isaaclua_repplus_api = nil
  local FRS = {}
  FRS.__index = FRS
  -- REP+ exe: the settings are {alignment i32 +0, MaxCharacters u16 +4, width u16 +6, mode i32 +8 (1 wrap,
  -- 2 truncation), line height modifier f32 +0xc}; EnableAutoWrap(width) 0x85e800 also resets the modifier to 1,
  -- EnableTruncation(width) 0xa1a8d0.
  function FRS.EnableAutoWrap(s, w) s._mode, s._w, s._lhm = 1, (math.tointeger(tonumber(w) or 0) or 0) & 0xffff, 1 end
  function FRS.EnableTruncation(s, w) s._mode, s._w = 2, (math.tointeger(tonumber(w) or 0) or 0) & 0xffff end
  function FRS.IsAutoWrapEnabled(s) return s._mode == 1 end
  function FRS.IsTruncationEnabled(s) return s._mode == 2 end
  function FRS.GetAlignment(s) return s._align end
  function FRS.SetAlignment(s, a) s._align = math.tointeger(a) or 0 end
  function FRS.GetLineHeightModifier(s) return s._lhm end
  function FRS.SetLineHeightModifier(s, v) s._lhm = tonumber(v) or 1 end
  function FRS.GetMaxCharacters(s) return s._max end
  function FRS.SetMaxCharacters(s, n) s._max = math.tointeger(n) or 0 end
  function FRS.GetMissingCharacterOverride(s) return s._miss end
  function FRS.SetMissingCharacterOverride(s, c) s._miss = math.tointeger(c) or 0 end
  FontRenderSettings = setmetatable({}, {__call = function()
    return setmetatable({_mode = 0, _w = 0, _align = 0, _lhm = 1, _max = 0, _miss = 0}, FRS)
  end})
  -- Font:DrawString(str, x, y, sx, sy, color, settings) (REP+); the KColor call stays as it was (main.lua)
  local fm = getmetatable(Font()).__index
  local draw_legacy, draw_scaled, width, lh = fm.DrawString, fm.DrawStringScaled, fm.GetStringWidth, fm.GetLineHeight
  local function cut(str, n)  -- the first n UTF-8 characters
    if not n or n <= 0 then return str end
    local ok, off = pcall(utf8.offset, str, n + 1)
    return (ok and off) and str:sub(1, off - 1) or str
  end
  local function chars(str)  -- UTF-8 characters of str
    local t = {}
    for c in str:gmatch("[%z\1-\127\194-\244][\128-\191]*") do t[#t + 1] = c end
    return t
  end
  -- REP+ layout 0xa1bdf0: mode 1 wraps at the width (in scaled pixels), mode 2 cuts each line that is wider
  -- and ends it with "..." (the line then has to fit in width - min(width, width of "...")).
  local function layout(f, str, sx, st)
    local lines = {}
    for l in (str .. "\n"):gmatch("(.-)\n") do lines[#lines + 1] = l end
    local mode, w = st and st._mode or 0, st and st._w or 0
    if mode == 1 and w > 0 then
      local out = {}
      for _, l in ipairs(lines) do
        local cur = ""
        for word in l:gmatch("%S+") do
          local try = cur == "" and word or cur .. " " .. word
          if cur ~= "" and width(f, try) * sx > w then out[#out + 1] = cur; cur = word else cur = try end
        end
        out[#out + 1] = cur
      end
      lines = out
    elseif mode == 2 then
      local room = w - math.min(w, width(f, "..."))
      for i, l in ipairs(lines) do
        if width(f, l) * sx // 1 > w then
          local c = chars(l)
          local n = #c - 1
          while n > 0 and width(f, table.concat(c, "", 1, n)) * sx // 1 > room do n = n - 1 end
          lines[i] = table.concat(c, "", 1, n) .. "..."
        end
      end
    end
    return lines
  end
  function fm.DrawString(f, str, x, y, p4, p5, p6, p7)
    if type(p4) ~= "number" then return draw_legacy(f, str, x, y, p4, p5, p6) end
    local sx, sy, col, st = p4, tonumber(p5) or 1, p6, p7
    local a = st and st._align or 0
    str = cut(tostring(str), st and st._max)
    local lines = (st and st._mode ~= 0 or str:find("\n", 1, true)) and layout(f, str, sx, st) or {str}
    local h = lh(f) * sy * (st and st._lhm or 1)
    if a >= 6 then y = y - h * #lines elseif a >= 3 then y = y - h * #lines / 2 end
    for i, l in ipairs(lines) do
      local w, lx = width(f, l) * sx, x
      if a % 3 == 1 then lx = x - w / 2 elseif a % 3 == 2 then lx = x - w end
      draw_scaled(f, l, lx, y + (i - 1) * h, sx, sy, col, 0, false)
    end
  end
end


---------------------------------------------------------------- Repentance+ "My Stuff" pause page
-- REP+ PauseScreen (isaac-ng.exe 0x9b6230): the menu got a 4th entry, index 1 (0x9b693a); confirming it
-- switches the pause screen to state 3, where MyStuff (object at PauseScreen+0x838) runs instead of the
-- menu: Update 0x8d3680, Render 0x8d3960. The page (PauseScreen_MyStuff.anm2, 320x272, centred) has four
-- sections, cycled with one button (0x8d3813; a section with nothing in it is skipped):
--   0 collectibles: a 6x6 grid of 20 px cells from (10,36), scrolled by rows of 6 (0x8d3fa2); the items
--     are the player's history entries that are collectibles he still has (0x8d3bb0);
--   1 the description of the selected collectible (CollectibleDesc, 130 wide), scrolled line by line;
--   2 trinkets (history trinkets he still has), 3 pocket items; each with title + description.
-- Left/right and up/down repeat like REP+ (+0x648 / +0x64c: at 1, then every 15 frames after 30). The
-- description lines are REP+'s info_display.xml (conditional facts) when mystuff_data.lua carries them
-- (gen_mystuff.py --with-text), else the game's own description; the pool icons next to the title come
-- from REP+'s itempools.xml (0x8d6e60: minimap icon per pool). Art and font are REP+'s (gfx/ui/repplus,
-- font/repplus), shipped with Egor's OK (2026-11-05).
-- The C side (repplus_hooks.inc) adds the menu entry and calls _isaaclua_mystuff(event, triggered, held).
(function()
  local DIR = "rom:/mods/isaac-repplus/"
  local ANM, ITEM_ANM = "gfx/ui/repplus/PauseScreen_MyStuff.anm2", "gfx/ui/repplus/mystuff_item.anm2"
  local PAGE_W, PAGE_H, COLS, ROWS, CELL = 320, 272, 6, 6, 20
  local B_LEFT, B_RIGHT, B_UP, B_DOWN, B_OK, B_BACK, B_TAB = 1, 2, 4, 8, 16, 32, 64
  local INK = {0.11, 0.08, 0.07, 1}
  -- ItemPoolType -> minimap_icons.anm2 animation (REP+ 0x8d6e60)
  -- (greed pools 16-22 only in greed mode, the others only outside it)
  local POOL_ICON = {[0] = "IconTreasureRoom", "IconShop", "IconBoss", "IconDevilRoom", "IconAngelRoom",
    "IconSecretRoom", "IconLibrary", "IconSlot", "IconChest", "IconChest", "IconBeggar", "IconBeggar",
    "IconCurseRoom", "IconBeggar", "IconBeggar", "IconChest", "IconTreasureRoomGreed", "IconBoss", "IconShop",
    "IconDevilRoom", "IconAngelRoom", "IconCurseRoom", "IconSecretRoom", "IconSlot", "IconUltraSecretRoom",
    "IconBeggar", "IconPlanetarium", "IconChest", "IconShop", "IconChest", "IconBeggar"}
  local FS = 0.9  -- text scale on the page (REP+ MyStuff +0x1ec)
  local data, data_err
  local st = {open = false, sec = 0, sel = 0, top = 0, dscroll = 0, tsel = 0, psel = 0, rep_lr = 0, rep_ud = 0}
  local spr, font, icons, item_spr = nil, nil, nil, {}

  local function load_data()
    if data or data_err then return data end
    local text = C.readfile(DIR .. "mystuff_data.lua")
    local chunk = text and load(text, "=mystuff_data.lua", "t", {})
    local ok, t = false, "no " .. DIR .. "mystuff_data.lua"
    if chunk then ok, t = pcall(chunk) end
    if ok then data = t else data_err = tostring(t); C.log("isaac-repplus My Stuff: " .. data_err) end
    return data
  end
  local function player() return Isaac.GetPlayer(0) end

  -- the player's items as REP+ lists them: history order, only what he still holds
  local function history(p)
    local items, trinkets = {}, {}
    local ep = p and C.ptr(p)
    if not ep then return items, trinkets end
    local b, e = peek(ep, 0x24d0, "i64"), peek(ep, 0x24d8, "i64")
    local n = (e - b) // 0x1c
    if b == 0 or n < 0 or n > 4096 then return items, trinkets end
    for i = 0, n - 1 do
      local trinket, id = peek(b + i * 0x1c, 4, "u8") ~= 0, peek(b + i * 0x1c, 8, "i32")
      if trinket then
        if p:HasTrinket(id & 0x7fff, false) then trinkets[#trinkets + 1] = id end
      elseif p:HasCollectible(id, true) then items[#items + 1] = id end
    end
    return items, trinkets
  end
  local function pockets(p)
    local out = {}
    for slot = 0, 3 do
      local c, pill = p:GetCard(slot), p:GetPill(slot)
      if c and c > 0 then out[#out + 1] = {card = c} elseif pill and pill > 0 then out[#out + 1] = {pill = pill} end
    end
    return out
  end

  local function has_any(p, ids, self_id)
    for _, id in ipairs(ids) do
      if id == self_id then
        if p:GetCollectibleNum(id, true) >= 2 then return true end
      elseif p:HasCollectible(id, true) then return true end
    end
    return false
  end
  local function has_trinket(p, ids)
    for _, id in ipairs(ids) do if p:HasTrinket(id, false) then return true end end
    return false
  end
  local function in_list(v, ids)
    for _, id in ipairs(ids) do if id == v then return true end end
    return false
  end
  -- REP+ info lines that apply now: tag 2 = plain trinket / pill, 1 = golden trinket / horse pill
  local function info_lines(list, p, self_id, mode)
    local out = {}
    for _, l in ipairs(list or {}) do
      local ok = (not l.tag or l.tag == mode)
        and (not l.c or has_any(p, l.c, self_id)) and (not l.nc or not has_any(p, l.nc, self_id))
        and (not l.t or has_trinket(p, l.t)) and (not l.nt or not has_trinket(p, l.nt))
        and (not l.pl or in_list(p:GetPlayerType(), l.pl)) and (not l.npl or not in_list(p:GetPlayerType(), l.npl))
      if ok then out[#out + 1] = l[1] end
    end
    return out
  end

  -- one info line -> {text, color} pieces wrapped to width w (font units)
  local function wrap(lines, w)
    local out = {}
    for _, raw in ipairs(lines) do
      local col = INK
      local text = raw:gsub("<rooms=%d+>>?", "")
      text = text:gsub("<color=0x(%x%x)(%x%x)(%x%x)(%x%x)>", function(a, r, g, b)
        col = {tonumber(r, 16) / 255, tonumber(g, 16) / 255, tonumber(b, 16) / 255, 1}
        return ""
      end)
      text = text:gsub("<[^>]*>", "")
      if text:match("%S") then
        local line = ""
        for word in text:gmatch("%S+") do
          local try = line == "" and word or line .. " " .. word
          if line ~= "" and font:GetStringWidth(try) * FS > w then
            out[#out + 1] = {line, col}
            line = word
          else line = try end
        end
        if line ~= "" then out[#out + 1] = {line, col} end
      end
    end
    return out
  end

  local function gfx_path(path, dir)
    if not path or path == "" then return nil end
    if path:lower():sub(1, 4) ~= "gfx/" then path = dir .. path end
    return path
  end
  local function icon_sprite(key, path)
    local s = item_spr[key]
    if s == nil then
      s = false
      if path then
        s = Sprite()
        s:Load(ITEM_ANM, false)
        s:ReplaceSpritesheet(0, path)
        s:LoadGraphics()
        s:SetFrame("Idle", 0)
      end
      item_spr[key] = s
    end
    return s or nil
  end
  local function collectible_icon(id)
    local it = id > 0 and Isaac.GetItemConfig():GetCollectible(id)
    return icon_sprite("c" .. id, it and gfx_path(it.GfxFileName, "gfx/items/collectibles/")
      or "gfx/items/collectibles/questionmark.png")
  end
  local function trinket_icon(id)
    local it = Isaac.GetItemConfig():GetTrinket(id & 0x7fff)
    return icon_sprite("t" .. id, it and gfx_path(it.GfxFileName, "gfx/items/trinkets/"))
  end

  local view = {}  -- what the page shows, rebuilt on open and when the selection changes
  local ensure_gfx
  local function rebuild()
    ensure_gfx()
    local p = player()
    if not p then return end
    view.items, view.trinkets = history(p)
    view.pockets = pockets(p)
    local d = load_data() or {}
    st.sel = math.max(0, math.min(st.sel, #view.items - 1))
    st.tsel = math.max(0, math.min(st.tsel, #view.trinkets - 1))
    st.psel = math.max(0, math.min(st.psel, #view.pockets - 1))
    local cfg = Isaac.GetItemConfig()
    local id = view.items[st.sel + 1]
    local it = id and cfg:GetCollectible(id)
    view.title = it and it.Name or ""
    local lines = id and info_lines(d.c and d.c[id], p, id, 2) or {}
    if #lines == 0 and it and it.Description ~= "" then lines = {it.Description} end
    view.desc = wrap(lines, 138)
    view.pools = {}
    local greed = Game():IsGreedMode()
    for _, pt in ipairs(id and d.pools and d.pools[id] or {}) do
      if (pt >= 16 and pt <= 22) == greed and POOL_ICON[pt] then view.pools[#view.pools + 1] = POOL_ICON[pt] end
    end
    local t = view.trinkets[st.tsel + 1]
    local tc = t and cfg:GetTrinket(t & 0x7fff)
    view.ttitle = tc and (tc.Name .. (t & 0x8000 ~= 0 and " (golden)" or "")) or ""
    local tl = t and info_lines(d.t and d.t[t & 0x7fff], p, nil, t & 0x8000 ~= 0 and 1 or 2) or {}
    if #tl == 0 and tc and tc.Description ~= "" then tl = {tc.Description} end
    view.tdesc = wrap(tl, 128)
    local pk = view.pockets[st.psel + 1]
    view.ptitle, view.pdesc = "", {}
    if pk and pk.card then
      local c = cfg:GetCard(pk.card)
      view.ptitle = c and c.Name or ""
      local kl = info_lines(d.k and d.k[pk.card], p, nil, 2)
      if #kl == 0 and c and c.Description ~= "" then kl = {c.Description} end
      view.pdesc = wrap(kl, 128)
    elseif pk then
      local pool, color = Game():GetItemPool(), pk.pill
      local base = color & 0x7ff
      if pool:IsPillIdentified(base) then
        local eff = pool:GetPillEffect(color, p)
        local pe = eff and eff >= 0 and cfg:GetPillEffect(eff)
        view.ptitle = pe and pe.Name or "???"
        local pl = info_lines(d.p and d.p[eff], p, nil, color & 0x800 ~= 0 and 1 or 2)
        if #pl == 0 and pe and pe.Description and pe.Description ~= "" then pl = {pe.Description} end
        view.pdesc = wrap(pl, 128)
      else
        view.ptitle = "???"
      end
    end
  end

  local function has_section(s)
    if s == 0 then return true end
    if s == 1 then return #(view.desc or {}) > 0 end
    if s == 2 then return #(view.trinkets or {}) > 0 end
    return #(view.pockets or {}) > 0
  end
  local function sound() SFXManager():Play(282, 1, 0, false, 1) end  -- the menus' scroll sound
  -- REP+ repeat: the frame a direction goes down, then every 15 frames once it is held over 30
  local function rep(n) return n == 1 or (n > 30 and n % 15 == 0) end

  local function line_h() return font and font:GetLineHeight() * FS or 9 end
  local function desc_rows() return math.max(1, math.floor((156 - 48) / line_h())) end
  local function input(trig, held)
    if trig & B_BACK ~= 0 then st.open = false; return false end
    st.rep_lr = held & (B_LEFT | B_RIGHT) ~= 0 and st.rep_lr + 1 or 0
    st.rep_ud = held & (B_UP | B_DOWN) ~= 0 and st.rep_ud + 1 or 0
    local lr = rep(st.rep_lr) and ((held & B_LEFT ~= 0) and -1 or (held & B_RIGHT ~= 0) and 1 or 0) or 0
    local ud = rep(st.rep_ud) and ((held & B_UP ~= 0) and -1 or (held & B_DOWN ~= 0) and 1 or 0) or 0
    if trig & B_TAB ~= 0 then
      for _ = 1, 4 do
        st.sec = (st.sec + 1) % 4
        if has_section(st.sec) then break end
      end
      sound()
    end
    local before = {st.sel, st.dscroll, st.tsel, st.psel}
    if st.sec == 0 then
      local n = #view.items
      if n > 0 then
        local s = st.sel
        if lr ~= 0 then s = math.max(0, math.min(n - 1, s + lr)) end
        if ud < 0 and s >= COLS then s = s - COLS elseif ud > 0 and s + COLS <= n - 1 then s = s + COLS end
        if s ~= st.sel then st.sel, st.dscroll = s, 0 end
        -- scroll by rows like REP+ (+0x30 moves by 6 so the cursor stays in view)
        while st.sel < st.top do st.top = math.max(0, st.top - COLS) end
        while st.sel >= st.top + COLS * ROWS do st.top = st.top + COLS end
      end
    elseif st.sec == 1 then
      st.dscroll = math.max(0, math.min(math.max(0, #view.desc - desc_rows()), st.dscroll + ud))
    elseif st.sec == 2 then
      st.tsel = math.max(0, math.min(#view.trinkets - 1, st.tsel + ud + lr))
    else
      st.psel = math.max(0, math.min(#view.pockets - 1, st.psel + ud + lr))
    end
    if before[1] ~= st.sel or before[3] ~= st.tsel or before[4] ~= st.psel then rebuild(); sound()
    elseif before[2] ~= st.dscroll then sound() end
    if not has_section(st.sec) then st.sec = 0 end
    return true
  end

  -- REP+'s own art (PauseScreen_MyStuff.anm2 layers, MyStuff_SectionCursor.png, its minimap_icons) and its
  -- font TeamMeatEx12 cut to Latin/Cyrillic (tools/repplus/crop_font.py), all in the mod's resources
  local L_PAPER, L_ITEMS_UP, L_ITEMS_DOWN, L_DESC_UP, L_DESC_DOWN, L_POCKET_UP, L_POCKET_DOWN = 0, 1, 2, 3, 4, 5, 6
  local L_BACK_PAPER, L_CURSOR, L_TRINKET_UP, L_TRINKET_DOWN = 7, 9, 10, 11
  local section_spr
  function ensure_gfx()
    if spr then return true end
    -- REP+'s path first: only a mod (e.g. unintrusive pause menu) has it on Switch
    spr = Sprite()
    spr:Load("gfx/ui/pausescreen_mystuff.anm2", true)
    if not spr:IsLoaded() then spr = Sprite(); spr:Load(ANM, true) end
    spr:SetFrame("Idle", 0)
    section_spr = Sprite()
    section_spr:Load(ITEM_ANM, false)
    section_spr:ReplaceSpritesheet(0, "gfx/ui/repplus/MyStuff_SectionCursor.png")
    section_spr:LoadGraphics()
    section_spr:SetFrame("Idle", 0)
    font = Font()
    font:Load("font/repplus/mystuff12.fnt")
    icons = Sprite()
    icons:Load("gfx/ui/repplus/minimap_icons.anm2", true)
    return true
  end

  local function render()
    ensure_gfx()
    local W, H = Isaac.GetScreenWidth(), Isaac.GetScreenHeight()
    local s = math.min(1, (H - 6) / PAGE_H, (W - 6) / PAGE_W)
    local ox, oy = (W - PAGE_W * s) / 2, (H - PAGE_H * s) / 2
    local function at(x, y) return Vector(ox + x * s, oy + y * s) end
    local O = at(0, 0)
    local sc = Vector(s, s)
    spr.Scale, section_spr.Scale = sc, sc
    local function layer(l, on)
      if on == false then return end
      spr:SetFrame("Idle", 0)
      spr:RenderLayer(l, O)
    end
    layer(L_PAPER)
    layer(L_BACK_PAPER)
    local fs = s * FS
    local function text(str, x, y, col, w)
      col = col or INK
      if w and font:GetStringWidth(str) * FS > w then
        while #str > 1 and font:GetStringWidth(str .. "...") * FS > w do str = str:sub(1, -2) end
        str = str .. "..."
      end
      local p = at(x, y)
      font:DrawStringScaled(str, p.X, p.Y, fs, fs, KColor(col[1], col[2], col[3], col[4]), 0, false)
    end
    local lh = line_h()
    -- collectibles grid (cursor frame k = cell k, from (10,36) by 20)
    for i = st.top, math.min(#view.items, st.top + COLS * ROWS) - 1 do
      local k = i - st.top
      local ic = collectible_icon(view.items[i + 1])
      if ic then
        ic.Scale = Vector(s * 0.5, s * 0.5)
        ic:Render(at(26 + CELL * (k % COLS), 52 + CELL * (k // COLS)))
      end
    end
    layer(L_ITEMS_UP, st.top > 0)
    layer(L_ITEMS_DOWN, #view.items > st.top + COLS * ROWS)
    if #view.items > 0 then
      local k = st.sel - st.top
      if st.sec == 0 then
        spr:SetFrame("Idle", k)
        spr:RenderLayer(L_CURSOR, O)
        spr:SetFrame("Idle", 0)
      end
    end
    -- title, pools, description (nulls CollectibleTitle 145,8 / CollectibleItemPools 219,29 / CollectibleDesc 167,48)
    text(view.title or "", 145, 8, nil, 160)
    local px = 219
    for _, anim in ipairs(view.pools or {}) do
      icons.Scale = sc
      icons:SetFrame(anim, 0)
      icons:Render(at(px + 4, 33))
      px = px + 12
    end
    local rows = desc_rows()
    for i = 1, rows do
      local l = view.desc[st.dscroll + i]
      if not l then break end
      text(l[1], 160, 48 + (i - 1) * lh, l[2])
    end
    layer(L_DESC_UP, st.dscroll > 0)
    layer(L_DESC_DOWN, st.dscroll + rows < #view.desc)
    if st.sec == 1 then section_spr:Render(at(270, 30)) end  -- right of the pools, off the text (HW photo)
    -- trinkets (TrinketTitle 14,180 / TrinketDesc 25,214)
    local t = view.trinkets[st.tsel + 1]
    if t then
      local ic = trinket_icon(t)
      if ic then ic.Scale = Vector(s * 0.5, s * 0.5); ic:Render(at(20, 190)) end
      text(view.ttitle, 32, 182, nil, 108)
      for i, l in ipairs(view.tdesc) do
        if i > 3 then break end
        text(l[1], 16, 204 + (i - 1) * lh, l[2])
      end
      layer(L_TRINKET_UP, st.tsel > 0)
      layer(L_TRINKET_DOWN, st.tsel < #view.trinkets - 1)
      if st.sec == 2 then section_spr:Render(at(20, 190)) end
    end
    -- pocket items (PocketTitle 145,180 / PocketDesc 159,211)
    if view.pockets[st.psel + 1] then
      text(view.ptitle, 160, 182, nil, 130)
      for i, l in ipairs(view.pdesc) do
        if i > 3 then break end
        text(l[1], 160, 204 + (i - 1) * lh, l[2])
      end
      layer(L_POCKET_UP, st.psel > 0)
      layer(L_POCKET_DOWN, st.psel < #view.pockets - 1)
      if st.sec == 3 then section_spr:Render(at(152, 190)) end
    end
  end

  -- In-room item info (REP+ HUD+0x5c80: update 0x83cc80, layout 0x83ee90 mode 1, position 0x83d769). Once the
  -- "Item Info" achievement is unlocked (REP+ 641, given with the Mom kill achievements 4-6 at 0x7fc54f; here
  -- achievement 4, The Womb, which Switch gives at the same kill) and the option is on (Manager+0x2a3f8,
  -- toggled by action 31 ToggleDescriptions; Switch has no such button, the option stays on), in a cleared
  -- room the pickup nearest to player 1 within 100 units (collectible not shown as "?", trinket, pill, card)
  -- gets a box with its icon, name and REP+'s info lines. Collectibles never picked up (collection byte,
  -- PGD+0xa68 on Switch / +0xae8 in REP+) show "???" with "Collect this item once to identify it";
  -- unidentified pills show "Unidentified Pill". Spindown Dice adds the item it would turn into. Actives get
  -- the ui_charge_icons frame of their charge (0x8403e0). The box (black at 60%, text scale 0.625) sits with
  -- its left edge at x 65 when the player is on the right half of the screen, else with its right edge
  -- 90 from the right, top at y 60.
  local INFO_SCALE, INFO_W, INFO_PAD_X, INFO_PAD_Y = 0.625, 150, 10, 6
  local info = {key = nil, box = nil}
  local charge_spr
  local unlocked_fn = C.sym("_ZNK15IsaacRepentance18PersistentGameData8UnlockedENS_12eAchievementE")
  local function info_unlocked()
    local m = C.managerptr()
    if not (m and unlocked_fn) then return false end
    return C.call(unlocked_fn, "bpi", offset(m, 0x18), 4) and true or false
  end
  local function in_collection(id)
    local m = C.managerptr()
    if not m or id < 1 or id > 0x2dc then return true end
    return (peek(m, 0x18 + 0xa68 + id, "u8") or 1) ~= 0
  end
  -- ui_charge_icons frames: 1, 2, 3, 4, 6, 12 bars, timed, special, varies (0x8403e0)
  local CHARGE_FRAME = {[1] = 0, [2] = 1, [3] = 2, [4] = 3, [6] = 4, [12] = 5}
  local function charge_frame(id, p)
    if id == 711 then  -- Flip: only Tainted Lazarus (29 / 38) gets an icon (4 bars)
      local t = p:GetPlayerType()
      return (t == 29 or t == 38) and 3 or nil
    end
    if id == 286 or id == 263 or id == 348 or id == 489 or id == 147 then return 8 end
    local it = Isaac.GetItemConfig():GetCollectible(id)
    if not it or it.Type ~= 3 then return nil end
    if it.ChargeType == 1 then return 6 elseif it.ChargeType ~= 0 then return 7 end
    return CHARGE_FRAME[it.MaxCharges] or (it.MaxCharges > 0 and 7 or nil)
  end
  local function info_build(e, p, d)
    local cfg, v, sub = Isaac.GetItemConfig(), e.Variant, e.SubType
    local box = {lines = {}}
    local lines
    if v == 100 then
      local it = cfg:GetCollectible(sub)
      if not it then return nil end
      box.icon = collectible_icon(sub)
      if not in_collection(sub) then
        box.title, lines = "???", {"Collect this item once to identify it"}
      else
        box.title = it.Name
        lines = info_lines(d.c and d.c[sub], p, sub, 2)
        if #lines == 0 and it.Description ~= "" then lines = {it.Description} end
        box.charge = charge_frame(sub, p)
        if p:HasCollectible(723, false) then  -- Spindown Dice: the item it would turn into
          local n = sub - 1
          while n > 0 do
            local c = cfg:GetCollectible(n)
            if c and not c.Hidden then break end
            n = n - 1
          end
          local c = n > 0 and cfg:GetCollectible(n)
          if c then lines[#lines + 1] = "<color=0xFF00FF00>Spins down into " .. c.Name end
        end
      end
    elseif v == 350 then
      local it = cfg:GetTrinket(sub & 0x7fff)
      if not it then return nil end
      box.icon = trinket_icon(sub)
      box.title = it.Name .. (sub & 0x8000 ~= 0 and " (golden)" or "")
      lines = info_lines(d.t and d.t[sub & 0x7fff], p, nil, sub & 0x8000 ~= 0 and 1 or 2)
      if #lines == 0 and it.Description ~= "" then lines = {it.Description} end
    elseif v == 70 then
      box.ent = e
      local pool = Game():GetItemPool()
      if not pool:IsPillIdentified(sub & 0x7ff) then
        box.title, lines = "???", {"Unidentified Pill"}
      else
        local eff = pool:GetPillEffect(sub, p)
        local pe = eff and eff >= 0 and cfg:GetPillEffect(eff)
        box.title = pe and pe.Name or "???"
        lines = info_lines(d.p and d.p[eff], p, nil, sub & 0x800 ~= 0 and 1 or 2)
        if #lines == 0 and pe and pe.Description and pe.Description ~= "" then lines = {pe.Description} end
      end
    else
      local c = cfg:GetCard(sub)
      if not c then return nil end
      box.ent = e
      box.title = c.Name
      lines = info_lines(d.k and d.k[sub], p, nil, 2)
      if #lines == 0 and c.Description ~= "" then lines = {c.Description} end
    end
    -- wrap() works in My Stuff scale units (FS); the box text is drawn at INFO_SCALE
    box.lines = wrap(lines, (INFO_W - 2 * INFO_PAD_X) * FS / INFO_SCALE)
    for _, l in ipairs(box.lines) do
      local c = l[2]
      if c == INK then l[2] = {1, 1, 1, 1} end
    end
    return box
  end
  local function info_target(p)
    local best, bd = nil, 100 * 100
    for _, e in ipairs(Isaac.FindByType(5, -1, -1, false, false)) do
      local v = e.Variant
      local ok = v == 350 or v == 70 or v == 300
      if v == 100 and e.SubType > 0 then
        -- REP+ skips pedestals drawn as questionmark.png: Curse of the Blind, and pedestals forced blind (the
        -- second item of alt-floor treasure rooms etc.; Entity_Pickup::SetForceBlind keeps it at +0x562 on Switch)
        ok = Game():GetLevel():GetCurses() & LevelCurse.CURSE_OF_BLIND == 0
        local ep = ok and C.ptr(e)
        if ep and peek(ep, 0x562, "u8") ~= 0 then ok = false end
      end
      if ok and e.SubType > 0 then
        local dd = e.Position:DistanceSquared(p.Position)
        if dd < bd then best, bd = e, dd end
      end
    end
    return best
  end
  function _rp_item_info_render()
    if Game():IsPaused() or st.open then return end
    -- External Item Descriptions shows the same and more, and knows which pedestals are hidden (Egor
    -- 2026-11-12: the box is not needed with EID): while EID is loaded and not hidden, REP+'s box stays off
    local eid = rawget(_G, "EID")
    if type(eid) == "table" and not eid.isHidden then info.key = nil; return end
    local p = Isaac.GetPlayer(0)
    if not p or not Game():GetRoom():IsClear() then info.key = nil; return end
    if info.unlocked == nil or Game():GetFrameCount() % 300 == 0 then info.unlocked = info_unlocked() end
    if not info.unlocked then return end
    local e = info_target(p)
    if not e then info.key = nil; return end
    ensure_gfx()
    local d = load_data() or {}
    local key = GetPtrHash(e) .. ":" .. e.Variant .. ":" .. e.SubType .. ":" .. Game():GetFrameCount() // 30
    if key ~= info.key then info.key, info.box = key, info_build(e, p, d) end
    local box = info.box
    if not box then return end
    local W = Isaac.GetScreenWidth()
    local lh = font:GetLineHeight() * INFO_SCALE
    local h = 2 * INFO_PAD_Y + 32 + #box.lines * lh
    local sx = Isaac.WorldToScreen(p.Position).X
    local x = sx > W * 0.5 and 65 or W - 90 - INFO_W
    local y = 60
    if not info.bg then
      info.bg = Sprite()
      info.bg:Load(ITEM_ANM, false)
      info.bg:ReplaceSpritesheet(0, "gfx/ui/repplus/item_info_bg.png")
      info.bg:LoadGraphics()
      info.bg:SetFrame("Idle", 0)
    end
    -- the 32x32 white texture of mystuff_item.anm2 stretched to the box, tinted black at 60%
    info.bg.Color = Color(0, 0, 0, 0.6)
    info.bg.Scale = Vector(INFO_W / 32, h / 32)
    info.bg:Render(Vector(x + INFO_W / 2, y + h / 2))
    local ix, iy = x + INFO_PAD_X, y + INFO_PAD_Y
    if box.icon then
      box.icon.Scale = Vector(1, 1)
      box.icon:Render(Vector(ix + 16, iy + 16))
    elseif box.ent and box.ent:Exists() then  -- the pickup's own sprite (pill colour, card face)
      box.ent:GetSprite():Render(Vector(ix + 16, iy + 24))
    end
    if not box.tfit then  -- keep the title clear of the charge icon
      local tw, t = (INFO_W - 2 * INFO_PAD_X - 34 - (box.charge and 22 or 0)) / INFO_SCALE, box.title
      if font:GetStringWidth(t) > tw then
        while #t > 1 and font:GetStringWidth(t .. "...") > tw do t = t:sub(1, -2) end
        t = t .. "..."
      end
      box.title, box.tfit = t, true
    end
    font:DrawStringScaled(box.title, ix + 34, iy + 8, INFO_SCALE, INFO_SCALE, KColor(1, 1, 1, 1), 0, false)
    if box.charge then
      if not charge_spr then
        charge_spr = Sprite()
        charge_spr:Load("gfx/ui/repplus/ui_charge_icons.anm2", true)
      end
      charge_spr:SetFrame("ChargeIcon", box.charge)
      charge_spr:Render(Vector(x + INFO_W - 10 - 6, iy + 18))
    end
    for i, l in ipairs(box.lines) do
      local c = l[2]
      font:DrawStringScaled(l[1], ix, iy + 32 + (i - 1) * lh, INFO_SCALE, INFO_SCALE, KColor(c[1], c[2], c[3], 1), 0, false)
    end
  end

  -- the menu entry: a marker next to the game's own MY STUFF paper while it is picked
  MYSTUFF_MARK = MYSTUFF_MARK or {-172, 26}  -- from the pause screen's centre (REP+ layer MyStuff)
  local function menu_mark()
    ensure_gfx()
    local W, H = Isaac.GetScreenWidth(), Isaac.GetScreenHeight()
    section_spr.Scale = Vector(1, 1)
    section_spr:Render(Vector(W * 0.5 + MYSTUFF_MARK[1], H * 0.5 + MYSTUFF_MARK[2]))
  end

  function _isaaclua_mystuff(ev, trig, held)
    if ev == "open" then
      if not player() then return false end
      st.open, st.sec, st.dscroll, st.rep_lr, st.rep_ud = true, 0, 0, 0, 0
      rebuild()
      return true
    elseif ev == "input" then
      if not st.open then return false end
      return input(trig or 0, held or 0)
    elseif ev == "render" then
      if st.open then render() end
    elseif ev == "menu" then
      menu_mark()
    elseif ev == "close" then
      st.open = false
      item_spr = {}  -- icons of a later run may differ (mods)
    end
    return st.open
  end
end)()

-- Repentance+ compact pause menu (REP+ PauseScreen_small.anm2; REP+ always opens it, PauseScreen::Open
-- 0x9b7b90): drawn at (W*0.7, H*0.975) like REP+ (0x9b774e) on the frame of the game's own pause animation.
-- main.c gives the animation, its frame, the REP+ menu index and whether the seed paper shows; it renders the
-- completion widget at the returned spot (null CompletionWidget).
;(function()
  local spr, cur, font
  local LAYERS = {1, 3}  -- Paper, Blood (Background is hidden, Invite/Kick/Password are online only)
  -- SeedPaper (layer 4) x keyframes {x, delay, interpolated}: the seed text and the widget ride along
  local SEED_X = {
    Idle = {{-230, 1, false}},
    Appear = {{-718, 5, true}, {-718, 5, true}, {-230, 2, true}, {-230, 1, false}},
    Dissapear = {{-230, 1, false}, {-230, 2, true}, {-400, 5, true}, {-450, 3, true}},
  }
  local function seed_x(anim, frame)
    local k, f = SEED_X[anim] or SEED_X.Idle, 0
    for i, kf in ipairs(k) do
      if frame < f + kf[2] or i == #k then
        local nx = k[i + 1]
        if kf[3] and nx then return kf[1] + (nx[1] - kf[1]) * math.min(1, (frame - f) / kf[2]) end
        return kf[1]
      end
      f = f + kf[2]
    end
    return -230
  end
  local own  -- our copy of the REP+ sprite (no other mod gave gfx/ui/pausescreen_small.anm2)
  local function load(path)
    local s = Sprite()
    s:Load(path, true)
    return s:IsLoaded() and s or nil
  end
  function _isaaclua_pause_small(anim, frame, idx, seed_on)
    if not REPENTANCE_PLUS then return false end
    if spr == nil then
      -- REP+'s own path first: the Switch game has no such file, so it is there only if a mod (e.g. unintrusive
      -- pause menu) brings one; otherwise the REP+ original from the isaac-repplus mod
      local path = "gfx/ui/pausescreen_small.anm2"
      spr = load(path)
      own = not spr
      if own then path = "gfx/ui/repplus/pausescreen_small.anm2"; spr = load(path) end
      if not spr then spr = false
      else
        cur = load(path)
        font = Font()
        font:Load("font/teammeatfont12.fnt")
      end
    end
    if not spr then return false end
    local W, H = Isaac.GetScreenWidth(), Isaac.GetScreenHeight()
    local t = PAUSE_SMALL or {0.7, 0.975}
    local pos = Vector(W * t[1], H * t[2])
    spr:SetFrame(anim, frame)
    local idle = anim == "Idle"
    local dx = seed_x(anim, frame) + 230  -- how far REP+'s seed paper is from its Idle spot
    if own then
      for _, l in ipairs(LAYERS) do spr:RenderLayer(l, pos) end
      if seed_on then spr:RenderLayer(4, pos) end
    else
      spr:Render(pos)  -- a mod's sprite: its layers as it made them
    end
    if seed_on and font:IsLoaded() then
      local nx, ny = spr:_NullPos(5)  -- null Seed: 2 lines of 4, 15 apart (REP+ 0x9b86e4)
      if not nx and own then nx, ny = -270 + dx, -38 end  -- ours rides along with the paper
      local s = nx and Game():GetSeeds():GetStartSeedString()
      if s then
        local c = KColor(0x36 / 255, 0x2f / 255, 0x2d / 255, 1)
        font:DrawString(s:sub(1, 4), pos.X + nx, pos.Y + ny, c, 0, false)
        font:DrawString(s:sub(6, 9), pos.X + nx, pos.Y + ny + 15, c, 0, false)
      end
    end
    if idle then
      cur:SetFrame("Cursor", idx)
      cur:RenderLayer(2, pos)
    end
    if not own then  -- null CompletionWidget where the mod put it
      local wx, wy = spr:_NullPos(0)
      if not wx then return true, -1000, -1000 end
      return true, pos.X + wx, pos.Y + wy, 1
    end
    -- REP+ null CompletionWidget is (-214, -72); the Switch screen is 54 narrower, so the widget would cover
    -- the menu paper there: it goes above the seed paper instead
    local w = PAUSE_WIDGET or {-278, -102, 1}
    return true, pos.X + w[1] + dx, pos.Y + w[2], w[3]
  end
end)()

-- Repentance+ MODS paper on the main menu (REP+ GameMenu.anm2 layers ModsTab / ModsText): main.c calls this
-- after Menu_Game::Render with the menu's position, draws the button icon at the returned point (REP+ null
-- ModsButton) and opens the Mods menu with that button. Only while the isaac-repplus mod is on.
;(function()
  local spr
  function _isaaclua_mods_widget(x, y)
    if not REPENTANCE_PLUS then return false end
    if not spr then
      spr = Sprite()
      spr:Load("gfx/ui/repplus/modswidget.anm2", true)
      spr:Play("Idle", true)
      if not spr:IsLoaded() then spr = false end
    end
    if not spr then return false end
    -- REP+ places it for a 480 wide screen; the Switch menu shows less on the right
    local dx = MODS_WIDGET_DX or -30
    spr:Render(Vector(x + dx, y))
    return true, 364 + dx, 166
  end
end)()

-- DAILY RUN in the main menu's list (REP+ has ONLINE there): the game's own hand-drawn "DAILY RUN" from
-- Menu_DailyRun; main.c gives the spot and whether the daily run is open (greyed otherwise)
;(function()
  local spr
  function _isaaclua_daily_item(x, y, open)
    if not REPENTANCE_PLUS then return false end
    if spr == nil then
      spr = Sprite()
      spr:Load("gfx/ui/repplus/dailyitem.anm2", true)
      spr:Play("Idle", true)
      if not spr:IsLoaded() then spr = false end
    end
    if not spr then return false end
    local t = DAILY_ITEM or {-4, 8, 0.92, -4}  -- dx, dy, scale, rotation: on the list's slant (tuned on HW)
    spr.Color = open and Color(1, 1, 1, 1) or Color(1, 1, 1, 0.4)
    spr.Scale = Vector(t[3], t[3])
    spr.Rotation = t[4]
    spr:Render(Vector(x + t[1], y + t[2]))
    return true
  end
end)()
