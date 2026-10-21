-- mame_blind.lua - produce desde MAME el mismo resumen por cuadro que build/sf2_blind (hito 2) e inyecta
-- la misma secuencia de entradas, para compararlos con tools/compare_summaries.py.
--
--   SF2_SUMMARY=build/summary_mame.txt SF2_INPUTS=tools/inputs_sf2.txt SF2_FRAMES=12000 \
--     tools/mame.sh sf2 -video none -sound none -nothrottle \
--       -keyboardprovider none -mouseprovider none -joystickprovider none -lightgunprovider none \
--       -str 300 -autoboot_script tools/mame_blind.lua
--
-- Los -*provider none evitan que el teclado, el ratón o un mando del anfitrión lleguen al juego: sin
-- ventana MAME sigue registrando el teclado del sistema y en macOS el proceso recibe el foco.
--
-- Variables de entorno:
--   SF2_SUMMARY     archivo de salida (por defecto sf2_summary_mame.txt)
--   SF2_INPUTS      secuencia de entradas "desde hasta tecla" (opcional)
--   SF2_FRAMES      cuadros (entregas de IRQ2) tras los que MAME termina (0 = hasta -str)
--   SF2_DETAIL=1    añade los hashes por bloque (wb=, gb=)
--   SF2_DUMP_FRAME  cuadro cuya RAM se vuelca a SF2_DUMP_DIR/wram_N.bin y gfx_N.bin
--   SF2_SNAP_FRAMES lista "a,b,c" de cuadros en los que guardar una captura PNG (en el directorio de capturas)
--   SF2_DIPS        dipswitches "AA,BB,CC" en hexadecimal (DSWA, DSWB, DSWC; hito 6).  MAME guarda los dipswitches
--                   en su cfg al salir: con SF2_DIPS hay que pasar un -cfg_directory propio para no tocar el de
--                   external/mame-data (tools/coverage.py lo hace)
--   SF2_CMDS        archivo donde anotar los comandos de sonido del 68000 (como tools/mame_sound_cmds.lua; hito 6)
--   SF2_TRACE       traza del 68000 con el ciclo total de cada instrucción (debugger, necesita -debug -debugger none)
--   SF2_TRACE_FRAMES "desde:hasta": cuadros que se trazan (hito 6, evaluación del reloj frente a build/sf2_blind
--                   --trace-cycles; la traza empieza en la muestra del cuadro desde-1 y termina en la de hasta)
--
-- Cuadro = ejecución del manejador de IRQ2, detectada con un tap de lectura sobre 0x800172 con CURPC =
-- 0xAD0 (la primera lectura de E/S del manejador).  Los taps deben guardarse en una variable global: si el
-- objeto devuelto por install_read_tap se recoge como basura, el tap desaparece.
--
-- Entradas: las de un cuadro N se fijan con field:set_value() durante la muestra del cuadro N-1, porque
-- los puertos de MAME se actualizan al empezar cada VBlank, justo antes de la IRQ2 (comprobado: un
-- set_value en el cuadro N se lee en N+1).  El bloqueo de monedas lo aplica MAME; el runtime lo imita.

local cpu = manager.machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local ioport = manager.machine.ioport
local screen = manager.machine.screens[":screen"]

local SAMPLE_PC = 0xad0
local VOL1_LO, VOL1_HI = 0xff0000, 0xff0a50
local VOL2_LO, VOL2_HI = 0xffff00, 0x1000000

local out_path = os.getenv("SF2_SUMMARY") or "sf2_summary_mame.txt"
local inputs_path = os.getenv("SF2_INPUTS")
local max_frames = tonumber(os.getenv("SF2_FRAMES") or "0")
local detail = os.getenv("SF2_DETAIL") == "1"
local dump_frame = tonumber(os.getenv("SF2_DUMP_FRAME") or "0")
local dump_dir = os.getenv("SF2_DUMP_DIR") or "."
local snap_frames = {}
for s in string.gmatch(os.getenv("SF2_SNAP_FRAMES") or "", "[^,]+") do snap_frames[tonumber(s)] = true end

local out = assert(io.open(out_path, "w"))

