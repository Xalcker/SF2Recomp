/* game.c - descriptores de las variantes de SF2 (ver game.h).  Datos de MAME 0.289: src/mame/capcom/cps1.cpp
 * (ROM_START, cps1_10MHz / cps1_12MHz) y cps1_v.cpp (tabla de configuración, CPS_B_*, mapper_*). */
#include "game.h"
#include <string.h>

/* {"sf2", CPS_B_11, mapper_STF29, 0x36}: CPS_B_11 = 0x32,0x0401, __not_applicable__, 0x26,{0x28,0x2a,0x2c,0x2e},0x30,
 * {0x08,0x10,0x20,0x00,0x00} */
static const CpsbConfig cpsb_11 = {
    "CPS-B-11", 0x32, 0x0401, -1, -1, -1, -1, 0x36,
    0x26, { 0x28, 0x2a, 0x2c, 0x2e }, 0x30, { 0x08, 0x10, 0x20 },
};

/* {"sf2ce", CPS_B_21_DEF, mapper_S9263B, 0x36}: CPS_B_21_DEF = 0x32,-1, 0x00,0x02,0x04,0x06, 0x08,-1,-1, 0x26,
 * {0x28,0x2a,0x2c,0x2e},0x30, {0x02,0x04,0x08,0x30,0x30} (las máscaras 3 y 4 son las estrellas, que SF2 no usa) */
static const CpsbConfig cpsb_21_def = {
    "CPS-B-21", 0x32, -1, 0x00, 0x02, 0x04, 0x06, 0x36,
    0x26, { 0x28, 0x2a, 0x2c, 0x2e }, 0x30, { 0x02, 0x04, 0x08 },
};

#define T_SPR 1     /* GFXTYPE_* de gfx.h */
#define T_S1  2
#define T_S2  4
#define T_S3  8

/* mapper_STF29 (B-board 90629B-3) y mapper_S9263B (B-board 91635B-2): las dos PAL dan los mismos rangos */
static const GfxMapper mapper_stf29 = {
    "STF29", { 0x8000, 0x8000, 0x8000, 0 }, 6, {
        { T_SPR, 0x00000, 0x07fff, 0 },
        { T_SPR, 0x08000, 0x0ffff, 1 },
        { T_SPR, 0x10000, 0x11fff, 2 },
        { T_S3,  0x02000, 0x03fff, 2 },
        { T_S1,  0x04000, 0x04fff, 2 },
        { T_S2,  0x05000, 0x07fff, 2 },
    },
};
static const GfxMapper mapper_s9263b = {
    "S9263B", { 0x8000, 0x8000, 0x8000, 0 }, 6, {
        { T_SPR, 0x00000, 0x07fff, 0 },
        { T_SPR, 0x08000, 0x0ffff, 1 },
        { T_SPR, 0x10000, 0x11fff, 2 },
        { T_S3,  0x02000, 0x03fff, 2 },
        { T_S1,  0x04000, 0x04fff, 2 },
        { T_S2,  0x05000, 0x07fff, 2 },
    },
};

