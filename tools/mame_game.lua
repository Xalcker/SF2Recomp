-- tools/mame_game.lua - lo que los scripts de MAME necesitan saber de cada variante (D63, docs/VARIANTES.md).
-- La variante sale de SF2_GAME (por omisión sf2) y debe ser la misma que se lanza en MAME (tools/mame.sh <set>).
--
-- Punto de muestra = primer acceso de E/S del manejador de la IRQ2, el mismo instante que GameDef.sample_pc en el núcleo
-- (runtime/game.c): en sf2 la lectura del ID del CPS-B (0x800172) en 0xAD0; en sf2ce, cuyo manejador no lee el CPS-B, la
-- escritura de la base de OBJ (0x800100) en 0x5DA; en sf2hf, cuyo manejador guarda los registros después de sus primeras escrituras,
-- la primera escritura tras el movem (0x800122 en 0x1D264).  El tap se dispara antes de que la instrucción termine su acceso y en ninguna
-- de las dos cambia registros ni RAM antes de él, así que el estado coincide con el del núcleo justo antes de ejecutarla.
--
--   local G = dofile(<carpeta de este archivo> .. "mame_game.lua")
--   T = G.install_sample_tap(space, "nombre", function() ... end)   -- guardar T en una global
local GAMES = {
  sf2   = { sample_pc = 0x000ad0, addr = 0x800172, write = false, cpu_hz = 10000000 },
  sf2ce = { sample_pc = 0x0005da, addr = 0x800100, write = true,  cpu_hz = 12000000 },
  sf2hf = { sample_pc = 0x01d264, addr = 0x800122, write = true,  cpu_hz = 12000000 },
}

local id = os.getenv("SF2_GAME") or "sf2"
local g = GAMES[id]
if not g then error("SF2_GAME: variante desconocida '" .. id .. "'") end

local M = { id = id, sample_pc = g.sample_pc, sample_addr = g.addr, cpu_hz = g.cpu_hz }

-- tap sobre la dirección del punto de muestra; `fn(at_sample)` recibe true si el acceso es el del manejador (CURPC = punto de
-- muestra) y false si es otro acceso a esa dirección
function M.install_sample_tap(space, name, fn)
  local cpu = manager.machine.devices[":maincpu"]
  local cb = function(offset, data, mask)
    fn(cpu.state["CURPC"].value == g.sample_pc)
  end
  if g.write then
    return space:install_write_tap(g.addr, g.addr + 1, name, cb)
  end
  return space:install_read_tap(g.addr, g.addr + 1, name, cb)
end

return M