-- dipswitches (hito 6): todos los campos de DSWA/DSWB/DSWC, incluidos los "sin uso", con los bits pedidos.
-- Un cambio de user_value solo llega al puerto en la siguiente actualización de entradas (el primer VBlank), y el
-- 68000 ya corre antes; para que valgan desde el encendido, como en la placa, tools/coverage.py hace antes una
-- pasada corta (SF2_FRAMES=1) con un -cfg_directory propio: MAME guarda los dipswitches en ese cfg al salir y la
-- pasada buena los carga al arrancar.  Aquí se comprueba en el cuadro 1 que el puerto lee lo pedido y se dice si
-- ya venían del cfg.
local DIP_TAGS = {":DSWA", ":DSWB", ":DSWC"}
local dips_want = nil
local dips_env = os.getenv("SF2_DIPS")
if dips_env and dips_env ~= "" then
  dips_want = {}
  for x in string.gmatch(dips_env, "[^,%s]+") do dips_want[#dips_want+1] = tonumber(x, 16) end
  if #dips_want ~= 3 then error("SF2_DIPS: se esperan tres bytes en hexadecimal, p. ej. ff,fc,9f") end
  local from_cfg = true
  for i, tag in ipairs(DIP_TAGS) do
    local port = ioport.ports[tag]
    if not port then error("sin puerto " .. tag) end
    if (port:read() & 0xff) ~= dips_want[i] then from_cfg = false end
    for _, field in pairs(port.fields) do
      field.user_value = dips_want[i] & field.mask
    end
  end
  print(string.format("mame_blind.lua: dipswitches %s (%s)", dips_env,
    from_cfg and "ya cargados del cfg al encender" or "puestos ahora: valen desde el primer VBlank"))
end

local trace_path = os.getenv("SF2_TRACE")
local trace_from, trace_to = 0, -1
if trace_path then
  local a, b = string.match(os.getenv("SF2_TRACE_FRAMES") or "", "(%d+):(%d+)")
  trace_from, trace_to = tonumber(a or "1"), tonumber(b or "1")
  if not manager.machine.debugger then error("SF2_TRACE necesita -debug -debugger none") end
  manager.machine.debugger.execution_state = "run"
  if trace_from <= 1 then          -- desde el reset
    manager.machine.debugger:command(string.format('trace %s,maincpu,noloop,{tracelog "%%X ",totalcycles}', trace_path))
  end
end
local function trace_at(frame)
  if not trace_path then return end
  local dbg = manager.machine.debugger
  if frame == trace_from - 1 then
    dbg:command(string.format('trace %s,maincpu,noloop,{tracelog "%%X ",totalcycles}', trace_path))
    print("mame_blind.lua: traza con ciclos en " .. trace_path)
  elseif frame == trace_to then
    dbg:command("trace off,maincpu")
  end
end

local function check_dips()
  if not dips_want then return end
  for i, tag in ipairs(DIP_TAGS) do
    local got = ioport.ports[tag]:read() & 0xff
    if got ~= dips_want[i] then
      error(string.format("%s: pedido %02x, MAME lee %02x", tag, dips_want[i], got))
    end
  end
end

-- comandos de sonido (hito 6): las mismas reglas y el mismo formato que tools/mame_sound_cmds.lua
local cmds_path = os.getenv("SF2_CMDS")
local cmds = cmds_path and assert(io.open(cmds_path, "w")) or nil

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
  print(string.format("mame_blind.lua: %d eventos de entrada de %s", #events, inputs_path))
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

-- FNV-1a sobre palabras de 16 bits big-endian, 8 palabras por iteración
local function fnv(s, h)
  local n = #s
  local i = 1
  while i + 15 <= n do
    local b1,b2,b3,b4,b5,b6,b7,b8,b9,b10,b11,b12,b13,b14,b15,b16 = string.byte(s, i, i+15)
    h = ((h ~ ((b1<<8)|b2)) * 0x01000193) & 0xffffffff
    h = ((h ~ ((b3<<8)|b4)) * 0x01000193) & 0xffffffff
    h = ((h ~ ((b5<<8)|b6)) * 0x01000193) & 0xffffffff
    h = ((h ~ ((b7<<8)|b8)) * 0x01000193) & 0xffffffff
    h = ((h ~ ((b9<<8)|b10)) * 0x01000193) & 0xffffffff
    h = ((h ~ ((b11<<8)|b12)) * 0x01000193) & 0xffffffff
    h = ((h ~ ((b13<<8)|b14)) * 0x01000193) & 0xffffffff
    h = ((h ~ ((b15<<8)|b16)) * 0x01000193) & 0xffffffff
    i = i + 16
  end
  while i + 1 <= n do
    local b1, b2 = string.byte(s, i, i+1)
    h = ((h ~ ((b1<<8)|b2)) * 0x01000193) & 0xffffffff
    i = i + 2
  end
  return h
end
local FNV_INIT = 0x811c9dc5

-- hash de [lo, hi) de la RAM de trabajo (string `w` empieza en 0xff0000) saltando las zonas volátiles
local function hash_wram_range(w, lo, hi, h)
  local cuts = { {VOL1_LO, VOL1_HI}, {VOL2_LO, VOL2_HI} }
  local a = lo
  while a < hi do
    local nxt, incut = hi, false
    for _, c in ipairs(cuts) do
      if a >= c[1] and a < c[2] then incut = true; a = c[2]; break end
      if c[1] > a and c[1] < nxt then nxt = c[1] end
    end
    if not incut then
      h = fnv(string.sub(w, a - 0xff0000 + 1, nxt - 0xff0000), h)
      a = nxt
    end
  end
  return h
end

local function sample(frame)
  local sp = cpu.state["SP"].value
  local pushed_sr = space:read_u16(sp + 60)
  local pushed_pc = space:read_u32(sp + 62)
  local w = space:read_range(0xff0000, 0xffffff, 8)
  local g = space:read_range(0x900000, 0x92ffff, 8)
  if dump_frame > 0 and frame == dump_frame then
    local f = assert(io.open(string.format("%s/wram_%d.bin", dump_dir, frame), "wb")); f:write(w); f:close()
    f = assert(io.open(string.format("%s/gfx_%d.bin", dump_dir, frame), "wb")); f:write(g); f:close()
    print(string.format("mame_blind.lua: RAM del cuadro %d volcada en %s", frame, dump_dir))
  end
  local d, a = {}, {}
  for i = 0, 7 do d[#d+1] = string.format("%08x", cpu.state["D" .. i].value) end
  for i = 0, 6 do a[#a+1] = string.format("%08x", cpu.state["A" .. i].value) end
  a[#a+1] = string.format("%08x", sp)
  local wh = hash_wram_range(w, 0xff0000, 0x1000000, FNV_INIT)
  local vh = fnv(string.sub(w, VOL1_LO - 0xff0000 + 1, VOL1_HI - 0xff0000), FNV_INIT)
  vh = fnv(string.sub(w, VOL2_LO - 0xff0000 + 1, VOL2_HI - 0xff0000), vh)
  local line = string.format("f=%d pc=%06x sr=%04x d=%s a=%s in=%02x,%04x,%02x fc=%02x w=%08x v=%08x g=%08x",
    frame, pushed_pc & 0xffffff, pushed_sr, table.concat(d, ","), table.concat(a, ","),
    ioport.ports[":IN0"]:read() & 0xff, ioport.ports[":IN1"]:read() & 0xffff, ioport.ports[":IN2"]:read() & 0xff,
    string.byte(w, 0x801c + 1), wh, vh, fnv(g, FNV_INIT))
  if detail then
    local wb, gb = {}, {}
    for b = 0, 15 do
      local lo = 0xff0000 + b * 0x1000
      wb[#wb+1] = string.format("%08x", hash_wram_range(w, lo, lo + 0x1000, FNV_INIT))
    end
    for b = 0, 11 do
      gb[#gb+1] = string.format("%08x", fnv(string.sub(g, b * 0x4000 + 1, (b + 1) * 0x4000), FNV_INIT))
    end
    line = line .. " wb=" .. table.concat(wb, ",") .. " gb=" .. table.concat(gb, ",")
  end
  out:write(line, "\n")
end

B = { frame = 0, other = 0, done = false, ncmds = 0 }   -- global: mantiene vivos los taps

local function close_cmds()
  if cmds then
    cmds:write(string.format("# cuadros %d\n", B.frame))
    cmds:close()
    cmds = nil
  end
end

if cmds then
  B.tap_latch = space:install_write_tap(0x800180, 0x80018f, "sf2_blind_latch", function(offset, data, mask)
    local ok, err = pcall(function()
      if B.done or not cmds then return end
      local k = (offset >= 0x800188) and 2 or 1
      local v
      if (mask & 0x00ff) ~= 0 then v = data & 0xff
      elseif k == 1 then v = (data >> 8) & 0xff
      else return end
      B.ncmds = B.ncmds + 1
      cmds:write(string.format("%d %d %02x\n", B.frame, k, v))
    end)
    if not ok then print("mame_blind.lua: ERROR (latch) " .. tostring(err)) end
  end)
end
B.tap = space:install_read_tap(0x800172, 0x800173, "sf2_irq2", function(offset, data, mask)
  local ok, err = pcall(function()
    if cpu.state["CURPC"].value ~= SAMPLE_PC then B.other = B.other + 1; return end
    if B.done then return end
    B.frame = B.frame + 1
    if B.frame == 1 then check_dips() end
    trace_at(B.frame)
    sample(B.frame)
    if snap_frames[B.frame] then screen:snapshot(string.format("cuadro_%d.png", B.frame)) end
    apply_inputs(B.frame + 1)
    if max_frames > 0 and B.frame >= max_frames then
      B.done = true
      out:close()
      close_cmds()
      print(string.format("mame_blind.lua: %d cuadros escritos en %s (%d lecturas de 0x800172 fuera del manejador)",
        B.frame, out_path, B.other))
      manager.machine:exit()
    end
  end)
  if not ok then
    print("mame_blind.lua: ERROR " .. tostring(err))
    B.done = true
    out:close()
    close_cmds()
    manager.machine:exit()
  end
end)

B.stop_sub = emu.add_machine_stop_notifier(function()
  if not B.done then
    out:close()
    close_cmds()
    print(string.format("mame_blind.lua: fin por -str con %d cuadros escritos en %s", B.frame, out_path))
  end
end)

apply_inputs(1)
print(string.format("mame_blind.lua: resumen en %s, %s cuadros", out_path, max_frames > 0 and tostring(max_frames) or "hasta -str"))
