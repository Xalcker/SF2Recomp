# 2026-10-19 · Hito 4 · Placa de sonido y planificación (`runtime/sound.c`)

## Pregunta de partida

Cómo encajar el Z80 en la máquina del hito 2 sin cambiar el 68000, y en qué orden exacto ocurren las
cosas cuando dos sucesos caen en el mismo ciclo o en el mismo ciclo M: el resultado tiene que coincidir
con el planificador de MAME, no con una idea razonable de él.

## Qué se hizo

### Un solo reloj y la placa "detrás" del 68000

En la CPS1 el Z80 no puede escribir nada que lea el 68000, así que la placa de sonido no influye en el
programa principal. `Cps1Sound` cuenta el tiempo en ciclos del Z80 desde el arranque (el YM2151 va con
el mismo cristal), y el ciclo c del 68000 es el ciclo floor(c · 3579545 / 10⁷) del Z80; los dos arrancan
en t = 0 como en MAME. Comprobado con los registros: el punto de muestra del cuadro n está en el mismo
ciclo del 68000 en los dos lados con una diferencia de −279 a +237 ciclos (mediana −25), la diferencia de
reloj entre el modelo de Musashi y el núcleo 68000 de MAME, sin deriva.

- `mem.c` llama a un gancho en cada escritura de los latches (`sound_latch_cb`); `harness/machine.c` la pasa
  a `sound_latch_write` con el ciclo del 68000 al final de la instrucción (el código generado suma los
  ciclos al empezarla). La placa la encola.
- En cada VBlank (y al terminar) la máquina llama a `sound_run_until(ciclo del 68000)`: el Z80 ejecuta
  instrucciones hasta alcanzar ese ciclo, procesando antes de cada límite de instrucción y de cada acceso a
  0xF000-0xF00F los sucesos que vencen hasta ese ciclo, en orden: escrituras de latch, sucesos del YM2151
  (temporizadores y escrituras aplazadas) y la actualización periódica de 50 Hz, que es donde se mezcla.
- Antes de cada acceso al YM2151 o al OKI se generan sus muestras hasta floor(t · f) inclusive, como
  `sound_stream::update()` antes de `read`/`write`.

### Tres detalles del planificador de MAME, encontrados por la comparación

1. **Escrituras aplazadas del YM2151 (registro 0x14).** ymfm las pide con `synchronize()`: un temporizador
   que vence en el instante del acceso y que corta la porción de tiempo del Z80 al final del ciclo M (el
   Z80 queda en acceso + 3). Primera versión: aplicar la escritura en acceso + 3. Resultado: los accesos
   del manejador de INT salían 7 ciclos tarde desde el ciclo 234.751. Causa: dentro de un callback,
   `device_scheduler::time()` devuelve el vencimiento del propio temporizador
   (`m_callback_timer_expire_time`), no el tiempo base, así que `emu_timer::adjust` arranca el
   temporizador A desde el ciclo del acceso. Con 3 ciclos más, el vencimiento caía en 234.669 en vez de
   234.666 y la INT se aceptaba tras `cp (hl)`, un límite después. Corregido: se aplican en el ciclo del
   acceso, después de él. Con eso, 10.857 escrituras y 8.809 lecturas de los primeros 300 cuadros
   iguales al ciclo.
2. **Actualización periódica entre dos ciclos.** La actualización k vence en k · 71.590,9 ciclos. Un
   comando del OKI escrito en el ciclo 188.856.794 con la actualización 2.638 en 188.856.794,2 daba una
   muestra distinta (la 399.657, a 52,76 s): con "antes si vence antes del acceso" la escritura iba
   primero; en MAME va después. El planificador hace correr el Z80 floor((t_obj − t_local) / período)
   ciclos, así que lo para al final del ciclo M que alcanza floor(k · 71.590,9) y ejecuta el temporizador
   de la actualización antes de cualquier ciclo M posterior. Regla adoptada: la actualización cuenta como
   hecha antes de todo acceso desde el ciclo floor(k · 71.590,9). Todos los sucesos quedan en ciclos
   enteros.
3. **Latch del 68000.** `generic_latch_8::write` también usa `synchronize()`, a la hora local del 68000;
   el Z80 corre hasta donde paró el 68000 tras abortar su porción. La placa hace visible el valor desde
   el ciclo del Z80 del final de la instrucción del 68000 que escribe.

Los temporizadores del YM vencen en ciclos enteros del Z80 (mismo cristal) y la INT se acepta en el
primer límite de instrucción con ciclo ≥ vencimiento: es lo que hace MAME al parar el Z80 al final del ciclo
M que alcanza el vencimiento y mirar la línea en el siguiente límite.

## Resultados

- Partida completa (12.000 cuadros, 203 s): 1.391 comandos del 68000 en el mismo cuadro y con el mismo
  valor; 456.589 escrituras y 316.409 lecturas del Z80 iguales en dirección, valor **y ciclo**.
- El instante de las escrituras de latch difiere entre −253 y +235 ciclos del 68000 (percentil 1-99:
  −129 a +203, mediana −1), el error del modelo de ciclos del 68000 (D16). No llega al sonido porque el
  Z80 solo lee el latch en la INT (cada 14.336 ciclos del Z80); la escritura más cercana a una lectura
  quedó 36 ciclos del Z80 antes de ella y el 1 % de los comandos cae a menos de 82 ciclos (~230 del
  68000). Es el único punto en que la temporización del 68000 puede cambiar el sonido: un comando que
  cayera al otro lado de una lectura sonaría 4 ms después. No ocurre en la partida de referencia.

## Pendiente

- Reloj del 68000 al ciclo del de MAME (hito 6) eliminaría el riesgo del latch descrito arriba.
