# 2026-10-19 · Hito 3 · Sprites, doble búfer y prioridades del CPS-B-11

## Pregunta de partida

Cómo dibujar la tabla de objetos tal como la usa el juego (doble búfer con un cuadro de retraso) y cómo
aplicar las máscaras de prioridad del CPS-B-11 para que los sprites se tapen con los tiles correctos y
entre sí en el mismo orden que MAME.

## Qué se hizo

### Tabla de objetos y doble búfer

La tabla son 0x800 bytes (256 entradas de 4 palabras: X, Y, código, atributo) en la base OBJ
(0x800100, registro × 256 alineado a 0x800). Atributo: bits 0-4 color (página 0 de la paleta), bit 5
volteo X, bit 6 volteo Y, bits 8-11 ancho en tiles menos uno, bits 12-15 alto menos uno. Una entrada
con atributo `0xFFxx` marca el fin de la tabla; sin marca se dibujan las 256.

La placa (y MAME, `cps1_objram_latch`) **copia la tabla al empezar cada VBlank**, después de haber
dibujado el cuadro, y dibuja con la copia del VBlank anterior. El juego aprovecha el retraso con un
doble búfer: el manejador de IRQ2 escribe la base OBJ en 0xAC0 (desde la sombra 0x2A(a5)) alternando
entre 0x9100 y 0x9180 mientras las tareas rellenan la otra zona. En el runtime, `video_vblank`
renderiza y después `video_latch_obj` copia la tabla (`Video.obj`), en ese orden, al cruzar el umbral
de ciclos del cuadro (gancho `on_vblank` de `harness/machine.c`), antes de marcar la IRQ2 pendiente.
Esto reproduce la secuencia de `vblank_begin` de MAME: `frame_update` (cuadro), llamadas de VBlank
(`cps1_get_video_base`, `cps1_objram_latch`) e IRQ.

Durante el test de RAM del arranque la IRQ2 está enmascarada pero los VBlank siguen ocurriendo: el
gancho se llama en cada umbral de ciclos (1806 VBlank frente a 1701 IRQ en 1700 cuadros), igual que en
MAME.

### Bloques de sprites

Con ancho o alto mayor que uno la entrada describe un bloque de tiles de 16x16 contiguos en pantalla
cuyos códigos se calculan como en `cps1_render_sprites`: `(código & ~0xF) + ((código + columna) & 0xF)
+ 0x10 × fila`; con volteo X la columna se recorre al revés (`(código + ancho − 1 − columna) & 0xF`) y
con volteo Y la fila (`0x10 × (alto − 1 − fila)`). Cada tile se coloca en `(X + 16·columna) & 0x1FF`,
`(Y + 16·fila) & 0x1FF`. Ryu en reposo es un bloque de 4 x 6 tiles (entrada 83 del cuadro 1600); las
sombras y la cabaña del fondo son entradas sueltas.

### Prioridad tile/sprite (máscaras del CPS-B-11)

MAME lo resuelve con un bitmap de prioridad por píxel. Traducido a nuestro renderizador:

1. Cada tilemap se dibuja en su turno con todos los pens distintos de 15, sin tocar la prioridad.
2. Si la capa que sigue en el orden es la de sprites, se hace un segundo pase sobre la capa recién
   dibujada ("capa alta") que marca el bit 0 de prioridad en los píxeles cuyo pen tiene a 1 el bit
   correspondiente en la máscara de prioridad del grupo del tile (bits 7-8 del atributo → registros
   0x800168, 0x80016A, 0x80016C, 0x80016E). El pen 15 también cuenta si la máscara tiene el bit 15, y
   los tiles fuera de rango del mapeador se tratan como pens 15.
3. Un píxel de sprite (pen ≠ 15) se escribe salvo que la prioridad valga 1 (tile por delante) **o 31**
   (ya lo escribió un sprite); en todo caso la prioridad del píxel pasa a 31.

El punto 3 es el que costó: `prio_transpen` recibe la máscara 0x02 pero `gfx_element::prio_transpen`
le añade implícitamente el bit 31 (`pmask |= 1 << 31`), de modo que un sprite nunca sobrescribe a otro
ya dibujado. Como `cps1_render_sprites` recorre la tabla desde la primera entrada, **la entrada de
índice más bajo queda encima**: Ryu (entrada 83) tapa al poste de la cabaña (entradas 140-141) y a las
sombras (118-129), que vienen después en la tabla. La primera versión del runtime ignoraba el bit 31 y
dejaba encima la última entrada: 1.129 píxeles distintos en el cuadro 1600 (piernas y pies de los dos
luchadores, el poste), 822 en el 1800, 1.874 en el 10000. Con la regla correcta, 0.

Los sprites no tienen "prioridad alta" propia (`cps1_render_high_layer` no hace nada para la capa 0).
En la partida de referencia las cuatro máscaras valen 0 en todos los cuadros capturados, así que esta
parte está implementada pero no ejercitada por SF2; queda contrastada solo contra la lectura del
código de MAME.

## Comandos

```bash
python3 -I - <<'EOF'   # listar la tabla de objetos de un volcado
d = open('build/video/mame/obj_1600.bin','rb').read()
for i in range(256):
    x, y, c, a = [int.from_bytes(d[8*i+2*k:8*i+2*k+2], 'big') for k in range(4)]
    if (a & 0xff00) == 0xff00: break
    print(i, hex(x), hex(y), hex(c), hex(a))
EOF
build/render_dump build/video/mame/gfx_1600.bin regs.bin obj_subconjunto.bin build/video/mame/pal_1600.bin salida.png
```

Para aislar el fallo de orden se construyeron tablas de objetos parciales (solo Ryu; solo las entradas
anteriores a Ryu; solo las posteriores; solo el poste) y se renderizaron con `render_dump` sobre el
volcado de MAME del cuadro 1600, comparando píxeles concretos: en (74, 149) MAME mostraba el color de
Ryu y el runtime el del poste.

## Resultados

- Con el doble búfer y la regla de prioridad, los 17 cuadros de referencia (selección de personaje,
  combate con los dos luchadores superpuestos a sombras y decorado, KO, continuar, atract con la demo
  Blanka/Guile) son idénticos a MAME píxel a píxel.
- `Video.sprites`, `Video.marked` y `Video.blocked` informan por cuadro de entradas dibujadas, píxeles
  marcados por las máscaras y píxeles de sprite tapados (0 y 0 en toda la partida de referencia).

## Pendiente

- Ejercitar las máscaras de prioridad con un juego o una escena que las use (en SF2 no se ha visto
  ninguna en 12.000 cuadros).
