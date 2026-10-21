/*
 * sf2_port.h - supuestos de portabilidad del núcleo, comprobados al compilar.
 *
 * El núcleo (CPU recompilada, máquina, vídeo, sonido) es C11 / C++17 sin llamadas al sistema: no abre
 * archivos, no lee el entorno, no usa hilos ni relojes, y solo aprovecha la biblioteca estándar (memoria
 * dinámica, string.h, setjmp.h y stdio solo para los registros de validación que le da el anfitrión).
 * Lo que sí supone de la plataforma está aquí, con un _Static_assert por cada supuesto, de modo que un
 * compilador o una arquitectura que no lo cumpla falle al compilar y no en la partida.
 */
#ifndef RUNTIME_SF2_PORT_H
#define RUNTIME_SF2_PORT_H

#include <float.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#define SF2_STATIC_ASSERT(c, m) static_assert(c, m)
#else
#define SF2_STATIC_ASSERT(c, m) _Static_assert(c, m)
#endif

/* ---- tipos y aritmética ------------------------------------------------------------------------ */
SF2_STATIC_ASSERT(CHAR_BIT == 8, "el codigo supone bytes de 8 bits");
SF2_STATIC_ASSERT(sizeof(int) >= 4, "el codigo supone int de al menos 32 bits");
SF2_STATIC_ASSERT(sizeof(float) == 4 && sizeof(double) == 8, "se supone IEEE-754 binario32/binario64");
SF2_STATIC_ASSERT(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && DBL_MANT_DIG == 53, "se supone IEEE-754 binario32/binario64");
/* complemento a dos y desplazamiento aritmético a la derecha de enteros negativos (cpu.h, z80.c) */
SF2_STATIC_ASSERT((-1 & 3) == 3, "se supone complemento a dos");
SF2_STATIC_ASSERT((-1 >> 1) == -1, "se supone desplazamiento aritmetico a la derecha");
SF2_STATIC_ASSERT((-8 >> 1) == -4, "se supone desplazamiento aritmetico a la derecha");

/* ---- orden de bytes ------------------------------------------------------------------------------
 * Solo importa dentro de los registros de pares del Z80 (acceso por .b.l / .b.h a un uint16_t) y para el
 * formato de los estados guardados, que se escribe en little-endian explícito y no depende de esto. */
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define SF2_LITTLE_ENDIAN 1
#elif defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define SF2_LITTLE_ENDIAN 0
#elif defined(_WIN32) || defined(__wasm__) || defined(__x86_64__) || defined(__i386__) || defined(__aarch64__)
#define SF2_LITTLE_ENDIAN 1
#else
#error "no se puede determinar el orden de bytes del anfitrion; define SF2_LITTLE_ENDIAN a 0 o 1"
#endif

/* ---- atributos de compilador ---------------------------------------------------------------------- */
#if defined(__GNUC__) || defined(__clang__)
#define SF2_LIKELY(x)   __builtin_expect(!!(x), 1)
#define SF2_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define SF2_NORETURN    __attribute__((noreturn))
#define SF2_PRINTF(a, b) __attribute__((format(printf, a, b)))
#define SF2_ALWAYS_INLINE inline __attribute__((always_inline))
#define SF2_NOINLINE    __attribute__((noinline))
#else
#define SF2_LIKELY(x)   (x)
#define SF2_UNLIKELY(x) (x)
#define SF2_NORETURN
#define SF2_PRINTF(a, b)
#define SF2_ALWAYS_INLINE inline
#define SF2_NOINLINE
#endif

/* El código generado necesita llamadas en cola garantizadas (jmp entre funciones, D9): clang >= 13 y GCC >= 15
 * ofrecen __attribute__((musttail)).  Sin ellas una cadena de jmp consumiría pila. */
#if defined(__clang__)
#if __clang_major__ < 13
#error "el codigo generado necesita musttail (clang >= 13)"
#endif
#elif defined(__GNUC__)
#if __GNUC__ < 15
#error "el codigo generado necesita musttail (GCC >= 15); usa clang"
#endif
#else
#error "compilador no soportado: se necesita clang >= 13 o GCC >= 15 (musttail)"
#endif

#endif
