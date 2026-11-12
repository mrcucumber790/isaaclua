---------------------------------------------------------------- debug console (as PC's ` console)
-- The Switch build has no console at all. This one opens with the ` key of the on-screen keyboard
-- (+ and - together), takes typed lines from it, and runs the PC commands that make sense here:
-- giveitem, remove, spawn, reseed, lua, clear, help. Anything else goes to MC_EXECUTE_CMD, as on PC,
-- so mods can add their own commands. While it is open, mods do not see keyboard keys.
;(function()  -- its own function: the main chunk is near Lua's 200-locals limit
  local KEY_GRAVE, KEY_ESC, KEY_ENTER, KEY_TAB, KEY_BKSP = 96, 256, 257, 258, 259
  local KEY_UP, KEY_DOWN, KEY_SHIFT = 265, 264, 340
  local raw_pressed, raw_triggered = Input.IsButtonPressed, Input.IsButtonTriggered
  local con = {open = false, input = "", lines = {}, history = {}, hist = 0}
  local MAX_LINES, WRAP = 200, 66

  local function out(text, r, g, b)
    for line in (tostring(text) .. "\n"):gmatch("(.-)\n") do
      repeat  -- wrapped to the screen's width (about 68 characters at scale 1)
        con.lines[#con.lines + 1] = {line:sub(1, WRAP), r or 1, g or 1, b or 1}
        line = line:sub(WRAP + 1)
      until line == ""
    end
    while #con.lines > MAX_LINES do table.remove(con.lines, 1) end
  end
  local function err(text) out(text, 1, 0.45, 0.45) end

  -- mods' console output lands here as well (on PC Isaac.ConsoleOutput writes to the console)
  local debug_string = Isaac.DebugString
  function Isaac.ConsoleOutput(s)
    out((tostring(s):gsub("\n$", "")))
    debug_string(s)
  end

  -- keys typed into the line: GLFW codes -> characters, shifted variants as on a US keyboard
  local plain = {[32] = " ", [39] = "'", [44] = ",", [45] = "-", [46] = ".", [47] = "/", [59] = ";", [61] = "=",
    [91] = "[", [92] = "\\", [93] = "]"}
  local shifted = {[39] = '"', [44] = "<", [45] = "_", [46] = ">", [47] = "?", [59] = ":", [61] = "+", [91] = "{",
    [92] = "|", [93] = "}", [48] = ")", [49] = "!", [50] = "@", [51] = "#", [52] = "$", [53] = "%", [54] = "^",
    [55] = "&", [56] = "*", [57] = "("}
  local function typed_char(key, shift)
    if key >= 65 and key <= 90 then return shift and string.char(key) or string.char(key + 32) end
    if shift and shifted[key] then return shifted[key] end
    if key >= 48 and key <= 57 then return string.char(key) end
    return plain[key]
  end

  -- giveitem / remove argument: c<id> t<id> k<id> p<id>, a bare id (collectible) or a name
  local kinds = {c = "collectible", t = "trinket", k = "card", p = "pill"}
  local function item_name(it) return tostring(it.Name or ""):gsub("^#", ""):gsub("_NAME$", ""):gsub("_", " ") end
  local function find_item(arg)
    local kind, id = arg:match("^([ctkpCTKP]?)(%d+)$")
    if id then return kinds[kind:lower()] or "collectible", tonumber(id) end
    local want = arg:lower():gsub("_", " ")
    local list = config:GetCollectibles()
    local found
    for i = 1, list.Size - 1 do
      local it = list:Get(i)
      if it then
        local n = item_name(it):lower()
        if n == want then return "collectible", it.ID end
        if not found and n:find(want, 1, true) then found = it.ID end
      end
    end
    if found then return "collectible", found end
  end
  local function player() return Isaac.GetPlayer(0) end

  local help = {
    {"giveitem", "g", "giveitem c<id>|t<id>|k<id>|p<id>|<name> [count]: give a collectible, trinket, card or pill"},
    {"remove", "r", "remove c<id>|t<id>|<name>: take a collectible or trinket away"},
    {"spawn", nil, "spawn <type>.<variant>.<subtype> [count]: spawn an entity next to the player"},
    {"reseed", nil, "reseed: a new layout for this floor"},
    {"lua", "l", "lua <code>: run Lua (prints what an expression returns)"},
    {"clear", "cls", "clear: empty the console"},
    {"help", nil, "help: this list. Up/Down: history, Tab: complete, Esc or `: close"},
  }
  local alias = {}
  for _, h in ipairs(help) do if h[2] then alias[h[2]] = h[1] end end

  function commands.giveitem(rest)
    local arg, count = rest:match("^(.-)%s+(%d+)$")
    arg, count = arg or rest, tonumber(count) or 1
    if arg == "" then return "usage: " .. help[1][3] end
    local kind, id = find_item(arg)
    local p = player()
    if not kind then return "no item named " .. arg end
    if not p then return "no player" end
    for _ = 1, math.min(count, 64) do
      if kind == "collectible" then
        if not config:GetCollectible(id) then return "no collectible " .. id end
        p:AddCollectible(id, 0, true)
      elseif kind == "trinket" then p:AddTrinket(id)
      elseif kind == "card" then p:AddCard(id)
      else p:AddPill(id) end
    end
    local it = kind == "collectible" and config:GetCollectible(id)
    return "gave " .. kind .. " " .. id .. (it and " (" .. item_name(it) .. ")" or "") .. (count > 1 and " x" .. count or "")
  end
  function commands.remove(rest)
    local kind, id = find_item(rest)
    local p = player()
    if not kind then return "no item named " .. rest end
    if not p then return "no player" end
    if kind == "collectible" then p:RemoveCollectible(id)
    elseif kind == "trinket" and p.TryRemoveTrinket then p:TryRemoveTrinket(id)
    else return "can only remove collectibles and trinkets" end
    return "removed " .. kind .. " " .. id
  end
  function commands.spawn(rest)
    local t, v, s, count = rest:match("^(%d+)%.?(%d*)%.?(%d*)%s*(%d*)$")
    if not t then return "usage: " .. help[3][3] end
    local p = player()
    local pos = p and p.Position + Vector(0, 40) or Vector(320, 280)
    local e
    for _ = 1, math.min(tonumber(count) or 1, 32) do
      e = Isaac.Spawn(tonumber(t), tonumber(v) or 0, tonumber(s) or 0, pos, Vector(0, 0), nil)
    end
    if not e then return "could not spawn " .. rest end
    return "spawned " .. e.Type .. "." .. e.Variant .. "." .. e.SubType
  end
  function commands.lua(rest)
    local f, msg = load("return " .. rest, "console")
    if not f then f, msg = load(rest, "console") end
    if not f then return nil, msg end
    local res = table.pack(pcall(f))
    if not res[1] then return nil, tostring(res[2]) end
    if res.n < 2 then return nil end
    local parts = {}
    for i = 2, res.n do parts[#parts + 1] = tostring(res[i]) end
    return table.concat(parts, "\t")
  end
  function commands.clear() con.lines = {} end
  commands.cls = commands.clear
  function commands.help()
    for _, h in ipairs(help) do out(h[3] .. (h[2] and "  (" .. h[2] .. ")" or ""), 0.7, 0.9, 1) end
  end

  -- The Switch build still has PC's Console::RunCommand (never called: the console object is never made).
  -- Commands we do not implement ourselves go to it on a console of our own: goto, stage, debug, curse...
  local game_cmds = {}
  for w in ("goto stage debug curse seed challenge combo gridspawn playsfx clearcache cutscene restock forceroom " ..
            "time macro listcollectibles hud copy delirious costumetest bigchest repeat clearseeds giveitem2 " ..
            "remove2"):gmatch("%S+") do game_cmds[w] = true end
  local RUN = NS .. "7Console10RunCommandERKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEEPS7_PNS_13Entity_PlayerE"
  local gcon
  local function game_command(line)
    local f = sym(RUN)
    if not f then return nil, "not on Switch yet: console command" end
    if not gcon then  -- sizeof(Console) is 0x168 (Console::Init writes up to +0x15c)
      gcon = C.buf(0x400)
      call(sym(NS .. "7ConsoleC1Ev"), "_p", gcon)
      call(sym(NS .. "7Console4InitEv"), "_p", gcon)
    end
    local n = #line
    local chars, str, res = C.buf(n + 1), C.buf(24), C.buf(24)
    for i = 1, n do poke(chars, i - 1, "u8", line:byte(i)) end
    poke(str, 0, "i64", ((n + 16) & ~15) | 1)  -- libc++ long string: capacity | 1, size, data
    poke(str, 8, "i64", n)
    poke(str, 16, "p", chars)
    -- RunCommand+0x40: with no player argument the game takes Game+0x25c50 (its player vector)[0]; on an
    -- empty vector (between a restart/stage change and the new player) that is a null read. Our player()
    -- walks the room's entity list, which can still hold a player then (HW 0.99.38/0.99.39 crashes).
    local g = C.gameptr()
    if not g or peek(g, 0x25c50, "i64") == peek(g, 0x25c58, "i64") then
      return "", "no player yet: command skipped"
    end
    -- always pass a player: with one, RunCommand never reads the vector (HW 0.99.44 still crashed there once
    -- with this check passed; cause unknown, so the null path is not taken at all)
    local p = player()
    local pp = p and ptr(p) or peek(peek(g, 0x25c50, "p"), 0, "p")
    if not pp then return "", "no player yet: command skipped" end
    call(f, "_pppp", gcon, str, res, pp)
    return peek(res, 0, "str") or ""
  end

  -- one console line: our commands, then mods' MC_EXECUTE_CMD (as PC does for every command)
  local function execute(line, echo)
    local word, rest = line:match("^%s*(%S+)%s*(.-)%s*$")
    if not word then return end
    if echo then out("> " .. line, 0.6, 0.6, 0.6) end
    word = word:lower()
    word = alias[word] or word
    local f = commands[word]
    if f then
      local ok, r, e = pcall(f, rest)
      if not ok then err(r) elseif e then err(e) elseif r and r ~= "" then out(r) end
      return r or ""
    end
    if game_cmds[word] then
      local ok, r, e = pcall(game_command, line:match("^%s*(.-)%s*$"))
      -- the game ends each answer with "\n"; PC's Isaac.ExecuteCommand returns it without (CustomHealthAPI in
      -- Fiend Folio repeats "debug 3" until it gets exactly "Enabled debug flag.": HW 0.99.35 hung for ever)
      if ok and type(r) == "string" then r = r:gsub("\n$", "") end
      if not ok then err(r) elseif e then err(e) elseif r ~= "" then out((r:gsub("\n$", ""))) end
      -- a mod's goto that finds no room only prints (no transition): keep what the game said in errors.log
      -- (only a failure: "Changed room." on screen for every scripted goto was noise)
      if not echo and word == "goto" and ok and r and r:find("Error") and C.log then
        C.log("Isaac.ExecuteCommand(\"" .. line .. "\"): " .. r:gsub("%s+$", ""))
      end
      return ok and r or ""
    end
    if HasCallbacks(ModCallbacks.MC_EXECUTE_CMD) then
      -- PC (isaac-ng.exe 0x801f30): a string the callback returns is the command's output
      local r = Run(ModCallbacks.MC_EXECUTE_CMD, nil, word, rest, player())
      if type(r) == "string" then
        if r ~= "" then out((r:gsub("\n$", ""))) end
        return r
      end
      return ""
    end
    err("unknown command " .. word .. " (help: list)")
    return ""
  end
  function Isaac.ExecuteCommand(cmd) return execute(tostring(cmd), false) or "" end

  local function complete()
    local word = con.input:match("^(%S*)$")
    if not word or word == "" then return end
    local hits = {}
    for name in pairs(commands) do if name:sub(1, #word) == word:lower() then hits[#hits + 1] = name end end
    table.sort(hits)
    if #hits == 1 then con.input = hits[1] .. " "
    elseif #hits > 1 then out(table.concat(hits, "  "), 0.7, 0.9, 1) end
  end

  local function read_keys()
    if raw_triggered(KEY_GRAVE, 0) or (con.open and raw_triggered(KEY_ESC, 0)) then
      con.open = not con.open
      return
    end
    if not con.open then return end
    local shift = raw_pressed(KEY_SHIFT, 0)
    if raw_triggered(KEY_ENTER, 0) then
      local line = con.input
      con.input = ""
      if line:match("%S") then
        if con.history[#con.history] ~= line then con.history[#con.history + 1] = line end
        con.hist = #con.history + 1
        execute(line, true)
      end
    elseif raw_triggered(KEY_BKSP, 0) then con.input = con.input:sub(1, -2)
    elseif raw_triggered(KEY_TAB, 0) then complete()
    elseif raw_triggered(KEY_UP, 0) or raw_triggered(KEY_DOWN, 0) then
      local n = #con.history
      if n > 0 then
        con.hist = math.max(1, math.min(n + 1, con.hist + (raw_triggered(KEY_UP, 0) and -1 or 1)))
        con.input = con.history[con.hist] or ""
      end
    else
      for key = 32, 93 do
        if raw_triggered(key, 0) then
          local c = typed_char(key, shift)
          if c and #con.input < 120 then con.input = con.input .. c end
        end
      end
    end
  end

  local SCALE, LINE_H = 1, 10  -- whole-pixel scale: half scale blurs on the Switch's panel
  local function draw()
    local x, y = 8, 14
    local shown = 9
    local first = math.max(1, #con.lines - shown + 1)
    for i = first, #con.lines do
      local l = con.lines[i]
      Isaac.RenderScaledText(l[1], x + 1, y + 1, SCALE, SCALE, 0, 0, 0, 1)
      Isaac.RenderScaledText(l[1], x, y, SCALE, SCALE, l[2], l[3], l[4], 1)
      y = y + LINE_H
    end
    local cursor = (Isaac.GetFrameCount() // 15) % 2 == 0 and "_" or ""
    Isaac.RenderScaledText("] " .. con.input .. cursor, x + 1, y + 1, SCALE, SCALE, 0, 0, 0, 1)
    Isaac.RenderScaledText("] " .. con.input .. cursor, x, y, SCALE, SCALE, 1, 1, 0.6, 1)
  end

  Isaac.AddPriorityCallback(internal_mod, ModCallbacks.MC_POST_RENDER, 10000, function()
    read_keys()
    if con.open then draw() end
  end)
  -- keys typed into the console are not mods' keys
  function Input.IsButtonPressed(key, ctrl)
    if con.open and key >= 32 and (ctrl or 0) <= 0 then return false end
    return raw_pressed(key, ctrl)
  end
  function Input.IsButtonTriggered(key, ctrl)
    if con.open and key >= 32 and (ctrl or 0) <= 0 then return false end
    return raw_triggered(key, ctrl)
  end
  Isaac.IsConsoleOpen = function() return con.open end
  _isaaclua_console = con  -- for the host test
  out("isaaclua console: help lists the commands", 0.7, 0.9, 1)
end)()

return exports
