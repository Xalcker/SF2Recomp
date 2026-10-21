-- Activa una traza del 68000 (instrucción a instrucción, con detección de
-- bucles) y deja correr la emulación. Pensado para ejecuciones sin ventana:
--   SF2_TRACE=/ruta/traza.log tools/mame.sh sf2 -video none -sound none \
--       -nothrottle -str 5 -debug -debugger none -autoboot_script tools/mame_trace.lua
-- Requiere -debug. Con -debugger none el archivo de -debugscript nunca se
-- procesa (MAME reanuda antes de leerlo), por eso se usa Lua.
local out = os.getenv("SF2_TRACE") or "sf2_trace.log"
local dbg = manager.machine.debugger
if dbg == nil then
  print("mame_trace.lua: sin depurador; añade -debug -debugger none")
else
  dbg:command("trace " .. out .. ",0")
  dbg.execution_state = "run"
  print("mame_trace.lua: traza activada en " .. out)
end
