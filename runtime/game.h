/*
 * game.h - descriptor de cada juego (variante de Street Fighter II) que el núcleo sabe mover (D63).
 *
 * Todo lo que distingue una variante de otra vive aquí y no en constantes repartidas por el núcleo:
 *
 *   - hardware (de cps1.cpp / cps1_v.cpp de MAME 0.289): reloj del 68000, tamaño del programa, configuración del
 *     CPS-B (el chip de la placa B: registro de identificación, multiplicador, IN2, control de capas, prioridades,
 *     control de paleta y bits de habilitación de capas) y mapeador de bancos gráficos (la PAL de la placa B);
 *   - catálogo de ROMs (ROM_START del set de MAME);
 *   - hechos del programa que el núcleo necesita para validar contra MAME (manejador de la IRQ2, punto de muestra,
 *     contador de cuadros).  Solo se conocen tras analizar el programa de esa variante: `program_known` dice si
 *     ya están (fase 1 de cada variante, docs/VARIANTES.md).
 *
 * Qué juego mueve un ejecutable lo decide el C generado: gen_c.py escribe `recomp_game_id` (el id del set de
 * MAME) y quien enlaza el código generado obtiene su descriptor con recomp_game() (runtime/dispatch.c).  Las
 * piezas que no enlazan código generado (romset, mem, cps1) reciben el descriptor como parámetro.
 */
#ifndef RUNTIME_GAME_H
#define RUNTIME_GAME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GAME_ROMS_MAX 32          /* ROMs de un catálogo (sf2: 23, sf2ce: 18) */
#define GAME_MAPPER_MAX 8         /* rangos de un mapeador gráfico */

typedef enum RomKind {
    ROMKIND_PROGRAM,    /* 68000: lane 0/1 = chip de bytes pares/impares de un par (dest = base del par, ROM_LOAD16_BYTE);
                           lane 2 = chip de palabras con los dos bytes de cada una invertidos (dest = dirección, ROM_LOAD16_WORD_SWAP) */
    ROMKIND_GFX,        /* región "gfx" de MAME, ROM_LOAD64_WORD (dest = offset dentro de la región) */
    ROMKIND_Z80,        /* programa del Z80 (64 KiB: 32 KiB fijos y 32 KiB de banco) */
    ROMKIND_OKI,        /* muestras del OKI MSM6295 (dest = offset en los 256 KiB) */
} RomKind;

#define ROMLANE_WORD 2

typedef struct RomInfo {
    const char *name;        /* nombre actual del set de MAME */
    const char *alt_name;    /* nombre antiguo (carpetas sueltas); el mismo que `name` si no lo hay */
    uint32_t crc, size;
    RomKind kind;
    uint32_t dest;
    int lane;
} RomInfo;

/* Configuración del CPS-B con los valores crudos de la tabla de MAME (struct CPS1config de cps1.h): desplazamientos
 * de byte desde 0x800140 y -1 donde MAME pone -1.  Se usan con la misma aritmética que cps1_cps_b_r (desplazamiento / 2
 * en enteros, de modo que -1 / 2 = 0): así el CPS-B-11 lee en 0x800140 el "resto del multiplicador" igual que MAME. */
typedef struct CpsbConfig {
    const char *name;               /* "CPS-B-11", "CPS-B-21" */
    int id_addr, id_value;          /* registro de identificación y su valor (-1 = lee 0xFFFF) */
    int mult_factor1, mult_factor2, mult_result_lo, mult_result_hi;
    int in2_addr;                   /* puerto IN2 de la placa C (botones 4-6) */
    int layer_control;
    int priority[4];
    int palette_control;
    uint16_t layer_enable[3];       /* bits de habilitación de SCROLL1, SCROLL2 y SCROLL3 en el control de capas */
} CpsbConfig;

/* mapeador gráfico (struct gfx_range de MAME): tamaños de banco en unidades de 8x8 y rangos con el código ya
 * desplazado a esa escala */
