/*
 * render_dump.c - renderiza un cuadro a partir de los volcados que tools/mame_snap.lua guarda de MAME
 * (RAM gráfica, registros CPS-A/CPS-B, tabla de objetos latcheada y paleta), para validar el renderizador
 * de runtime/video.c aislado del resto de la máquina: si la imagen coincide con cuadro_N.png de MAME, el
 * dibujo es correcto y cualquier diferencia en la ejecución completa viene del estado o de la temporización.
 *
 *   render_dump gfx_N.bin regs_N.bin obj_N.bin pal_N.bin salida.png [--gfx carpeta|zip]
 *
 * pal_N.bin trae los pens ya convertidos a RGB por MAME; se usan tal cual (sin pasar por la conversión de
 * brillo del runtime) para que esta prueba solo mida capas, sprites y prioridades.  Con --pal-runtime la
 * paleta se reconstruye con mem_palette_copy a partir de la RAM gráfica y los registros, lo que además
 * comprueba la copia de paleta y la fórmula de brillo (válido si el juego no tocó la paleta en la RAM
 * gráfica entre la última escritura de la base de paleta y la captura).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mem.h"
#include "gfx.h"
#include "video.h"
#include "hostio.h"

static int read_file(const char *path, void *buf, size_t n) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    if (got != n) { fprintf(stderr, "%s: %zu bytes, esperados %zu\n", path, got, n); return -1; }
    return 0;
}

int main(int argc, char **argv) {
    const char *files[5] = { 0 }, *gfx_dir = "sf2";
    int nfiles = 0, pal_runtime = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--gfx") && i + 1 < argc) gfx_dir = argv[++i];
        else if (!strcmp(argv[i], "--pal-runtime")) pal_runtime = 1;
        else if (nfiles < 5) files[nfiles++] = argv[i];
    }
    if (nfiles != 5) {
        fprintf(stderr, "uso: %s gfx_N.bin regs_N.bin obj_N.bin pal_N.bin salida.png [--gfx dir] [--pal-runtime]\n", argv[0]);
        return 1;
    }
    static Gfx gfx;
    RomSet rs;
    char report[2048];
    romset_init(&rs);
    if (hostio_load_romset(gfx_dir, &rs, report, sizeof report) != 0) { fprintf(stderr, "%s: %s", gfx_dir, report); return 1; }
    if (gfx_init(&gfx, &rs) < 0) return 1;
    romset_free(&rs);
    static uint8_t rom[ROM_SIZE];
    static Mem mem;
    mem_init(&mem, rom, "volcado");
    if (read_file(files[0], mem.gfxram, GFXRAM_SIZE) < 0) return 1;
    uint8_t regs[0x80];
    if (read_file(files[1], regs, sizeof regs) < 0) return 1;
    for (int i = 0; i < 0x20; i++) {
        mem.cps_a[i] = (uint16_t)((regs[2 * i] << 8) | regs[2 * i + 1]);
        mem.cps_b[i] = (uint16_t)((regs[0x40 + 2 * i] << 8) | regs[0x40 + 2 * i + 1]);
    }
    static Video video;
    video_init(&video, &gfx);
    uint8_t obj[OBJ_WORDS * 2];
    if (read_file(files[2], obj, sizeof obj) < 0) return 1;
    for (int i = 0; i < OBJ_WORDS; i++) video.obj[i] = (uint16_t)((obj[2 * i] << 8) | obj[2 * i + 1]);
    if (pal_runtime) {
        mem_palette_copy(&mem);
    } else {
        uint8_t pal[PALETTE_ENTRIES * 4];
        if (read_file(files[3], pal, sizeof pal) < 0) return 1;
        for (int i = 0; i < PALETTE_ENTRIES; i++)
            video.rgb[i] = ((uint32_t)pal[4 * i + 1] << 16) | ((uint32_t)pal[4 * i + 2] << 8) | pal[4 * i + 3];
        video.rgb_fixed = 1;
    }
    video_render(&video, &mem);
    if (hostio_write_video_png(&video, files[4]) < 0) return 1;
    printf("%s: orden de capas %d,%d,%d,%d, habilitadas %d%d%d, %d sprites, rowscroll %d, %d pixeles con prioridad\n",
           files[4], video.order[0], video.order[1], video.order[2], video.order[3], video.enabled[1], video.enabled[2],
           video.enabled[3], video.sprites, video.rowscroll, video.marked);
    return 0;
}
