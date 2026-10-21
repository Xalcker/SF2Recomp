# Cajas de golpe y de daño (hitboxes) de SF2: dónde leerlas

Fecha: 2026-10-21. Especificación para dibujarlas en un overlay leyendo la RAM de trabajo (0xFF0000-0xFFFFFF) y la ROM de
programa (1 MiB, `build/rom.bin`). Verificada contra capturas con `tools/hitbox_probe.py` (ver §7). No contiene bytes de ROM.

## 1. El «objeto» y sus campos

El juego trabaja con **objetos** (en el desensamblado, registro `a6`). Cada jugador es un objeto y su dirección es
**base de la estructura de jugador + 6**: P1 `0xFF83C6`, P2 `0xFF86C6` (`0x3C6(A5)` y `0x6C6(A5)` con A5 = 0xFF8000).
Los proyectiles son objetos con el mismo formato. Offsets relativos al objeto (restar 6 para obtener el de la
estructura de jugador que se usaba en la bitácora: `+0x0C` x = objeto `+0x06`, etc.):

| Offset objeto | Tamaño | Significado |
|---|---|---|
| +0x00 | byte | existe/activo (0 = libre; los proyectiles se consideran vivos con 1) |
| +0x06 | palabra con signo | x de mundo |
| +0x0A | palabra con signo | y de mundo, hacia arriba; 0x28 = suelo |
| +0x12 | byte | **orientación/espejo**: 0 = mira a la izquierda (formato de las tablas), distinto de 0 (1) = mira a la derecha. El bit 1 es espejo vertical (solo sprites especiales); el código de colisión solo hace `tst.b`. En la estructura de jugador es `+0x18` |
| +0x18 | palabra | cuenta atrás de la pose actual |
| +0x1A | puntero largo (ROM) | **entrada de la pose actual** (estructura de jugador `+0x20`) |
| +0x2A | palabra | vida (jugador; en la estructura `+0x30`) |
| +0x34 | puntero largo (ROM) | **tabla de cajas del personaje** (estructura `+0x3A`) |
| +0x1D4 | palabra | dirección (16 bits bajos, 0xFFxxxx) del proyectil activo del jugador, 0 si no hay (estructura `+0x1DA`) |

## 2. Cómo se eligen las cajas de la pose

La pose actual es una entrada de **0x18 bytes** (las poses forman una lista; el motor avanza `+0x18` al agotar la cuenta;
la palabra `+2` con el bit 15 indica salto atrás a un puntero en `+0x18`). Bytes de la entrada que importan:

| Byte | Nombre (OBJ TEST) | Caja |
|---|---|---|
| +0x08 | HEAD | índice de caja vulnerable de cabeza |
| +0x09 | BODY | cuerpo |
| +0x0A | FOOT | pies |
| +0x0B | WEAK | punto débil (4 bytes, ver abajo) |
| +0x0C | ATCK | índice de caja de **ataque** (0 = ataque inactivo) |
| +0x0D | BODY1 | caja de empuje/choque («atari») |
| +0x0E/+0x0F | KAGE/PRIO | sombra/prioridad (no son cajas) |

Índice 0 significa «sin caja»; los índices valen desde 1 (el registro 0 de cada tabla es de relleno).

La **tabla de cajas** (`+0x34`) empieza con 6 palabras de desplazamiento relativas al inicio de la tabla (algunas tablas
tienen cabecera más larga, el código solo usa estas 6): `+0` cabeza, `+2` cuerpo, `+4` pies, `+6` débil, `+8` ataque,
`+0xA` empuje. Dirección del registro = `tabla + palabra(tabla + k) + índice × tamaño` (tamaño 4 bytes salvo el ataque, 12).
Las secciones no están en orden ni delimitadas en todas las tablas: no se deduce el tamaño de una sección del desplazamiento siguiente.

## 3. Formato de los registros

Caja de 4 bytes (cabeza, cuerpo, pies, débil, empuje): `x_off` (con signo), `y_off` (con signo), `rx` (radio, sin signo),
`ry` (radio). Es **centro + radios**: la caja cubre `centro ± radio`.

