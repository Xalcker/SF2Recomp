/*
 * romset.h - catálogo de las 23 ROMs de Street Fighter II: The World Warrior (World 910522, set "sf2" de
 * MAME) y su carga desde un origen abstracto (carpeta o zip) con verificación por CRC-32.
 *
 * Las ROMs se identifican por CRC-32 y tamaño, no por nombre: el mismo contenido circula con los nombres
 * antiguos de MAME ("sf2e.30g", "sf2_05.bin") y con los actuales ("sf2e_30g.11e", "sf2-1m.3a"), y el
 * cargador acepta cualquiera dentro de un zip, con la ruta que sea.  Los nombres solo sirven para los
 * mensajes y para que el origen de tipo carpeta sepa qué archivos buscar.  Archivos de otro tamaño o de
 * otro CRC (los volcados de PLD del zip de MAME, por ejemplo) se ignoran.
 *
 * El núcleo no abre archivos: el anfitrión entrega un RomSource (plataforma/hostio.c lo hace con stdio, el
 * navegador con los bytes que recibe del selector de archivos) y recibe los búferes ya verificados.
 */
#ifndef RUNTIME_ROMSET_H
#define RUNTIME_ROMSET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROMSET_COUNT 23

typedef enum RomKind {
    ROMKIND_PROGRAM,    /* 68000, pares de chips que se intercalan en la imagen de 1 MiB (dest = base, lane = 0 par / 1 impar) */
    ROMKIND_GFX,        /* región "gfx" de MAME de 6 MiB, ROM_LOAD64_WORD (dest = offset dentro de la región) */
    ROMKIND_Z80,        /* programa del Z80 (64 KiB: 32 KiB fijos y 32 KiB de banco) */
    ROMKIND_OKI,        /* muestras del OKI MSM6295 (dest = offset en los 256 KiB) */
} RomKind;

typedef struct RomInfo {
    const char *name;        /* nombre actual del set de MAME */
    const char *alt_name;    /* nombre antiguo (carpetas sueltas) */
    uint32_t crc, size;
    RomKind kind;
    uint32_t dest;
    int lane;
} RomInfo;

extern const RomInfo romset_catalogue[ROMSET_COUNT];

/* ---- origen de archivos ----------------------------------------------------------------------- */
typedef struct RomFile {
    const char *name;        /* nombre o ruta del archivo en el origen */
    uint32_t size;
    uint32_t crc;            /* solo si crc_known (un zip ya lo trae en su directorio) */
    int crc_known;
} RomFile;

typedef struct RomSource {
    void *ctx;
    /* describe el archivo i-ésimo del origen; devuelve 0 cuando ya no hay más */
    int (*get)(void *ctx, size_t i, RomFile *out);
    /* lee el archivo i-ésimo completo en dst (out.size bytes); 0 si va bien */
    int (*read)(void *ctx, size_t i, uint8_t *dst);
} RomSource;

/* ---- conjunto cargado ------------------------------------------------------------------------- */
typedef struct RomSet {
    uint8_t *data[ROMSET_COUNT];     /* búfer de cada ROM (romset_catalogue[i].size bytes), NULL si falta */
} RomSet;

void romset_init(RomSet *rs);
void romset_free(RomSet *rs);

/* nombres que un origen de tipo carpeta debe probar: 2 por ROM (actual y antiguo) */
size_t romset_candidate_count(void);
const char *romset_candidate_name(size_t i);

/* Recorre el origen, verifica por CRC y rellena `rs`.  Devuelve cuántas ROMs faltan o están mal (0 = el
 * conjunto está completo).  `report` recibe un texto legible (en español) con lo que falta, lo que tiene
 * el CRC equivocado y lo que no se pudo leer; queda vacío si todo está bien. */
int romset_load(RomSet *rs, const RomSource *src, char *report, size_t report_size);

/* intercala los ocho chips de programa en la imagen plana de 1 MiB que usa el código recompilado */
void romset_build_program(const RomSet *rs, uint8_t *image /* 0x100000 bytes */);

/* abre el zip que está en memoria y carga las ROMs de él: lo mismo que romset_load, y -1 si no es un zip legible
 * (con el motivo en `report`) */
int romset_load_zip_memory(RomSet *rs, const uint8_t *data, size_t size, char *report, size_t report_size);

/* origen en memoria sobre un zip (el Zip y su búfer deben sobrevivir a la carga) */
struct Zip;
void romset_source_from_zip(RomSource *src, const struct Zip *zip);

#ifdef __cplusplus
}
#endif

#endif
