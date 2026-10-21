-- mame_snap.lua - capturas de MAME para la comparación píxel a píxel del hito 3, con la misma secuencia de
-- entradas y la misma numeración de cuadros que tools/mame_blind.lua y build/sf2_blind.
--
--   SF2_SNAP_FRAMES=100,650,1600 SF2_INPUTS=tools/inputs_sf2.txt SF2_FRAMES=1700 \
--     tools/mame.sh sf2 -video none -sound none -nothrottle \
--       -keyboardprovider none -mouseprovider none -joystickprovider none -lightgunprovider none \
--       -snapshot_directory build/video/mame -str 60 -autoboot_script tools/mame_snap.lua
--
-- Variables de entorno:
--   SF2_SNAP_FRAMES  lista "a,b,c" de cuadros a capturar (cuadro_N.png en el directorio de capturas)
--   SF2_INPUTS       secuencia de entradas "desde hasta tecla" (la misma que el runtime)
--   SF2_FRAMES       cuadros tras los que MAME termina (0 = hasta -str)
--   SF2_DUMP_DIR     si se define, para cada cuadro capturado se vuelcan además:
--                      gfx_N.bin   RAM gráfica (0x30000 bytes) en el punto de muestra
--                      regs_N.bin  registros CPS-A (0x40 bytes) y CPS-B (0x40 bytes), big-endian, tal como
--                                  estaban al renderizar el cuadro (antes de que el manejador de IRQ2 los toque)
--                      obj_N.bin   tabla de objetos (0x800 bytes) usada para dibujar el cuadro (la copiada en
--                                  el VBlank anterior, m_buffered_obj)
--                      pal_N.bin   los 0xC00 pens de la paleta de MAME como 0x00RRGGBB big-endian
--
-- Cuadro N = N-ésima ejecución del manejador de IRQ2 (tap de lectura sobre 0x800172 con CURPC = 0xAD0, igual
-- que mame_blind.lua).  La captura se toma dentro del tap: screen:snapshot() guarda el último cuadro
-- compuesto por MAME, que es el que vblank_begin renderizó justo antes de la IRQ2 (video_manager::
-- frame_update va antes de las llamadas de VBlank y de la IRQ), con los registros escritos por el manejador
-- del cuadro anterior y la tabla de objetos copiada en el VBlank anterior.
--
-- Los registros y la tabla de objetos se leen en el callback de "frame done" (emu.register_frame_done), que
-- MAME ejecuta dentro de frame_update justo después de renderizar y antes de copiar la tabla de objetos y
-- de entregar la IRQ2: ahí los registros son los del cuadro recién dibujado (el manejador escribe la base
-- OBJ y OTHER en 0xAC0/0xAC8, antes del punto de muestra) y la tabla de objetos en la base OBJ es la que se
-- va a copiar para el cuadro siguiente, así que la usada por este cuadro es la leída un "frame done" antes.

local cpu = manager.machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local ioport = manager.machine.ioport
local screen = manager.machine.screens[":screen"]
local shares = manager.machine.memory.shares
local cps_a = shares[":cps_a_regs"]
local cps_b = shares[":cps_b_regs"]
local palette = screen.palette

local SAMPLE_PC = 0xad0
local inputs_path = os.getenv("SF2_INPUTS")
local max_frames = tonumber(os.getenv("SF2_FRAMES") or "0")
local dump_dir = os.getenv("SF2_DUMP_DIR")
local snap_frames = {}
local nsnap = 0
for s in string.gmatch(os.getenv("SF2_SNAP_FRAMES") or "", "[^,]+") do snap_frames[tonumber(s)] = true; nsnap = nsnap + 1 end

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
  print(string.format("mame_snap.lua: %d eventos de entrada de %s", #events, inputs_path))
end

-- aplica las entradas que corresponden al cuadro `frame` (se llama en la muestra del cuadro anterior)
local function apply_inputs(frame)
  for _, e in ipairs(events) do
    local want = frame >= e.from and frame <= e.to
    if want ~= e.state then
      if want then e.field:set_value(1) else e.field:clear_value() end
      e.state = want
    end
  end
end

local function be16(v) return string.char((v >> 8) & 0xff, v & 0xff) end
local function be32(v) return string.char((v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff) end

-- estado del último "frame done": registros y tabla de objetos del cuadro recién dibujado
S = { frame = 0, other = 0, done = false, regs = nil, obj_used = nil, obj_next = nil, snaps = 0 }   -- global: mantiene vivos los taps

local function read_regs()
  local t = {}
  for i = 0, 0x3e, 2 do t[#t+1] = be16(cps_a:read_u16(i)) end
  for i = 0, 0x3e, 2 do t[#t+1] = be16(cps_b:read_u16(i)) end
  return table.concat(t)
end

local function read_obj()
  local base = (cps_a:read_u16(0) * 256) & ~0x7ff & 0x3ffff
  return space:read_range(0x900000 + base, 0x900000 + base + 0x7ff, 8)
end

local function read_pal()
  local t = {}
  for i = 0, 0xbff do t[#t+1] = be32(palette:pen_color(i) & 0xffffff) end
  return table.concat(t)
end

if dump_dir then
  emu.register_frame_done(function()
    local ok, err = pcall(function()
      S.regs = read_regs()
      S.obj_used = S.obj_next
      S.obj_next = read_obj()
    end)
    if not ok then print("mame_snap.lua: ERROR en frame_done " .. tostring(err)) end
  end)
end

local function dump(frame)
  local function save(name, data)
    local f = assert(io.open(string.format("%s/%s_%d.bin", dump_dir, name, frame), "wb")); f:write(data); f:close()
  end
  save("gfx", space:read_range(0x900000, 0x92ffff, 8))
  if S.regs then save("regs", S.regs) end
  if S.obj_used then save("obj", S.obj_used) end
  save("pal", read_pal())
end

S.tap = space:install_read_tap(0x800172, 0x800173, "sf2_irq2", function(offset, data, mask)
  local ok, err = pcall(function()
    if cpu.state["CURPC"].value ~= SAMPLE_PC then S.other = S.other + 1; return end
    if S.done then return end
    S.frame = S.frame + 1
    if snap_frames[S.frame] then
      screen:snapshot(string.format("cuadro_%d.png", S.frame))
      if dump_dir then dump(S.frame) end
      S.snaps = S.snaps + 1
      print(string.format("mame_snap.lua: captura del cuadro %d (%d de %d)", S.frame, S.snaps, nsnap))
    end
    apply_inputs(S.frame + 1)
    if max_frames > 0 and S.frame >= max_frames then
      S.done = true
      print(string.format("mame_snap.lua: %d cuadros, %d capturas (%d lecturas de 0x800172 fuera del manejador)",
        S.frame, S.snaps, S.other))
      manager.machine:exit()
    end
  end)
  if not ok then
    print("mame_snap.lua: ERROR " .. tostring(err))
    S.done = true
    manager.machine:exit()
  end
end)

apply_inputs(1)
print(string.format("mame_snap.lua: %d capturas pedidas, %s cuadros", nsnap, max_frames > 0 and tostring(max_frames) or "hasta -str"))