typedef struct GfxRange { int type, start, end, bank; } GfxRange;
typedef struct GfxMapper {
    const char *name;               /* "STF29", "S9263B" */
    int bank_sizes[4];
    int nranges;
    GfxRange ranges[GAME_MAPPER_MAX];
} GfxMapper;

/* RAM del juego que leen y escriben los mods (D68).  Todo direcciones absolutas de la RAM de trabajo (0xFF0000-0xFFFFFF); los
 * luchadores tienen su objeto en base + 6 (X de mundo en base + 0xC) y la vida en base + 0x30. */
typedef struct ModRam {
    uint32_t mode, phase, subphase;     /* bytes bajos de 0x0(a5), 0x4(a5), 0x8(a5): partida 2/4/0x0A en combate */
    uint32_t credits;
    uint32_t p1, p2;                    /* estructura de cada jugador */
    uint32_t timer, timer_sub;          /* temporizador del asalto (BCD) y cuadros hasta el siguiente segundo (30 -> 1) */
    uint32_t cam_x, cam_y;              /* cámara principal */
    uint32_t cam_lo, cam_hi;            /* topes izquierdo y derecho de la cámara del escenario */
    uint32_t opponent;                  /* personaje del oponente = escenario */
    uint32_t proj;                      /* reserva de proyectiles: 8 objetos de 0xC0 bytes */
    uint32_t snd_queue, snd_read;       /* cola de comandos de sonido (64 palabras) e índice de lectura */
    uint32_t obj_lo, obj_hi;            /* objetos del combate (proyectiles, chispas): [obj_lo, obj_hi) */
} ModRam;

typedef struct GameDef {
    const char *id;                 /* nombre del set de MAME: "sf2", "sf2ce" */
    const char *title;              /* para los mensajes */
    uint32_t cpu_hz;                /* reloj del 68000 (cps1_10MHz / cps1_12MHz) */
    uint32_t frame_cycles;          /* ciclos del 68000 por cuadro: cpu_hz * 512 * 262 / 8 MHz */
    uint32_t prog_size;             /* bytes de programa del 68000 (lo demás de 0x000000-0x3FFFFF lee 0) */
    uint32_t gfx_size;              /* bytes de la región "gfx" */
    const CpsbConfig *cpsb;
    const GfxMapper *mapper;
    int nroms;
    const RomInfo *roms;
    /* hechos del programa (validos solo con program_known) */
    int program_known;
    uint32_t irq2_handler;          /* manejador de la IRQ2 (VBlank) */
    uint32_t sample_pc;             /* primera lectura de E/S del manejador: punto de muestra comparable con MAME */
    uint32_t frame_counter;         /* byte de la RAM de trabajo que el manejador incrementa en cada cuadro */
    int has_mods;                   /* 1 si los mods existen en esta variante (parches de recompiler/mod_patches*.txt y mod_ram) */
    const ModRam *mod_ram;          /* RAM que usan los mods (NULL sin mods) */
    int has_dev_tools;              /* herramientas de desarrollo de Capcom (docs/HERRAMIENTAS_DEV.md): solo WW */
} GameDef;

extern const GameDef game_sf2;      /* The World Warrior (World 910522) */
extern const GameDef game_sf2ce;    /* Champion Edition (World 920513) */
extern const GameDef game_sf2hf;    /* Hyper Fighting (World 921209) */

/* descriptor por id de MAME, o NULL */
const GameDef *game_find(const char *id);
/* número de juegos conocidos y el i-ésimo */
int game_count(void);
const GameDef *game_at(int i);
/* descriptor de la variante del código generado enlazado (runtime/dispatch.c, con `recomp_game_id` de gen_c.py); NULL si
 * el id no es de ningún juego conocido.  Solo existe en los ejecutables que enlazan el código generado. */
const GameDef *recomp_game(void);
/* cuadros por segundo de la variante */
static inline double game_frame_hz(const GameDef *g) { return (double)g->cpu_hz / (double)g->frame_cycles; }

#ifdef __cplusplus
}
#endif

#endif
