/*
 * harness.h - lo que añaden los arneses de validación a la máquina del núcleo (runtime/machine.h): cobertura
 * dinámica y anillo de traza por instrucción, la red de seguridad de Musashi para direcciones no recompiladas
 * (D19) y el informe final.  El ejecutable del juego no lleva nada de esto (D13).
 *
 * Red de seguridad: si el control llega a una dirección sin función recompilada, el estado de la CPU pasa a
 * Musashi, que interpreta sobre la misma memoria hasta que el PC vuelve a una dirección de la tabla; entonces
 * el estado vuelve al contexto recompilado y se registra el episodio (dirección de entrada, de salida e
 * instrucciones), para añadir `entry` a tools/hints.txt.
 */
#ifndef HARNESS_HARNESS_H
#define HARNESS_HARNESS_H

#include <stdint.h>
#include "machine.h"

/* instala en `mc` el gancho de cobertura/anillo y, si `fallback`, la red de seguridad de Musashi (y prepara Musashi).
 * Sin red de seguridad (por omisión desde el hito 6) una dirección sin función recompilada detiene la máquina con el
 * mismo mensaje que el juego. */
void harness_install(Machine *mc, int fallback);
/* traza "pc ciclo cuadro" por instrucción en los cuadros [from, to] (evaluación del reloj frente a MAME, hito 6) */
void harness_set_cycle_trace(FILE *f, uint64_t from, uint64_t to);
/* registra cada despacho por la tabla (origen, destino, veces) y lo escribe en `path` al terminar (NULL = no) */
void harness_set_dispatch_log(Machine *mc, const char *path);
/* rutas opcionales del informe: lista de direcciones ejecutadas y tabla de E/S en markdown */
void harness_set_report_paths(const char *cov_path, const char *io_table_path);
/* mensaje de progreso cada 600 cuadros (llamar desde on_sample) */
void harness_progress(Machine *mc, uint64_t frame);
/* ejecuta la máquina hasta el final y termina el proceso: imprime el informe, llama a `on_finish` (si no es NULL)
 * y sale con 0 si acabó bien o con 2 si el código recompilado falló (con registros y anillo de traza) */
void harness_run(Machine *mc, void (*on_finish)(Machine *mc, const char *why));

#endif
