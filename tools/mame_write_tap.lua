-- tools/mame_write_tap.lua - quién escribe en la RAM del juego (D76): para localizar la rutina de la cámara, las paredes, etc. en
-- una variante nueva.  Un tap de escritura por rango y, por cada (PC, dirección), cuántas veces escribe y los primeros valores.
--
--   SF2_GAME=sfzch SF2_TAPS=ff8290-ff8295,ff8400-ff8401 SF2_TAPOUT=/tmp/taps.txt SF2_INPUTS=tools/inputs_sf2.txt SF2_FRAMES=3400 \
--     SF2_FROM=1800 tools/mame.sh sfzch -video none -sound none -nothrottle -autoboot_script tools/mame_write_tap.lua
--
--   SF2_TAPS   rangos de direcciones hexadecimales "ini-fin" separados por comas (fin incluido)
--   SF2_FROM   primer cuadro que se registra (por omisión 1)
-- Salida: "PC direccion veces valores..." ordenado por PC; la última línea dice hasta qué cuadro llegó.

local cpu = manager.machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local ioport = manager.machine.ioport

local G = dofile((debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "./") .. "mame_game.lua")   -- variante (SF2_GAME)
local SAMPLE_PC = G.sample_pc

local out_path = assert(os.getenv("SF2_TAPOUT"), "falta SF2_TAPOUT")
local inputs_path = os.getenv("SF2_INPUTS")
local max_frames = tonumber(os.getenv("SF2_FRAMES") or "0")

-- teclas (mismos nombres que runtime/cps1.c)
local KEYS = {
  coin1 = {":IN0", 0x01}, coin2 = {":IN0", 0x02}, service = {":IN0", 0x04},
  start1 = {":IN0", 0x10}, start2 = {":IN0", 0x20}, test = {":IN0", 0x40},
  p1_right = {":IN1", 0x0001}, p1_left = {":IN1", 0x0002}, p1_down = {":IN1", 0x0004}, p1_up = {":IN1", 0x0008},
  p1_lp = {":IN1", 0x0010}, p1_mp = {":IN1", 0x0020}, p1_hp = {":IN1", 0x0040},
  p2_right = {":IN1", 0x0100}, p2_left = {":IN1", 0x0200}, p2_down = {":IN1", 0x0400}, p2_up = {":IN1", 0x0800},
  p2_lp = {":IN1", 0x1000}, p2_mp = {":IN1", 0x2000}, p2_hp = {":IN1", 0x4000},
  p1_lk = {":IN2", 0x01}, p1_mk = {":IN2", 0x02}, p1_hk = {":IN2", 0x04},
  p2_lk = {":IN2", 0x10}, p2_mk = {":IN2", 0x20}, p2_hk = {":IN2", 0x40},
}
KEYS = G.keys(KEYS)   -- puertos de la variante (CPS Changer, D71)

local events = {}
if inputs_path then
  local f = io.open(inputs_path, "r")
  if not f then error("no se puede leer " .. inputs_path) end
  local lineno = 0
  for line in f:lines() do
    lineno = lineno + 1
    local body = line:gsub("#.*$", "")
    local from, to, key = body:match("^%s*(%d+)%s+(%d+)%s+(%S+)")
    if from then
      local k = KEYS[key]
      if not k then error(string.format("%s:%d: tecla desconocida '%s'", inputs_path, lineno, key)) end
      local field = ioport.ports[k[1]]:field(k[2])
      if not field then error(string.format("%s:%d: sin campo para %s/%x", inputs_path, lineno, k[1], k[2])) end
      events[#events+1] = { from = tonumber(from), to = tonumber(to), field = field, key = key, state = false }
    elseif body:match("%S") then
      error(string.format("%s:%d: formato esperado 'desde hasta tecla'", inputs_path, lineno))
    end
  end
  f:close()
end

local function apply_inputs(frame)
  for _, e in ipairs(events) do
    local want = frame >= e.from and frame <= e.to
    if want ~= e.state then
      if want then e.field:set_value(1) else e.field:clear_value() end
      e.state = want
    end
  end
end

local out = assert(io.open(out_path, "w"))

-- global: mantiene vivos los taps y los suscriptores
C = { frame = 0, done = false, n = 0 }

local dump
local function finish(why)
  if C.done then return end
  C.done = true
  if dump then dump() end
  out:write(string.format("# cuadros %d\n", C.frame))
  out:close()
  print(string.format("mame_write_tap.lua: %s; %d cuadros en %s", why, C.frame, out_path))
end

local function guarded(fn)
  return function(...)
    local ok, err = pcall(fn, ...)
    if not ok then
      print("mame_write_tap.lua: ERROR " .. tostring(err))
      finish("error")
      manager.machine:exit()
    end
  end
end

-- cuadros: mismo criterio que mame_blind.lua (lectura de 0x800172 en el punto de muestra del manejador)
C.tap_frame = G.install_sample_tap(space, "sf2_cmds_irq2", guarded(function(at_sample)
  if C.done or not at_sample then return end
  C.frame = C.frame + 1
  apply_inputs(C.frame + 1)
  if max_frames > 0 and C.frame >= max_frames then
    finish("fin")
    manager.machine:exit()
  end
end))

local from_frame = tonumber(os.getenv("SF2_FROM") or "1")
local seen = {}
local taps = {}
for lo, hi in (os.getenv("SF2_TAPS") or ""):gmatch("(%x+)-(%x+)") do
  taps[#taps+1] = space:install_write_tap(tonumber(lo, 16), tonumber(hi, 16), "sf2_wtap", guarded(function(offset, data, mask)
    if C.done or C.frame < from_frame then return end
    local pc = cpu.state["CURPC"].value
    local key = string.format("%06X %06X", pc, offset)
    local e = seen[key]
    if not e then e = { n = 0, v = {} }; seen[key] = e end
    e.n = e.n + 1
    if #e.v < 6 then e.v[#e.v+1] = string.format("%04X/%04X", data & 0xffff, mask & 0xffff) end
  end))
end
C.taps = taps

dump = function()
  local keys = {}
  for k in pairs(seen) do keys[#keys+1] = k end
  table.sort(keys)
  for _, k in ipairs(keys) do
    out:write(string.format("%s %d %s\n", k, seen[k].n, table.concat(seen[k].v, " ")))
  end
end

C.stop_sub = emu.add_machine_stop_notifier(function() finish("fin por -str") end)

apply_inputs(1)
print(string.format("mame_write_tap.lua: %d eventos de entrada, %d rangos, salida en %s", #events, #taps, out_path))
