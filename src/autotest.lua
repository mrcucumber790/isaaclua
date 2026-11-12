---------------------------------------------------------------- autotest (debug build only: build.sh DEBUG=1)
-- Plays by itself once a run starts, so long-run problems (memory, rare crashes) show without anyone playing:
-- invincible (debug 3), it walks every room of the floor, gives one mod item or trinket per room (an active
-- one is also used), spawns the enemies and pickups of the mods' content/entities2.xml, kills the room's enemies, visits a few
-- special rooms with goto, then goes to the next floor with stage; after the last floor a new run. It stops
-- after isaaclua.ini "autotest = <minutes>" (20 in this build; with batches of mods, see main.c, per batch). sdmc:/isaaclua/autotest.log gets a status line
-- every 15 seconds (largest free memory block, Lua memory, errors so far, where it is, what it did last) and
-- each new error's first line; the file is rewritten at every status line, so a crash keeps what came before.
-- In each room a screenshot goes to the album (isaaclua.ini "autotest_shots = 0" turns them off).
;(function()
  if not C.autotest then return end
  local minutes  -- C.autotest() at the first run: the batch is picked while the mods load (after this file)
  local STAGES = {"1", "1c", "2", "2a", "3", "3c", "4", "4a", "5", "5c", "6", "6a", "7", "8", "9", "10", "10a",
    "11", "11a", "12", "13"}
  local SPECIAL = {"s.boss", "s.shop", "s.treasure", "s.planetarium", "s.devil", "s.angel", "s.secret",
    "s.library", "s.sacrifice", "s.curse", "s.arcade", "s.challenge", "s.dice", "s.chest", "s.miniboss",
    "s.isaacs", "s.barren", "s.supersecret", "s.ultrasecret"}
  local ROOM_FRAMES, PER_FLOOR_SPECIAL, MAX_SHOTS = 90, 3, 150

  local items, trinkets, ents = {}, {}, {}
  local t0, started, stopped
  local st = {stage = 0, special = 0, item = 0, trinket = 0, ent = 0, rooms = nil, ri = 0, wait = 0, phase = "rooms",
    floors = 0, runs = 0, given = 0, spawned = 0, shots = 0, last = "start"}
  local shots = C.autotest_shots and C.autotest_shots()

  local function clock()
    local s = (C.ms() - (t0 or C.ms())) // 1000
    return string.format("%02d:%02d", s // 60, s % 60)
  end
  local function log(text, flush) C.autolog("[" .. clock() .. "] " .. text, flush) end
  local function try(what, f, ...)
    st.last = what
    local ok, e = pcall(f, ...)
    if not ok then log("  FAIL " .. what .. ": " .. tostring(e):match("[^\n]*")) end
    return ok
  end

  local function collect()
    local list = config:GetCollectibles()
    for id = CollectibleType.NUM_COLLECTIBLES, (list.Size or 0) - 1 do
      local it = config:GetCollectible(id)
      if it then items[#items + 1] = {id = id, active = it.Type == ItemType.ITEM_ACTIVE} end
    end
    local tl = config:GetTrinkets()
    for id = TrinketType.NUM_TRINKETS, (tl.Size or 0) - 1 do
      if config:GetTrinket(id) then trinkets[#trinkets + 1] = id end
    end
    local seen = {}
    for key, def in pairs(mod_entities) do
      -- enemies, pickups, slots: what a room spawns by itself (a mod's effects, tears and familiars come from
      -- its own code, with a parent or spawner it expects; spawned alone they only fail in the mod)
      local t = def.Type
      if key:find("^%d+%.%d+$") and not seen[def] and (t == 5 or t == 6 or (t >= 10 and t < 1000)) then
        seen[def] = true
        ents[#ents + 1] = def
      end
    end
    table.sort(ents, function(a, b) return a.Type * 65536 + a.Variant < b.Type * 65536 + b.Variant end)
  end

  local function status(flush)
    local lv, desc = Game():GetLevel(), nil
    pcall(function() desc = lv:GetCurrentRoomDesc() end)
    local ents_n = 0
    pcall(function() ents_n = #Isaac.GetRoomEntities() end)
    local free, lua_mb = C.memfree(), collectgarbage("count") / 1024
    st.minfree = math.min(st.minfree or free, free)
    st.maxlua = math.max(st.maxlua or 0, lua_mb)
    log(string.format("free %d MB | lua %.1f MB | errors %d | run %d floor %d (%s) room %s type %d | gave %d spawned %d" ..
      " | entities %d | last: %s", free, lua_mb, C.errors(), st.runs, st.floors,
      STAGES[st.stage] or "?", desc and tostring(desc.SafeGridIndex) or "?", Game():GetRoom():GetType(), st.given,
      st.spawned, ents_n, st.last), flush)
  end

  local function next_stage()
    st.stage = st.stage + 1
    if st.stage > #STAGES then
      st.stage, st.runs = 0, st.runs + 1
      log("== new run ==", true)
      try("restart", Isaac.ExecuteCommand, "restart")
      started = false  -- POST_GAME_STARTED sets it up again
      return
    end
    st.floors = st.floors + 1
    log("== stage " .. STAGES[st.stage] .. " ==", true)
    st.rooms, st.phase = nil, "rooms"
    try("stage " .. STAGES[st.stage], Isaac.ExecuteCommand, "stage " .. STAGES[st.stage])
  end

  local function in_room()  -- one mod item or trinket, one or two mod entities
    local p = Isaac.GetPlayer(0)
    if not p then return end
    if #items > 0 and (st.given % 3 ~= 2 or #trinkets == 0) then
      st.item = st.item % #items + 1
      local it = items[st.item]
      if try("give c" .. it.id, p.AddCollectible, p, it.id, 0, true) then st.given = st.given + 1 end
      if it.active then try("use c" .. it.id, p.UseActiveItem, p, it.id, 0, -1) end
    elseif #trinkets > 0 then
      st.trinket = st.trinket % #trinkets + 1
      if try("give t" .. trinkets[st.trinket], p.AddTrinket, p, trinkets[st.trinket]) then st.given = st.given + 1 end
    end
    for _ = 1, math.min(2, #ents) do
      st.ent = st.ent % #ents + 1
      local d = ents[st.ent]
      local what = "spawn " .. d.Type .. "." .. d.Variant .. "." .. d.SubType .. (d.Name and " (" .. d.Name .. ")" or "")
      if try(what, Isaac.Spawn, d.Type, d.Variant, d.SubType, p.Position + Vector(60, 0), Vector(0, 0), nil) then
        st.spawned = st.spawned + 1
      end
    end
  end
  local function kill_enemies()
    for _, e in ipairs(Isaac.GetRoomEntities()) do
      if e:IsVulnerableEnemy() then e:Kill() end
    end
  end

  -- what is on screen (the room after the mods' spawns, before the kill) to the album, as the capture button
  -- does; the numbers in the log give the order of the pictures
  local function shot()
    if not (shots and C.screenshot) or st.shots >= MAX_SHOTS then return end
    st.shots = st.shots + 1
    local r = C.screenshot()
    local desc = Game():GetLevel():GetCurrentRoomDesc()
    log(string.format("shot %d %s room %s type %d%s", st.shots, STAGES[st.stage] or "?", tostring(desc.SafeGridIndex),
      Game():GetRoom():GetType(), r ~= 0 and string.format(" FAILED 0x%x", r) or ""))
    if r ~= 0 then shots = false end  -- screenshots not allowed here: no more tries
  end

  local function step()
    if st.wait > 0 then
      st.wait = st.wait - 1
      if st.wait == ROOM_FRAMES - 20 then try("room items", in_room)
      elseif st.wait == 45 then shot()
      elseif st.wait == 30 then try("kill enemies", kill_enemies) end
      return
    end
    if st.phase == "rooms" then
      local lv = Game():GetLevel()
      if not st.rooms then
        st.rooms, st.ri = {}, 0
        local list = lv:GetRooms()
        for i = 0, list.Size - 1 do
          local d = list:Get(i)
          if d and d.Data then st.rooms[#st.rooms + 1] = d.SafeGridIndex end
        end
        -- two vanilla items per floor, as a run piles them up
        local p = Isaac.GetPlayer(0)
        for _ = 1, 2 do
          local id = math.random(1, CollectibleType.NUM_COLLECTIBLES - 1)
          local it = config:GetCollectible(id)
          if p and it then
            log("  vanilla item c" .. id .. " " .. tostring(it.Name), false)
            try("give c" .. id, p.AddCollectible, p, id, 0, true)
          end
        end
      end
      st.ri = st.ri + 1
      local idx = st.rooms[st.ri]
      if not idx then st.phase, st.ri = "special", 0 return end
      try("room " .. idx, Game().ChangeRoom, Game(), idx)
      st.wait = ROOM_FRAMES
    elseif st.phase == "special" then
      st.ri = st.ri + 1
      if st.ri > PER_FLOOR_SPECIAL then return next_stage() end
      st.special = st.special % #SPECIAL + 1
      try("goto " .. SPECIAL[st.special], Isaac.ExecuteCommand, "goto " .. SPECIAL[st.special])
      st.wait = ROOM_FRAMES
    end
  end

  Isaac.AddPriorityCallback(internal_mod, ModCallbacks.MC_POST_GAME_STARTED, 100000, function(_, continued)
    if stopped then return end
    -- the menu's A presses take "Continue" when a run is saved: the autotest wants a new one
    if continued then  -- Game::StartFromSavedState (main.c)
      log("continued run: restart", true)
      started = false
      try("restart", Isaac.ExecuteCommand, "restart")
      return
    end
    if not t0 then
      minutes = C.autotest()
      if minutes <= 0 then stopped = true return end
      t0 = C.ms()
      collect()
      local b, nb, names = C.autotest_batch()
      if b > 0 then log(string.format("batch %d/%d: %s", b, nb, names:gsub("\n$", ""):gsub("\n", ", ")), true) end
      log(string.format("autotest %d min: %d mod items, %d mod trinkets, %d mod entities", minutes, #items,
        #trinkets, #ents), true)
    end
    started = true
    st.rooms, st.phase, st.wait = nil, "rooms", 30
    try("debug 3", Isaac.ExecuteCommand, "debug 3")
    if st.stage == 0 then st.stage, st.floors = 1, st.floors + 1 end
  end)
  -- Entity_Familiar's Player* (+0x400) must be a player: the 0.79 autotest crashed in
  -- Entity_Familiar::TriggerRoomClear on one overwritten with two floats. Checked every frame; a bad one is
  -- logged with what the autotest did last and set back to player 0, so the batch goes on.
  local bad_fam = {}
  local function check_familiars(when)
    local ps, room = {}, nil
    for i = 0, Game():GetNumPlayers() - 1 do
      local p = Isaac.GetPlayer(i)
      if p and ptr(p) then ps[tostring(ptr(p))] = true end
      local sub = p and ptr(p) and peek(ptr(p), O.Player_SubPlayer, "p")  -- The Soul
      if sub then ps[tostring(sub)] = true end
    end
    for _, f in ipairs(Isaac.FindByType(EntityType.ENTITY_FAMILIAR)) do
      local fp = ptr(f)
      local q = fp and peek(fp, 0x400, "p")
      if f.Variant == FamiliarVariant.SIREN_MINION then q = nil end  -- charmed by The Siren: not a player's
      if fp and q and not ps[tostring(q)] and not room then  -- or any entity of the room
        room = {}
        for _, e in ipairs(Isaac.GetRoomEntities()) do if ptr(e) then room[tostring(ptr(e))] = true end end
      end
      if fp and q and not ps[tostring(q)] and not room[tostring(q)] then
        local key = tostring(fp) .. tostring(q)
        if not bad_fam[key] then
          bad_fam[key] = true
          log(string.format("  BAD FAMILIAR %d.%d.%d (%s): Player is 0x%x, frame %d, last: %s", f.Type, f.Variant,
            f.SubType, when, peek(fp, 0x400, "i64") or 0, Game():GetFrameCount(), st.last), true)
        end
        poke(fp, 0x400, "p", ptr(Isaac.GetPlayer(0)))
      end
    end
  end
  Isaac.AddPriorityCallback(internal_mod, ModCallbacks.MC_POST_UPDATE, 100000, function()
    if not started or stopped then return end
    pcall(check_familiars, "update")
    if (C.ms() - t0) > minutes * 60000 then
      stopped = true
      pcall(status, false)
      log("== autotest done ==", true)
      -- the summary line of the batch; with batches the game then relaunches with the next one
      C.autotest_next(string.format("errors %d, lowest free %d MB, most Lua %.1f MB, %d runs, %d floors, gave %d," ..
        " spawned %d, %d shots", C.errors(), st.minfree or -1, st.maxlua or 0, st.runs, st.floors, st.given,
        st.spawned, st.shots))
      return
    end
    step()
  end)
  local last_status = 0
  Isaac.AddPriorityCallback(internal_mod, ModCallbacks.MC_POST_RENDER, 100000, function()
    if not t0 or stopped then return end
    pcall(check_familiars, "render")
    local now = C.ms()
    if now - last_status >= 15000 then
      last_status = now
      pcall(status, true)
    end
  end)
end)()