/* ROM_START( sf2 ) */
static const RomInfo roms_sf2[] = {
    /* programa del 68000: pares (par, impar) cada 256 KiB */
    { "sf2e_30g.11e", "sf2e.30g",    0xfe39ee33u, 0x20000, ROMKIND_PROGRAM, 0x00000, 0 },
    { "sf2e_37g.11f", "sf2e.37g",    0xfb92cd74u, 0x20000, ROMKIND_PROGRAM, 0x00000, 1 },
    { "sf2e_31g.12e", "sf2e.31g",    0x69a0a301u, 0x20000, ROMKIND_PROGRAM, 0x40000, 0 },
    { "sf2e_38g.12f", "sf2e.38g",    0x5e22db70u, 0x20000, ROMKIND_PROGRAM, 0x40000, 1 },
    { "sf2e_28g.9e",  "sf2e.28g",    0x8bf9f1e5u, 0x20000, ROMKIND_PROGRAM, 0x80000, 0 },
    { "sf2e_35g.9f",  "sf2e.35g",    0x626ef934u, 0x20000, ROMKIND_PROGRAM, 0x80000, 1 },
    { "sf2_29b.10e",  "sf2_29a.bin", 0xbb4af315u, 0x20000, ROMKIND_PROGRAM, 0xc0000, 0 },
    { "sf2_36b.10f",  "sf2_36a.bin", 0xc02a13ebu, 0x20000, ROMKIND_PROGRAM, 0xc0000, 1 },
    /* gráficos: ROM_LOAD64_WORD, una palabra de cada ROM cada 8 bytes de la región de 6 MiB */
    { "sf2-5m.4a",    "sf2_06.bin",  0x22c9cc8eu, 0x80000, ROMKIND_GFX, 0x000000, 0 },
    { "sf2-7m.6a",    "sf2_08.bin",  0x57213be8u, 0x80000, ROMKIND_GFX, 0x000002, 0 },
    { "sf2-1m.3a",    "sf2_05.bin",  0xba529b4fu, 0x80000, ROMKIND_GFX, 0x000004, 0 },
    { "sf2-3m.5a",    "sf2_07.bin",  0x4b1b33a8u, 0x80000, ROMKIND_GFX, 0x000006, 0 },
    { "sf2-6m.4c",    "sf2_15.bin",  0x2c7e2229u, 0x80000, ROMKIND_GFX, 0x200000, 0 },
    { "sf2-8m.6c",    "sf2_17.bin",  0xb5548f17u, 0x80000, ROMKIND_GFX, 0x200002, 0 },
    { "sf2-2m.3c",    "sf2_14.bin",  0x14b84312u, 0x80000, ROMKIND_GFX, 0x200004, 0 },
    { "sf2-4m.5c",    "sf2_16.bin",  0x5e9cd89au, 0x80000, ROMKIND_GFX, 0x200006, 0 },
    { "sf2-13m.4d",   "sf2_25.bin",  0x994bfa58u, 0x80000, ROMKIND_GFX, 0x400000, 0 },
    { "sf2-15m.6d",   "sf2_27.bin",  0x3e66ad9du, 0x80000, ROMKIND_GFX, 0x400002, 0 },
    { "sf2-9m.3d",    "sf2_24.bin",  0xc1befaa8u, 0x80000, ROMKIND_GFX, 0x400004, 0 },
    { "sf2-11m.5d",   "sf2_26.bin",  0x0627c831u, 0x80000, ROMKIND_GFX, 0x400006, 0 },
    /* sonido */
    { "sf2_9.12a",    "sf2_09.bin",  0xa4823a1bu, 0x10000, ROMKIND_Z80, 0x00000, 0 },
    { "sf2_18.11c",   "sf2_18.bin",  0x7f162009u, 0x20000, ROMKIND_OKI, 0x00000, 0 },
    { "sf2_19.12c",   "sf2_19.bin",  0xbeade53fu, 0x20000, ROMKIND_OKI, 0x20000, 0 },
};

/* ROM_START( sf2ce ): programa en tres ROMs de palabras (ROM_LOAD16_WORD_SWAP) de 512 KiB */
static const RomInfo roms_sf2ce[] = {
    { "s92e_23b.8f", "s92e_23b.8f", 0x0aaa1a3au, 0x80000, ROMKIND_PROGRAM, 0x000000, ROMLANE_WORD },
    { "s92_22b.7f",  "s92_22b.7f",  0x2bbe15edu, 0x80000, ROMKIND_PROGRAM, 0x080000, ROMLANE_WORD },
    { "s92_21a.6f",  "s92_21a.6f",  0x925a7877u, 0x80000, ROMKIND_PROGRAM, 0x100000, ROMLANE_WORD },
    { "s92-1m.3a",   "s92-1m.3a",   0x03b0d852u, 0x80000, ROMKIND_GFX, 0x000000, 0 },
    { "s92-3m.5a",   "s92-3m.5a",   0x840289ecu, 0x80000, ROMKIND_GFX, 0x000002, 0 },
    { "s92-2m.4a",   "s92-2m.4a",   0xcdb5f027u, 0x80000, ROMKIND_GFX, 0x000004, 0 },
    { "s92-4m.6a",   "s92-4m.6a",   0xe2799472u, 0x80000, ROMKIND_GFX, 0x000006, 0 },
    { "s92-5m.7a",   "s92-5m.7a",   0xba8a2761u, 0x80000, ROMKIND_GFX, 0x200000, 0 },
    { "s92-7m.9a",   "s92-7m.9a",   0xe584bfb5u, 0x80000, ROMKIND_GFX, 0x200002, 0 },
    { "s92-6m.8a",   "s92-6m.8a",   0x21e3f87du, 0x80000, ROMKIND_GFX, 0x200004, 0 },
    { "s92-8m.10a",  "s92-8m.10a",  0xbefc47dfu, 0x80000, ROMKIND_GFX, 0x200006, 0 },
    { "s92-10m.3c",  "s92-10m.3c",  0x960687d5u, 0x80000, ROMKIND_GFX, 0x400000, 0 },
    { "s92-12m.5c",  "s92-12m.5c",  0x978ecd18u, 0x80000, ROMKIND_GFX, 0x400002, 0 },
    { "s92-11m.4c",  "s92-11m.4c",  0xd6ec9a0au, 0x80000, ROMKIND_GFX, 0x400004, 0 },
    { "s92-13m.6c",  "s92-13m.6c",  0xed2c67f6u, 0x80000, ROMKIND_GFX, 0x400006, 0 },
    { "s92_09.11a",  "s92_09.11a",  0x08f6b60eu, 0x10000, ROMKIND_Z80, 0x00000, 0 },
    { "s92_18.11c",  "s92_18.11c",  0x7f162009u, 0x20000, ROMKIND_OKI, 0x00000, 0 },
    { "s92_19.12c",  "s92_19.12c",  0xbeade53fu, 0x20000, ROMKIND_OKI, 0x20000, 0 },
};

