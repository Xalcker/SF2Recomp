/*
 * hostio.h - acceso a archivos del anfitrión con stdio (lo único que el núcleo no hace por sí mismo):
 * leer y escribir archivos, cargar las ROMs desde una carpeta o un zip, escribir PNG y WAV.  Es C estándar
 * sin SDL, así que lo usan igual el juego, los arneses de validación y las herramientas.
 */
#ifndef PLATFORM_HOSTIO_H
#define PLATFORM_HOSTIO_H

#include <stddef.h>
#include <stdint.h>
#include "romset.h"
#include "cps1.h"
#include "video.h"

#ifdef __cplusplus
extern "C" {
#endif

/* lee un archivo entero en un búfer de malloc (con un byte final a 0 que no cuenta en len); 0 o -1 */
int hostio_read_file(const char *path, uint8_t **data, size_t *len);
/* escribe el archivo de forma atómica (primero path.tmp y luego renombra); 0 o -1 */
int hostio_write_file(const char *path, const void *data, size_t len);

/* ¿es `path` un zip?  (por su contenido: marca "PK") */
int hostio_is_zip(const char *path);
/* carga las ROMs de `path`, que es un zip o una carpeta con los archivos sueltos (con los nombres de MAME,
 * antiguos o actuales).  Devuelve las ROMs que faltan (0 = completo) o -1 si no se puede abrir; `report`
 * lleva el motivo en español. */
int hostio_load_romset(const char *path, RomSet *rs, char *report, size_t report_size);

/* secuencia de entradas por cuadros (tools/inputs_sf2.txt): devuelve el número de eventos, -1 si no se puede
 * leer el archivo o -2 si tiene líneas mal formadas (que se describen por stderr) */
int cps1_load_inputs(Cps1 *m, const char *path);

/* escribe el cuadro como PNG; 0 o -1 */
int hostio_write_png(const char *path, const uint32_t *frame, int w, int h);
int hostio_write_video_png(const Video *v, const char *path);

#ifdef __cplusplus
}
#endif

#endif
