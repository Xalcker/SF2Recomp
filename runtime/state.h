/*
 * state.h - serialización de estados guardados.
 *
 * Una sola función por estructura sirve para guardar y para cargar (el mismo recorrido de campos con
 * StateStream, que escribe o lee según el modo), así que no pueden desincronizarse.  El formato es
 * little-endian explícito (los float van como su patrón de bits de 32), independiente del anfitrión: un
 * estado guardado en macOS se carga en Linux, Windows o el navegador.
 *
 * Los estados vienen de archivos y no son de fiar: al leer, un campo fuera de rango o un final prematuro
 * deja `error` y el que carga lo rechaza; ningún valor leído se usa antes de validarlo como índice.
 */
#ifndef RUNTIME_STATE_H
#define RUNTIME_STATE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StateStream {
    uint8_t *buf;          /* guardando: destino (NULL = solo medir); cargando: origen */
    size_t cap, pos;       /* capacidad / longitud disponible y posición actual */
    int saving;
    int error;             /* pegajoso: desbordamiento al guardar, final prematuro o valor inválido al cargar */
} StateStream;

void state_begin_save(StateStream *s, uint8_t *buf, size_t cap);
void state_begin_load(StateStream *s, const uint8_t *buf, size_t len);

void st_u8(StateStream *s, uint8_t *v);
void st_u16(StateStream *s, uint16_t *v);
void st_u32(StateStream *s, uint32_t *v);
void st_u64(StateStream *s, uint64_t *v);
void st_i32(StateStream *s, int32_t *v);
void st_f32(StateStream *s, float *v);
void st_bytes(StateStream *s, void *p, size_t n);
void st_u16s(StateStream *s, uint16_t *p, size_t n);
void st_f32s(StateStream *s, float *p, size_t n);
/* marca el flujo como inválido si no se cumple `ok` al cargar (validación de rangos) */
void st_check(StateStream *s, int ok);

#ifdef __cplusplus
}
#endif

#endif
