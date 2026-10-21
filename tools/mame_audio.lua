-- mame_audio.lua - referencia de sonido del hito 4: registra desde MAME todo lo que entra y sale del
-- subsistema de sonido de la CPS1 con la misma secuencia de entradas y la misma numeración de cuadros que
-- tools/mame_blind.lua y build/sf2_blind.
--
--   SF2_AUDIO_DIR=build/audio/mame SF2_INPUTS=tools/inputs_sf2.txt SF2_FRAMES=12000 \
--     tools/mame.sh sf2 -video none -sound none -nothrottle \
--       -keyboardprovider none -mouseprovider none -joystickprovider none -lightgunprovider none \
--       -str 240 -autoboot_script tools/mame_audio.lua
--
-- Variables de entorno:
--   SF2_AUDIO_DIR  directorio de salida (debe existir)
--   SF2_INPUTS     secuencia de entradas "desde hasta tecla" (la misma que el runtime)
--   SF2_FRAMES     cuadros (entregas de IRQ2) tras los que MAME termina (0 = hasta -str)
--
-- Salida en SF2_AUDIO_DIR (mismo formato que escribe build/sf2_blind --audio-dir):
--   events.txt  una línea por suceso, en orden de tiempo de emulación:
--                 F n t68              cuadro n (punto de muestra 0xAD0 de la IRQ2), t68 = ciclo del 68000 (10 MHz)
--                 L k v t68 tz         escritura del 68000 en el latch k (1 = 0x800181, 2 = 0x800189), valor v
--                 W a v tz             escritura del Z80 en 0xF000-0xF00F (a = dirección, v = valor)
--                 R a v tz             lectura del Z80 en 0xF000-0xF00F y el valor leído
--               tz = ciclo del Z80 (3,579545 MHz) contado desde el arranque de la máquina.  Todos los
--               números en decimal salvo a y v (hexadecimal).
--   ym.f32      salida del YM2151 (dos canales intercalados, L R), float32 little-endian, 55.930 Hz
--   oki.f32     salida del OKI MSM6295, float32, 7.575 Hz (pin 7 alto)
--   mono.f32    entrada del altavoz "mono" (la mezcla que MAME escribe con -wavwrite), float32, 48.000 Hz
--   chunks.txt  muestras entregadas por cada actualización de sonido: "ym oki mono" por línea
--
-- Tiempo: manager.machine.time dentro de un tap es el tiempo local de la CPU que hace el acceso
-- (device_scheduler::time), así que tz es el ciclo exacto del ciclo M del acceso.  Se redondea al ciclo
-- más próximo porque MAME acumula el tiempo con attosegundos por ciclo truncados (1e18 / 3579545), que
-- quedan un poco por debajo del valor exacto.
--
-- Salida de los chips: emu.register_sound_update recibe, en cada actualización periódica de sonido (50 Hz),
-- las muestras nuevas de los dispositivos con hook = true: los flujos de salida del YM2151 y del OKI tal como
-- los generan sus núcleos (antes de remuestrear) y el búfer de entrada del altavoz ya mezclado a 48 kHz.

local cpu = manager.machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local audiocpu = manager.machine.devices[":audiocpu"]
local aspace = audiocpu.spaces["program"]
local ioport = manager.machine.ioport

local SAMPLE_PC = 0xad0
local Z80_HZ = 3579545
local M68K_HZ = 10000000

local out_dir = assert(os.getenv("SF2_AUDIO_DIR"), "falta SF2_AUDIO_DIR")
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

-- tiempo de emulación actual en ciclos de un reloj de `hz` (redondeado, ver la cabecera)
local function ticks(hz)
  local t = manager.machine.time
  return t.seconds * hz + math.floor(t.attoseconds / 1e18 * hz + 0.5)
end

local ev = assert(io.open(out_dir .. "/events.txt", "w"))
local f_ym = assert(io.open(out_dir .. "/ym.f32", "wb"))
local f_oki = assert(io.open(out_dir .. "/oki.f32", "wb"))
local f_mono = assert(io.open(out_dir .. "/mono.f32", "wb"))
local f_chunks = assert(io.open(out_dir .. "/chunks.txt", "w"))

-- global: mantiene vivos los taps y los suscriptores
A = { frame = 0, done = false, nl = 0, nw = 0, nr = 0, samples = { ym = 0, oki = 0, mono = 0 } }

