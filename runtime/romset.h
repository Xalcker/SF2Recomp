/*
 * romset.h - carga de las ROMs de una variante de SF2 (catálogo en game.c: 23 ROMs del set "sf2" de MAME,
 * 18 del set "sf2ce"...) desde un origen abstracto (carpeta o zip) con verificación por CRC-32.
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
#include "game.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROMSET_MAX GAME_ROMS_MAX

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
    const GameDef *game;             /* variante cuyo catálogo se carga (game->roms) */
    uint8_t *data[ROMSET_MAX];       /* búfer de cada ROM (game->roms[i].size bytes), NULL si falta */
} RomSet;

void romset_init(RomSet *rs, const GameDef *game);
void romset_free(RomSet *rs);
/* 1 si están todas las ROMs del catálogo */
int romset_complete(const RomSet *rs);
/* índice en game->roms de la nth-ésima ROM de ese tipo, o -1 */
int romset_index(const GameDef *game, RomKind kind, int nth);

/* nombres que un origen de tipo carpeta debe probar: 2 por ROM (actual y antiguo) */
size_t romset_candidate_count(const GameDef *game);
const char *romset_candidate_name(const GameDef *game, size_t i);

/* Recorre el origen, verifica por CRC y rellena `rs`.  Devuelve cuántas ROMs faltan o están mal (0 = el
 * conjunto está completo).  `report` recibe un texto legible (en español) con lo que falta, lo que tiene
 * el CRC equivocado y lo que no se pudo leer; queda vacío si todo está bien. */
int romset_load(RomSet *rs, const RomSource *src, char *report, size_t report_size);

/* monta los chips de programa en la imagen plana que usa el código recompilado (game->prog_size bytes) */
void romset_build_program(const RomSet *rs, uint8_t *image);

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