Registro de ataque de 12 bytes: `[0]` x_off, `[1]` y_off, `[2]` rx, `[3]` ry (igual que arriba) y, además:
`[4]` índice del efecto/sonido del golpe (`&0x7F`, rutina 0x7E210), `[5]` identificador de golpe (evita golpear dos veces
con el mismo; con el bit 7 activo vale 1 y significa **alcance largo**: el desplazamiento x pasa a ser `-byte5`
tomado sin signo, ya que un byte con signo no llega a 165 px, y `[0]` vale 0), `[6]` y `[7]` fuerza/tipo (no estudiados),
`[8]` daño (el juego resta `[8]+1` en la ruta de golpe directo), `[0xB]` clase de golpe (el código compara con 0x14, 0x16,
0x1A y 0x20 en 0x7D320). Los demás bytes no se han descifrado.

Posición de la caja en el mundo (rutinas 0x7E462 y 0x7E46E):

```
centro_x = x_objeto + (orient != 0 ? -x_off : +x_off)      ; las tablas están en formato «mira a la izquierda»
centro_y = y_objeto + y_off                                ; y hacia arriba, sin espejo
```

Dos cajas se solapan si `|dx| <= rx_a + rx_b` y `|dy| <= ry_a + ry_b` (comparación sin signo de `d+r` con `2r`; los
extremos cuentan). El centro del ataque lleva el espejo y el del vulnerable también.

## 4. De mundo a pantalla

- Cámara de mundo X: `0xFF8BD8` (palabra); Y: `0xFF8BDC`. Es la que usan los sprites de los objetos con `+0x25 == 0`
  (jugadores y proyectiles; otros modos usan `0xFF8B58`, `0xFF8BD8`+tabla o `0xFF8C58`, rutina 0x7F546).
- `pantalla_x = x_mundo - [0xFF8BD8]` (el juego suma 0x40 porque las coordenadas de sprite de la CPS empiezan 64 px a la izquierda del área visible de 384).
- `pantalla_y = 240 - (y_mundo - [0xFF8BDC])`, píxel 0 = línea superior de la imagen de 384x224 (el juego calcula `256 - (y - camY)` y la CPS resta 16 de desplazamiento).
- Relación con SCROLL2: `[0xFF803A] = [0xFF8BD8] - 192`; el registro CPS-A de SCROLL2 X (`0xFF8064`, copiado un cuadro
  después) vale `[0xFF803A] - 64 = camara - 256`. SCROLL1 (`0xFF8036`) y SCROLL3 (`0xFF803E`) son capas con otra velocidad,
  no se usan para los objetos de combate. Medido en los cuadros 1660-1676 de `ryu.txt` (cámara +2 px por cuadro).

## 5. Qué rutinas usan los datos y cuándo está activo un ataque

| Dirección | Función |
|---|---|
| 0x07E3D2 | obtiene el registro de ataque del objeto `a6` (`a3`); devuelve 0 si el byte +0x0C de la pose es 0 → **ataque activo = ese byte distinto de 0** |
| 0x07E3FC / 40C / 41C / 42C / 43C | obtienen la caja de cabeza / cuerpo / pies / débil / empuje de `a2` (`a4`) |
| 0x07E462 / 0x07E46E | solape de una caja de ataque (`a3`) o de 4 bytes contra la caja `a4`; salida `d4` = 1 si solapan |
| 0x07E23A | prueba el ataque contra cabeza, cuerpo y pies del defensor (y el punto débil después) |
| 0x07D29A → 0x07D2F2 | golpe jugador contra jugador (por cada dirección); evita repetir golpe con `[5]` (guardado en `0x4A(a2)`); aplica daño; los proyectiles entran por 0x07DD7C/0x07DF70 (hay dos variantes de golpe de proyectil) |
| 0x07E516 | choque/empuje entre jugadores con las cajas de empuje (separa a los luchadores) |
| 0x07D56A | choque proyectil contra proyectil con las cajas de empuje (los dos proyectiles se anulan) |
| 0x07F190 / 0x07F546 | dibujo de sprites; cálculo de pantalla del objeto |

El juego no usa un indicador aparte: «ataque activo» es solo que la pose actual tenga índice de ataque. Los golpes
de un proyectil se prueban con el objeto proyectil como atacante (`0x1D4(a6)` del jugador).

## 6. Proyectiles