/* ROM_START( sf2hf ): otro programa; gráficos y sonido, los de sf2ce */
static const RomInfo roms_sf2hf[] = {
    { "s2te_23.8f",  "s2te_23.8f",  0x2dd72514u, 0x80000, ROMKIND_PROGRAM, 0x000000, ROMLANE_WORD },
    { "s2te_22.7f",  "s2te_22.7f",  0xaea6e035u, 0x80000, ROMKIND_PROGRAM, 0x080000, ROMLANE_WORD },
    { "s2te_21.6f",  "s2te_21.6f",  0xfd200288u, 0x80000, ROMKIND_PROGRAM, 0x100000, ROMLANE_WORD },
    { "s92-1m.3a",   "s92-1m.3a",   0x03b0d852u, 0x80000, ROMKIND_GFX, 0x000000, 0 },
    { "s92-3m.5a",   "s92-3m.5a",   0x840289ecu, 0x80000, ROMKIND_GFX, 0x000002, 0 },
    { "s92-2m.4a",   "s92-2m.4a",   0xcdb5f027u, 0x80000, ROMKIND_GFX, 0x000004, 0 },
    { "s92-4m.6a",   "s92-4m.6a",   0xe2799472u, 0x80000, ROMKIND_GFX, 0x000006, 0 },
    { "s92-5m.7a",   "s92-5m.7a",   0xba8a2761u, 0x80000, ROMKIND_GFX, 0x200000, 0 },
    { "s92-7m.9a",   "s92-7m.9a",   0xe584bfb5u, 0x80000, ROMKIND_GFX, 0x200002, 0 },
    { "s92-6m.8a",   "s92-6m.8a",   0x21e3f87du, 0x80000, ROMKIND_GFX, 0x200004, 0 },
    { "s92-8m.10a",  "s92-8m.10a",  0xbefc47dfu, 0x80000, ROMKIND_GFX, 0x200006, 0 },
    { "s92-10m.3c",  "s92-10m.3c",  0x960687d5u, 0x80000, ROMKIND_GFX, 0x400000, 0 },
    { "s92-12m.5c",  "s92-12m.5c",  0x978ecd18u, 0x80000, ROMKIND_GFX, 0x400002, 0 },
    { "s92-11m.4c",  "s92-11m.4c",  0xd6ec9a0au, 0x80000, ROMKIND_GFX, 0x400004, 0 },
    { "s92-13m.6c",  "s92-13m.6c",  0xed2c67f6u, 0x80000, ROMKIND_GFX, 0x400006, 0 },
    { "s92_09.11a",  "s92_09.11a",  0x08f6b60eu, 0x10000, ROMKIND_Z80, 0x00000, 0 },
    { "s92_18.11c",  "s92_18.11c",  0x7f162009u, 0x20000, ROMKIND_OKI, 0x00000, 0 },
    { "s92_19.12c",  "s92_19.12c",  0xbeade53fu, 0x20000, ROMKIND_OKI, 0x20000, 0 },
};

#define COUNT(a) ((int)(sizeof (a) / sizeof (a)[0]))

