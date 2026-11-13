"""Variantes de SF2 que el proyecto conoce: la parte que necesitan las herramientas de Python (D63).

Es un espejo de runtime/game.c (que manda): id del set de MAME, tamaño de la imagen de programa y cómo se monta a
partir de los chips (ROM_START de src/mame/capcom/cps1.cpp en MAME 0.289).  Cada chip de programa es
(nombre actual, nombre antiguo, CRC-32, tamaño, destino, lane), con lane 0/1 = bytes pares/impares de un par
(ROM_LOAD16_BYTE, destino = base del par) y lane 2 = palabras con los dos bytes invertidos (ROM_LOAD16_WORD_SWAP).
"""

LANE_WORD = 2

GAMES = {
    "sf2": {
        "title": "Street Fighter II: The World Warrior (World 910522)",
        "prog_size": 0x100000,
        "cpu_hz": 10_000_000,
        "frame_cycles": 167_680,
        "program": [
            ("sf2e_30g.11e", "sf2e.30g",    0xfe39ee33, 0x20000, 0x00000, 0),
            ("sf2e_37g.11f", "sf2e.37g",    0xfb92cd74, 0x20000, 0x00000, 1),
            ("sf2e_31g.12e", "sf2e.31g",    0x69a0a301, 0x20000, 0x40000, 0),
            ("sf2e_38g.12f", "sf2e.38g",    0x5e22db70, 0x20000, 0x40000, 1),
            ("sf2e_28g.9e",  "sf2e.28g",    0x8bf9f1e5, 0x20000, 0x80000, 0),
            ("sf2e_35g.9f",  "sf2e.35g",    0x626ef934, 0x20000, 0x80000, 1),
            ("sf2_29b.10e",  "sf2_29a.bin", 0xbb4af315, 0x20000, 0xC0000, 0),
            ("sf2_36b.10f",  "sf2_36a.bin", 0xc02a13eb, 0x20000, 0xC0000, 1),
        ],
        "hints": "hints.txt",              # en tools/
        "mods": True,                      # recompiler/mod_patches.txt
    },
    "sf2ce": {
        "title": "Street Fighter II': Champion Edition (World 920513)",
        "prog_size": 0x180000,
        "cpu_hz": 12_000_000,
        "frame_cycles": 201_216,
        "program": [
            ("s92e_23b.8f", "s92e_23b.8f", 0x0aaa1a3a, 0x80000, 0x000000, LANE_WORD),
            ("s92_22b.7f",  "s92_22b.7f",  0x2bbe15ed, 0x80000, 0x080000, LANE_WORD),
            ("s92_21a.6f",  "s92_21a.6f",  0x925a7877, 0x80000, 0x100000, LANE_WORD),
        ],
        "hints": "hints_sf2ce.txt",
        "mods": True,                      # recompiler/mod_patches_sf2ce.txt (D68)
    },
    "sf2hf": {
        "title": "Street Fighter II': Hyper Fighting (World 921209)",
        "prog_size": 0x180000,
        "cpu_hz": 12_000_000,
        "frame_cycles": 201_216,
        "program": [
            ("s2te_23.8f", "s2te_23.8f", 0x2dd72514, 0x80000, 0x000000, LANE_WORD),
            ("s2te_22.7f", "s2te_22.7f", 0xaea6e035, 0x80000, 0x080000, LANE_WORD),
            ("s2te_21.6f", "s2te_21.6f", 0xfd200288, 0x80000, 0x100000, LANE_WORD),
        ],
        "hints": "hints_sf2hf.txt",
        "mods": True,                      # recompiler/mod_patches_sf2hf.txt (D68)
    },
    "sfzch": {
        "title": "Street Fighter Zero (CPS Changer, Japan 951020)",
        "prog_size": 0x200000,
        "cpu_hz": 12_000_000,
        "frame_cycles": 201_216,
        "program": [
            ("sfzch23", "sfzch23", 0x1140743f, 0x80000, 0x000000, LANE_WORD),
            ("sfza22",  "sfza22",  0x8d9b2480, 0x80000, 0x080000, LANE_WORD),
            ("sfzch21", "sfzch21", 0x5435225d, 0x80000, 0x100000, LANE_WORD),
            ("sfza20",  "sfza20",  0x806e8f38, 0x80000, 0x180000, LANE_WORD),
        ],
        "hints": "hints_sfzch.txt",
        "mods": False,
    },
}


def game(gid):
    if gid not in GAMES:
        raise SystemExit(f"juego desconocido: {gid} (conocidos: {', '.join(sorted(GAMES))})")
    return GAMES[gid]


def build_program(gid, chips):
    """Monta la imagen de programa; `chips` = {índice del chip: bytes}.  Devuelve un bytearray de prog_size bytes."""
    g = game(gid)
    img = bytearray(g["prog_size"])
    for i, (_, _, _, size, dest, lane) in enumerate(g["program"]):
        data = chips[i]
        if lane == LANE_WORD:
            img[dest:dest + size:2] = data[1::2]
            img[dest + 1:dest + size:2] = data[0::2]
        else:
            img[dest + lane:dest + 2 * size:2] = data
    return img
