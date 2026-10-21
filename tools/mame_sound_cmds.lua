-- mame_sound_cmds.lua - flujo de comandos de sonido del 68000 en MAME (hito 4, primer nivel de validación):
-- registra, por cuadro, cada valor que el 68000 escribe en los latches de sonido, con la misma secuencia de
-- entradas y la misma numeración de cuadros que tools/mame_blind.lua y build/sf2_blind.
--
--   SF2_CMDS=build/audio/cmds_mame.txt SF2_INPUTS=tools/inputs_sf2.txt SF2_FRAMES=12000 \
--     tools/mame.sh sf2 -video none -sound none -nothrottle \
--       -keyboardprovider none -mouseprovider none -joystickprovider none -lightgunprovider none \
--       -str 240 -autoboot_script tools/mame_sound_cmds.lua
--
-- Variables de entorno:
--   SF2_CMDS     archivo de salida
--   SF2_INPUTS   secuencia de entradas "desde hasta tecla" (la misma que el runtime)
--   SF2_FRAMES   cuadros (entregas de IRQ2) tras los que MAME termina (0 = hasta -str)
--
-- Salida: una línea por escritura, "cuadro latch valor":
--   cuadro  número de cuadro: los puntos de muestra (0xAD0 del manejador de la IRQ2) pasados hasta ese
--           instante, igual que A.frame de mame_audio.lua y mc->sample_frame de runtime/machine.c
--   latch   1 = 0x800181 (comando del 68000 al Z80), 2 = 0x800189 (fade)
--   valor   dos cifras hexadecimales (byte bajo; el alto solo si es lo único que se escribe en el latch 1,
--           como cps1_soundlatch_w)
-- La última línea, "# cuadros N", dice hasta qué punto de muestra llegó la partida: los comandos del cuadro N
-- (después de su punto de muestra) pueden estar incompletos y tools/compare_sound_cmds.py no los compara.
-- Lo mismo escribe `build/sf2_blind --sound-cmds`; tools/compare_sound_cmds.py compara las dos listas.

local cpu = manager.machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local ioport = manager.machine.ioport

local SAMPLE_PC = 0xad0

local out_path = assert(os.getenv("SF2_CMDS"), "falta SF2_CMDS")
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

local out = assert(io.open(out_path, "w"))

-- global: mantiene vivos los taps y los suscriptores
C = { frame = 0, done = false, n = 0 }

local function finish(why)
  if C.done then return end
  C.done = true
  out:write(string.format("# cuadros %d\n", C.frame))
  out:close()
  print(string.format("mame_sound_cmds.lua: %s; %d cuadros, %d comandos en %s", why, C.frame, C.n, out_path))
end

local function guarded(fn)
  return function(...)
    local ok, err = pcall(fn, ...)
    if not ok then
      print("mame_sound_cmds.lua: ERROR " .. tostring(err))
      finish("error")
      manager.machine:exit()
    end
  end
end

-- cuadros: mismo criterio que mame_blind.lua (lectura de 0x800172 en el punto de muestra del manejador)
C.tap_frame = space:install_read_tap(0x800172, 0x800173, "sf2_cmds_irq2", guarded(function(offset, data, mask)
  if C.done or cpu.state["CURPC"].value ~= SAMPLE_PC then return end
  C.frame = C.frame + 1
  apply_inputs(C.frame + 1)
  if max_frames > 0 and C.frame >= max_frames then
    finish("fin")
    manager.machine:exit()
  end
end))

-- latches de sonido (0x800180-0x80018F): mismas reglas que cps1_soundlatch_w y cps1_soundlatch2_w
C.tap_latch = space:install_write_tap(0x800180, 0x80018f, "sf2_cmds_latch", guarded(function(offset, data, mask)
  if C.done then return end
  local k = (offset >= 0x800188) and 2 or 1
  local v
  if (mask & 0x00ff) ~= 0 then v = data & 0xff
  elseif k == 1 then v = (data >> 8) & 0xff
  else return end
  C.n = C.n + 1
  out:write(string.format("%d %d %02x\n", C.frame, k, v))
end))

C.stop_sub = emu.add_machine_stop_notifier(function() finish("fin por -str") end)

apply_inputs(1)
print(string.format("mame_sound_cmds.lua: %d eventos de entrada, salida en %s, %s cuadros", #events, out_path,
  max_frames > 0 and tostring(max_frames) or "hasta -str"))
