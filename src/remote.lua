-- Remote control (debug build only, src/remote.c): a relay on the internet sends Lua chunks, the game runs them
-- between frames and sends back what they printed and returned. A chunk runs as a coroutine resumed once per
-- frame, so it can wait for the game: wait(n) lets n frames pass, press("A", n) holds a pad button for n frames.
-- Variables a chunk sets stay for the next ones (env below).
;(function(C)
  if not (C and C.remote_press) then return end
  local env = setmetatable({}, {__index = _G})
  local cur
  local function fmt(...)
    local t = {}
    for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
    return table.concat(t, "\t")
  end
  env.wait = function(n) for _ = 1, n or 1 do coroutine.yield() end end
  env.press = function(button, frames) C.remote_press(button, frames or 6) end
  env.errors = function() return C.remote_log and C.remote_log() or "" end
  env.frame = function() return Isaac.GetFrameCount() end
  env.C = C
  -- shot(): what is on screen, as the chunk's answer ("IMG <w> <h>" and base64 RGB; tools/shot.py makes a PNG)
  env.shot = function()
    local w, h, data = C.remote_shot()
    if not w then return "ERROR shot: " .. tostring(h) end
    return "IMG " .. w .. " " .. h .. "\n" .. data
  end
  -- the mods on the card: mods() lists them ("+" on, "-" off); only({names}) switches all others off and
  -- starts the game again (the relay sees the console come back a few seconds later)
  env.mods = function()
    local t = {}
    for _, m in ipairs(C.remote_mods()) do t[#t + 1] = (m.on and "+ " or "- ") .. m.name end
    return table.concat(t, "\n")
  end
  env.only = function(names)
    local want, off = {}, {}
    for _, n in ipairs(names) do want[n] = true end
    for _, m in ipairs(C.remote_mods()) do if not want[m.name] then off[#off + 1] = m.name end end
    if not C.remote_set_off(table.concat(off, "\n") .. "\n") then return "could not save the switches" end
    C.remote_relaunch()
    return "relaunching with " .. #names .. " mod(s) on"
  end
  local function finish(text)
    cur = nil
    return text
  end
  local R = {}
  function R.step()
    if not cur then return nil end
    local r = table.pack(coroutine.resume(cur.co))
    if not r[1] then
      return finish(table.concat(cur.out, "\n") .. (#cur.out > 0 and "\n" or "") .. "ERROR " ..
        debug.traceback(cur.co, tostring(r[2])))
    end
    if coroutine.status(cur.co) ~= "dead" then
      cur.frames = cur.frames + 1
      if cur.frames > 60 * 120 then return finish(table.concat(cur.out, "\n") .. "\nERROR timeout (2 min)") end
      return nil
    end
    local out = cur.out
    if r.n > 1 then out[#out + 1] = "= " .. fmt(table.unpack(r, 2, r.n)) end
    return finish(table.concat(out, "\n"))
  end
  function R.start(src)
    local out = {}
    env.print = function(...) out[#out + 1] = fmt(...) end
    local f = load("return " .. src, "=remote", "t", env)  -- an expression: its value comes back
    local e
    if not f then f, e = load(src, "=remote", "t", env) end
    if not f then return "ERROR " .. tostring(e) end
    cur = {co = coroutine.create(f), out = out, frames = 0}
    return R.step()
  end
  function R.busy() return cur ~= nil end
  debug.getregistry()["isaaclua.remote"] = R
end)(C)
