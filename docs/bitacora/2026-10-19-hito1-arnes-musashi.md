# 2026-10-19 · Hito 1 · Arnés de validación con Musashi y cierre del hito

## Pregunta de partida

¿Ejecuta el código recompilado exactamente igual que el 68000? El criterio del hito: arrancar
desde reset y correr 600 cuadros con la IRQ de VBlank sin divergir del intérprete Musashi.

## Qué se hizo

- Musashi (MIT, repo de Karl Stenerud, commit 313ebf1, versión 4.60) copiado a
  `third_party/musashi/` con `m68kconf.h` configurado como 68000 puro: sin 68010/020/030/040 ni
  PMMU, acuse de interrupción por callback (`recomp_musashi_int_ack`), sin prefetch ni error de
  dirección (decisión D13). `m68kmake` genera `m68kops.c` en `build/musashi/`.
- `harness/musashi_glue.c`: callbacks de memoria de Musashi sobre una segunda instancia de
  `runtime/mem.c` (RAM propia, ROM compartida), `musashi_check_irq()` para que Musashi tome la
  interrupción pendiente sin ejecutar ninguna instrucción, y el desensamblador para los informes.
- `harness/main.c`: trampolín con la tabla de despacho (`rt_lookup`, búsqueda binaria), gancho
  `rt_hook()` por instrucción (entrega de IRQ en ambos lados → comparación de registros, SR y
  escrituras de la instrucción anterior → un paso de Musashi), inyección de IRQ2 cada 166.666
  ciclos de Musashi, comparación de la RAM completa cada 60 cuadros, informe de divergencia con
  desensamblado de la instrucción siguiente de cada lado y las 64 últimas direcciones, cobertura
  dinámica (`--cov`), y un modo autónomo sin Musashi (`sf2_run`) temporizado con los ciclos base
  de la tabla de Musashi. Las excepciones y la IRQ ejecutan el manejador recompilado anidado; si
  el manejador no vuelve al punto interrumpido se reanuda por `longjmp` al trampolín.

### Divergencias encontradas y corregidas (en orden)

| Síntoma | Causa | Arreglo |
|---|---|---|
| SR 0x2700 vs 0x2704 en la primera instrucción | Musashi arranca con `not_z_flag = 0`, es decir Z=1 | el reset del recompilado pone Z=1 |
| Musashi no ejecutaba la primera instrucción | su primer `m68k_execute` consume los ciclos de la secuencia de reset | `m68k_execute(0)` tras `m68k_pulse_reset()` |
| "memoria distinta" en el cuadro 60 | la comparación de RAM corría entre el paso de Musashi y el del recompilado | se comprueba al principio del gancho, con ambos lados en el mismo límite |
| `destino indirecto sin resolver 005740` (cuadro 107, SR=0000) | PC de tarea leído de la tabla de punteros 0xEA2 y pasado en a0 a `trap #0` vía envoltorio | el análisis sigue los argumentos de `trap` y de los envoltorios `trap ; rts`, también cuando vienen de una tabla |
| 2048 lecturas "sin mapear" en 0xC4000-0xC6000 | la imagen tenía 768 KB; el juego lee datos del cuarto par de ROMs | imagen de 1 MB (ver bitácora del intercalado) |

Ninguna divergencia se debió a las banderas ni a la semántica de las instrucciones: la copia
fiel de Musashi en `runtime/cpu.h` (D8) funcionó a la primera.

## Comandos

```bash
make -j8
make check                                                        # 600 cuadros
build/sf2_lockstep build/rom.bin --frames 7200 --quiet --cov build/executed_7200.txt
build/sf2_lockstep build/rom.bin --frames 200 --trace --log-io    # depuración
build/sf2_run build/rom.bin --frames 600                          # autónomo
```

## Resultados

```
OK: 600 cuadros sin divergencia, 9330968 instrucciones, 99999606 ciclos, 493 IRQ atendidas, memoria identica
accesos E/S: 3949 lecturas, 10866 escrituras, 0 sin mapear
cobertura dinamica: 1064 instrucciones distintas ejecutadas de 43216 estaticas (2.5%)

OK: 7200 cuadros sin divergencia, 104488551 instrucciones, 1199995206 ciclos, 7093 IRQ atendidas, memoria identica
accesos E/S: 56751 lecturas, 156652 escrituras, 2 sin mapear
cobertura dinamica: 10118 instrucciones distintas ejecutadas de 43216 estaticas (23.4%)
```

- Tiempo real: 600 cuadros en ~2 s; 7.200 cuadros en ~37 s (Apple M1 Pro).
- 493 IRQ en 600 cuadros: la IRQ2 está enmascarada durante el test de RAM del arranque y las
  peticiones de VBlank solapadas se funden en una (como `HOLD_LINE` en MAME).
- Los 2 accesos sin mapear de la ejecución larga son lecturas de 0xC60180, fuera de toda zona de
  la placa (ROM, E/S, RAM); devuelven 0xFFFF en los dos lados. Posiblemente un puntero sin
  inicializar del propio juego; a contrastar con MAME en el hito 2.
- El binario autónomo `sf2_run` ejecuta los mismos 600 cuadros en ~0,05 s con la IRQ por ciclos
  estimados (489 IRQ, la diferencia viene de la estimación de ciclos).
- Cobertura dinámica: con 1 MB el juego avanza más en la atracción (de 3.660 a 10.118
  instrucciones distintas en 7.200 cuadros).

## Estado del hito 1

Cumplido el criterio del plan. Entregables: `tools/interleave.py`, `tools/m68k.py`,
`tools/analyze.py`, `recompiler/gen_c.py`, `runtime/`, `harness/`, `Makefile`, READMEs por
carpeta, decisiones D8-D14 y estas cinco entradas de bitácora. `docs/PLAN.md` actualizado.

## Pendiente

- Hito 2: mapa de memoria con entradas y dipswitches reales, planificador de VBlank, contraste
  con la traza de MAME (`tools/mame.sh`, formato con registros) como referencia independiente.
- Punteros a código en RAM no vistos por el análisis: el arnés los detecta; `tools/hints.txt`
  los corrige.
- Rendimiento del código generado (hito 6).