local function finish(why)
  if A.done then return end
  A.done = true
  ev:close(); f_ym:close(); f_oki:close(); f_mono:close(); f_chunks:close()
  print(string.format("mame_audio.lua: %s; %d cuadros, %d escrituras de latch, %d escrituras y %d lecturas del Z80, " ..
    "muestras ym %d, oki %d, mono %d, en %s", why, A.frame, A.nl, A.nw, A.nr, A.samples.ym, A.samples.oki,
    A.samples.mono, out_dir))
end

local function guarded(fn)
  return function(...)
    local ok, err = pcall(fn, ...)
    if not ok then
      print("mame_audio.lua: ERROR " .. tostring(err))
      finish("error")
      manager.machine:exit()
    end
  end
end

-- cuadros: mismo criterio que mame_blind.lua
A.tap_frame = space:install_read_tap(0x800172, 0x800173, "sf2_audio_irq2", guarded(function(offset, data, mask)
  if A.done or cpu.state["CURPC"].value ~= SAMPLE_PC then return end
  A.frame = A.frame + 1
  ev:write(string.format("F %d %d\n", A.frame, ticks(M68K_HZ)))
  apply_inputs(A.frame + 1)
  if max_frames > 0 and A.frame >= max_frames then
    finish("fin")
    manager.machine:exit()
  end
end))

-- latches de sonido escritos por el 68000 (byte bajo de 0x800180-0x80018F)
A.tap_latch = space:install_write_tap(0x800180, 0x80018f, "sf2_audio_latch", guarded(function(offset, data, mask)
  if A.done then return end
  local k = (offset >= 0x800188) and 2 or 1
  local v
  if (mask & 0x00ff) ~= 0 then v = data & 0xff
  elseif k == 1 then v = (data >> 8) & 0xff      -- cps1_soundlatch_w acepta también el byte alto
  else return end                                -- cps1_soundlatch2_w ignora el byte alto
  A.nl = A.nl + 1
  ev:write(string.format("L %d %02x %d %d\n", k, v, ticks(M68K_HZ), ticks(Z80_HZ)))
end))

-- accesos del Z80 a la zona de E/S
A.tap_zw = aspace:install_write_tap(0xf000, 0xf00f, "sf2_audio_zw", guarded(function(offset, data, mask)
  if A.done then return end
  A.nw = A.nw + 1
  ev:write(string.format("W %04x %02x %d\n", offset, data & 0xff, ticks(Z80_HZ)))
end))
A.tap_zr = aspace:install_read_tap(0xf000, 0xf00f, "sf2_audio_zr", guarded(function(offset, data, mask)
  if A.done then return end
  A.nr = A.nr + 1
  ev:write(string.format("R %04x %02x %d\n", offset, data & 0xff, ticks(Z80_HZ)))
end))

-- salida de los chips y del altavoz
local sounds = manager.machine.sounds
for _, tag in ipairs({ ":2151", ":oki", ":mono" }) do
  local s = sounds[tag]
  if not s then error("no existe el dispositivo de sonido " .. tag) end
  s.hook = true
end

local function pack_floats(f, ch)
  local n = #ch
  local i = 1
  while i <= n do
    local j = math.min(i + 999, n)
    f:write(string.pack("<" .. string.rep("f", j - i + 1), table.unpack(ch, i, j)))
    i = j + 1
  end
  return n
end

A.sub_sound = emu.register_sound_update(guarded(function(data)
  if A.done then return end
  local ym, oki, mono = data[":2151"], data[":oki"], data[":mono"]
  local nym, noki, nmono = 0, 0, 0
  if ym then
    local l, r = ym[1], ym[2]
    nym = #l
    local inter = {}
    for i = 1, nym do inter[2 * i - 1] = l[i]; inter[2 * i] = r[i] end
    pack_floats(f_ym, inter)
  end
  if oki then noki = pack_floats(f_oki, oki[1]) end
  if mono then nmono = pack_floats(f_mono, mono[1]) end
  A.samples.ym = A.samples.ym + nym
  A.samples.oki = A.samples.oki + noki
  A.samples.mono = A.samples.mono + nmono
  f_chunks:write(string.format("%d %d %d\n", nym, noki, nmono))
end))

A.stop_sub = emu.add_machine_stop_notifier(function() finish("fin por -str") end)

apply_inputs(1)
print(string.format("mame_audio.lua: %d eventos de entrada, salida en %s, %s cuadros", #events, out_dir,
  max_frames > 0 and tostring(max_frames) or "hasta -str"))