/* RAM de los mods (D68).  WW: la de siempre (training.c, video.c, docs/HITBOXES.md, docs/ESTEREO.md).  CE y HF comparten la suya: deducida
 * del código (las mismas instrucciones de WW, emparejadas por el código de alrededor, usan estos desplazamientos de a5) y comprobada
 * con la partida de tools/inputs_sf2.txt (bitácora 2026-11-12-variantes-fase-3). */
static const ModRam mod_ram_sf2 = {
    0xFF8001u, 0xFF8005u, 0xFF8009u, 0xFF82DDu, 0xFF83C0u, 0xFF86C0u, 0xFF8ACEu, 0xFF8ACFu,
    0xFF8BD8u, 0xFF8BDCu, 0xFF89D0u, 0xFF89D2u, 0xFF8668u, 0xFF938Au, 0xFF81A2u, 0xFF8024u, 0xFF9000u, 0xFFE000u,
};
static const ModRam mod_ram_sf2ce = {
    0xFF8001u, 0xFF8005u, 0xFF8009u, 0xFF82D9u, 0xFF83B8u, 0xFF86B8u, 0xFF8ABEu, 0xFF8ABFu,
    0xFF8BC4u, 0xFF8BC8u, 0xFF89C4u, 0xFF89C6u, 0xFF8660u, 0xFF9376u, 0xFF819Eu, 0xFF8024u, 0xFF9000u, 0xFFE000u,
};

const GameDef game_sf2 = {
    "sf2", "Street Fighter II: The World Warrior (World 910522)",
    10000000u, 167680u,                 /* 10 MHz * 512 * 262 / 8 MHz */
    0x100000u, 0x600000u, &cpsb_11, &mapper_stf29, COUNT(roms_sf2), roms_sf2,
    1, 0x000AB8u, 0x000AD0u, 0xFF801Cu, 1, &mod_ram_sf2, 1,
};

const GameDef game_sf2ce = {
    "sf2ce", "Street Fighter II': Champion Edition (World 920513)",
    12000000u, 201216u,                 /* 12 MHz * 512 * 262 / 8 MHz */
    0x180000u, 0x600000u, &cpsb_21_def, &mapper_s9263b, COUNT(roms_sf2ce), roms_sf2ce,
    /* fase 1 (bitácora 2026-11-12-variantes-fase-1): manejador de la IRQ2 en 0x5C6; no lee el CPS-B, así que el punto de muestra
     * es su primer acceso de E/S, la escritura de la base de OBJ (`move.w 0x26(a5),$800100.l`) en 0x5DA; contador de cuadros
     * 0x1C(a5) (0x6AC) y bandera de VBlank 0xFF020E (0x6B0) en las mismas direcciones que WW.  Mods con recompiler/mod_patches_sf2ce.txt
     * y su RAM (D68); sin herramientas de desarrollo. */
    1, 0x0005C6u, 0x0005DAu, 0xFF801Cu, 1, &mod_ram_sf2ce, 0,
};

const GameDef game_sf2hf = {
    "sf2hf", "Street Fighter II': Hyper Fighting (World 921209)",
    12000000u, 201216u,                 /* cps1_12MHz, como sf2ce */
    0x180000u, 0x600000u, &cpsb_21_def, &mapper_s9263b, COUNT(roms_sf2hf), roms_sf2hf,
    /* fase 2 (bitácora 2026-11-12-variantes-fase-2): manejador de la IRQ2 en 0x1D1E0, que escribe registros de vídeo antes de guardar
     * los registros (movem en 0x1D24C); el punto de muestra es el primer acceso de E/S tras el movem, para que el SR y el PC apilados
     * estén en SP+60/62 como en las demás: la escritura de 0x800122 en 0x1D264.  Contador de cuadros 0xFF801C (0x1D52E); la bandera
     * de VBlank se mueve a 0xFF04CE (0x1D532).  Mods como CE (recompiler/mod_patches_sf2hf.txt, la RAM de CE). */
    1, 0x01D1E0u, 0x01D264u, 0xFF801Cu, 1, &mod_ram_sf2ce, 0,
};

static const GameDef *const games[] = { &game_sf2, &game_sf2ce, &game_sf2hf };

int game_count(void) { return COUNT(games); }

const GameDef *game_at(int i) { return i >= 0 && i < COUNT(games) ? games[i] : NULL; }

const GameDef *game_find(const char *id) {
    for (int i = 0; id && i < COUNT(games); i++)
        if (strcmp(games[i]->id, id) == 0)
            return games[i];
    return NULL;
}