- Reserva «C»: **8 objetos de 0xC0 bytes desde `0xFF938A`** (`0xFF938A + i×0xC0`, `i` = 0..7; el último, `0xFF98CA`, es el que se
  asigna primero; la reserva se crea en 0x0020AA, el tipo está en `+0x22` = 4). Los proyectiles activos de las capturas
  (bola de fuego de Ryu y Ken, sonic boom de Guile) salieron siempre en `0xFF98CA`, con `+0x00` = 1.
- Se llega a ellos por el puntero de `+0x1D4` del jugador (el método que usa el juego) o recorriendo la reserva y quedándose
  con los que tienen `+0x34` distinto de 0 (los demás objetos de las reservas, escenario y sombras, tienen tabla 0).
- Mismos campos que un jugador (x `+6`, y `+0xA`, orientación `+0x12`, pose `+0x1A`, tabla `+0x34`). Las tablas de
  proyectil tienen cabeza/cuerpo/pies/débil vacíos y solo ataque y empuje: hadoken 0x249C0 (ataque de 15x12 centrado en
  el objeto; empuje 28x19), sonic boom 0x24A58 (ataque 35x8 desplazado 16 px adelante; empuje 41x17).
- No se han comprobado los demás proyectiles (Dhalsim, Honda, Zangief…) ni las reservas A, B y D como proyectiles.

## 7. Verificación

`tools/hitbox_probe.py --seq tools/sequences/ryu.txt --frame N --out salida.png [--list]` reproduce la secuencia, lee las
cajas con esta especificación y las dibuja (amarillo cabeza, verde cuerpo, azul pies, magenta débil, rojo ataque, blanco empuje).
Cuadros verificados abriendo los PNG (con zoom 2):

| Secuencia y cuadro | Qué se comprobó |
|---|---|
| ryu 1690 | Ryu parado: cabeza, cuerpo, pies y empuje sobre el personaje; cámara en movimiento lento |
| ryu 1708 | puñetazo activo (ataque idx 36, x_off -54, flip 1): la caja roja cubre el brazo estirado a la derecha |
| ryu 1636 | Blanka mirando a la izquierda (flip 0) con ataque idx 26 (x_off -93): la caja roja cubre el brazo hacia la izquierda; Ryu a la vez con ataque propio |
| ryu 1670 | shoryuken en el aire con y=75 y camY=7: las cajas siguen a Ryu fuera del suelo |
| ryu 4953 | hadoken: caja roja de ataque 30x24 centrada en la bola; caja blanca de empuje mayor |
| guile 1766 | sonic boom: caja roja alrededor de la media luna; Ken a la derecha con sus cajas |
| guile 2022 | salto de Guile con y=75 y camY=4 |

Qué quedó **sin confirmar o con reservas**:

- Las cajas vulnerables son aproximadas respecto a la silueta del sprite: en poses extremas (piernas colgando en el salto de
  Guile, cabeza del shoryuken de Ryu) la caja no cubre toda la silueta o se desplaza hasta 20 px. Es el diseño de los
  datos del juego, no un fallo de lectura: las cajas coinciden en el suelo y en reposo, y los radios y desplazamientos
  son los mismos que usa el código de colisión.
- Caja de punto débil (byte +0x0B): nunca fue distinta de 0 en los cuadros vistos; se lee con la misma regla, sin verificar con captura.
- **Agarre (CATCH) y bloqueo (BLOCK)**: no se encontró un formato de caja propio. La detección de agarre pasa por registros de
  ataque con clase `[0xB]` (0x14, 0x16, 0x1A, 0x20 en 0x7D320-0x7D440) y el bloqueo se decide por entradas y estado del
  objeto, no por una caja. No verificado con capturas; hacen falta trazas de agarres para cerrarlo.
- Las rutas con espejo vertical (`+0x12` bit 1), el modo de cámara distinto de 0 (`+0x25 != 0`) y los proyectiles de otros personajes no se han probado.
- El campo `[5]` de alcance largo (x = -byte5) se dedujo del código 0x7E462; las tablas con esos registros (la de 0x92E70 y 0x9362C)
  no se dibujaron en una captura.
- Los jugadores usan la tabla `+0x34` por personaje (Ryu usa la de 0x91790; la tabla de datos por personaje se elige con
  la lista de pares de 8 bytes de 0x887B4, y algunas se comparten entre entradas).
