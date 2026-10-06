#!/usr/bin/env python3
"""Builds the 3D model of the "Archipelago Item": the Archipelago logo (six colored circles on a
ring) as six glossy spheres, in the game's own formats.

    python tools/models/make_ap_item.py [-o overlay/res/Object/O_gD_ap.arc]

Output: a RARC archive holding one J3D model (bmdr/O_gD_ap.bmd, file index 3, the index the item
tables in src/item.cpp use). The mod bundle overlays it into the game as /res/Object/O_gD_ap.arc,
a file the disc doesn't have, so nothing of the game is replaced.

The model is deliberately simple, so it renders the same everywhere: one joint, one material and
one shape, positions (F32) and vertex colors (RGBA8) only, no textures, no lighting (the shading
is baked into the vertex colors and only depends on height, so it looks right while the item
spins around its vertical axis). The material is a single TEV stage that outputs the vertex
color, opaque, depth tested and written, both faces drawn.

Formats follow the game's loaders in Dusklight (JSystem J3DModelLoader v26 / J3DMaterialFactory /
J3DShapeFactory, JKRArchive); tools/models/check_ap_item.py reads the result back.
"""

import argparse
import math
import struct
from pathlib import Path

ARC_NAME = "O_gD_ap"
BMD_NAME = "O_gD_ap.bmd"

# The logo's circles, clockwise from the top (colors sampled from Archipelago's icon)
LOGO = [
    (90, (201, 118, 130)),    # red
    (30, (117, 194, 117)),    # green
    (-30, (202, 148, 194)),   # pink
    (-90, (217, 160, 125)),   # orange
    (-150, (118, 126, 189)),  # blue
    (150, (238, 227, 145)),   # yellow
]
RING_RADIUS = 8.6
SPHERE_RADIUS = 5.3
# The ring leans back: the bottom circle is in front and the top one behind, which gives the
# overlaps of the flat logo
TILT_DEGREES = 25.0
LATITUDES = 12   # rings from pole to pole
LONGITUDES = 22  # segments around

# GX enums
GX_VA_POS, GX_VA_CLR0, GX_VA_NULL = 9, 11, 0xFF
GX_POS_XYZ, GX_CLR_RGBA = 1, 1
GX_F32, GX_RGBA8 = 4, 5
GX_INDEX16 = 3
GX_DRAW_TRIANGLES = 0x90
GX_CULL_NONE = 0
GX_SRC_REG, GX_SRC_VTX = 0, 1
GX_DF_CLAMP, GX_AF_NONE = 2, 2
GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0 = 0xFF, 0xFF, 4
GX_CC_RASC, GX_CC_ZERO = 10, 15
GX_CA_RASA, GX_CA_ZERO = 5, 7
GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TEVPREV = 0, 0, 0, 0
GX_ALWAYS, GX_LEQUAL, GX_AOP_AND = 7, 3, 0
GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY = 0, 1, 0, 3

# Scene graph node types (J3DModelHierarchy)
H_END, H_OPEN, H_CLOSE, H_JOINT, H_MATERIAL, H_SHAPE = 0x00, 0x01, 0x02, 0x10, 0x11, 0x12


def align(data: bytearray, boundary: int, fill: bytes = b"\0") -> None:
    while len(data) % boundary:
        data += fill


def name_hash(name: str) -> int:
    """JUTNameTab / JKRArchive name hash."""
    value = 0
    for ch in name.encode("ascii"):
        value = (value * 3 + ch) & 0xFFFF
    return value


def name_table(names: list[str]) -> bytes:
    """ResNTAB: count, entries (hash, offset from the table start), strings."""
    head = struct.pack(">HH", len(names), 0xFFFF)
    strings = bytearray()
    entries = bytearray()
    base = 4 + 4 * len(names)
    for name in names:
        entries += struct.pack(">HH", name_hash(name), base + len(strings))
        strings += name.encode("ascii") + b"\0"
    return head + entries + strings


class Block:
    """A J3D block: header fields, then data whose offsets (from the block start) the header
    points to. Offsets are fixed up when the data is added."""

    def __init__(self, magic: bytes, header_size: int):
        self.data = bytearray(magic + b"\0\0\0\0" + bytes(header_size - 8))

    def put(self, at: int, fmt: str, *values) -> None:
        struct.pack_into(">" + fmt, self.data, at, *values)

    def add(self, payload: bytes, alignment: int = 4, pointer_at: int | None = None) -> int:
        align(self.data, alignment)
        offset = len(self.data)
        self.data += payload
        if pointer_at is not None:
            self.put(pointer_at, "I", offset)
        return offset

    def finish(self) -> bytes:
        align(self.data, 32)
        self.put(4, "I", len(self.data))
        return bytes(self.data)


# ---------------------------------------------------------------------------------------------
# Geometry

def build_mesh():
    """Returns positions, colors (RGBA tuples, one per position) and triangles (index triples)."""
    tilt = math.radians(TILT_DEGREES)
    positions, colors, triangles = [], [], []
    for angle, base in LOGO:
        cx = RING_RADIUS * math.cos(math.radians(angle))
        cy = RING_RADIUS * math.sin(math.radians(angle))
        center = (cx, cy * math.cos(tilt), -cy * math.sin(tilt))
        first = len(positions)

        def vertex(normal):
            nx, ny, nz = normal
            positions.append((center[0] + SPHERE_RADIUS * nx, center[1] + SPHERE_RADIUS * ny,
                              center[2] + SPHERE_RADIUS * nz))
            # Baked light from above (the same from every side, as the item spins): darker
            # underneath, a soft white highlight on top
            shade = 0.70 + 0.30 * ny
            highlight = max(0.0, ny) ** 12 * 0.45
            rgb = []
            for channel in base:
                value = min(255.0, channel * shade * 1.06)
                rgb.append(round(value + (255.0 - value) * highlight))
            colors.append((*rgb, 255))

        vertex((0.0, 1.0, 0.0))  # north pole
        for lat in range(1, LATITUDES):
            theta = math.pi * lat / LATITUDES
            for lon in range(LONGITUDES):
                phi = 2.0 * math.pi * lon / LONGITUDES
                vertex((math.sin(theta) * math.cos(phi), math.cos(theta), math.sin(theta) * math.sin(phi)))
        vertex((0.0, -1.0, 0.0))  # south pole
        north, south = first, len(positions) - 1

        def ring(lat, lon):
            return first + 1 + (lat - 1) * LONGITUDES + lon % LONGITUDES

        for lon in range(LONGITUDES):
            triangles.append((north, ring(1, lon + 1), ring(1, lon)))
            triangles.append((south, ring(LATITUDES - 1, lon), ring(LATITUDES - 1, lon + 1)))
        for lat in range(1, LATITUDES - 1):
            for lon in range(LONGITUDES):
                a, b = ring(lat, lon), ring(lat, lon + 1)
                c, d = ring(lat + 1, lon), ring(lat + 1, lon + 1)
                triangles.append((a, b, d))
                triangles.append((a, d, c))

    # Origin at the bottom center, like the game's item models (Link holds them from below)
    min_y = min(p[1] for p in positions)
    positions = [(x, y - min_y, z) for x, y, z in positions]
    return positions, colors, triangles


def bounds(positions):
    lo = tuple(min(p[i] for p in positions) for i in range(3))
    hi = tuple(max(p[i] for p in positions) for i in range(3))
    radius = max(math.sqrt(x * x + y * y + z * z) for x, y, z in positions)
    return lo, hi, radius


# ---------------------------------------------------------------------------------------------
# J3D model (bmd3)

def inf1(vertex_count: int) -> bytes:
    block = Block(b"INF1", 0x18)
    block.put(0x08, "HH", 0, 0xFFFF)          # flags (basic matrix calculation), padding
    block.put(0x0C, "II", 1, vertex_count)    # matrix groups, vertices
    hierarchy = [(H_JOINT, 0), (H_OPEN, 0), (H_MATERIAL, 0), (H_OPEN, 0), (H_SHAPE, 0),
                 (H_CLOSE, 0), (H_CLOSE, 0), (H_END, 0)]
    block.add(b"".join(struct.pack(">HH", *node) for node in hierarchy), 4, 0x14)
    return block.finish()


def vtx1(positions, colors) -> bytes:
    block = Block(b"VTX1", 0x40)
    formats = b"".join(struct.pack(">IIIBxxx", attr, count, comp, 0) for attr, count, comp in [
        (GX_VA_POS, GX_POS_XYZ, GX_F32),
        (GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8),
    ]) + struct.pack(">IIIBxxx", GX_VA_NULL, 1, 0, 0)
    block.add(formats, 4, 0x08)
    block.add(b"".join(struct.pack(">fff", *p) for p in positions), 32, 0x0C)        # POS
    block.add(b"".join(struct.pack(">BBBB", *c) for c in colors), 32, 0x18)          # CLR0
    return block.finish()


def evp1() -> bytes:
    block = Block(b"EVP1", 0x1C)
    block.put(0x08, "HH", 0, 0xFFFF)  # no weighted matrices
    return block.finish()


def drw1() -> bytes:
    block = Block(b"DRW1", 0x14)
    block.put(0x08, "HH", 1, 0xFFFF)
    block.add(bytes([0]), 1, 0x0C)                 # matrix 0 is not weighted...
    block.add(struct.pack(">H", 0), 2, 0x10)       # ...and is joint 0
    return block.finish()


def jnt1(lo, hi, radius) -> bytes:
    block = Block(b"JNT1", 0x18)
    block.put(0x08, "HH", 1, 0xFFFF)
    joint = struct.pack(">HBB", 0, 0, 0xFF)                    # kind, scale compensate, padding
    joint += struct.pack(">fff", 1.0, 1.0, 1.0)                 # scale
    joint += struct.pack(">hhhH", 0, 0, 0, 0xFFFF)              # rotation, padding
    joint += struct.pack(">fff", 0.0, 0.0, 0.0)                 # translation
    joint += struct.pack(">f", radius) + struct.pack(">fff", *lo) + struct.pack(">fff", *hi)
    assert len(joint) == 0x40
    block.add(joint, 4, 0x0C)
    block.add(struct.pack(">H", 0), 2, 0x10)
    block.add(name_table(["ap_logo"]), 4, 0x14)
    return block.finish()


def shp1(triangles, lo, hi, radius) -> bytes:
    block = Block(b"SHP1", 0x2C)
    block.put(0x08, "HH", 1, 0xFFFF)
    shape = struct.pack(">BBHHHHH", 0, 0xFF, 1, 0, 0, 0, 0xFFFF)  # matrix type, groups, indices
    shape += struct.pack(">f", radius) + struct.pack(">fff", *lo) + struct.pack(">fff", *hi)
    assert len(shape) == 0x28
    block.add(shape, 4, 0x0C)
    block.add(struct.pack(">H", 0), 2, 0x10)                     # shape index table
    # 0x14: no shape name table
    descriptors = b"".join(struct.pack(">II", attr, kind) for attr, kind in [
        (GX_VA_POS, GX_INDEX16), (GX_VA_CLR0, GX_INDEX16), (GX_VA_NULL, 0)])
    block.add(descriptors, 4, 0x18)
    block.add(struct.pack(">H", 0), 2, 0x1C)                     # matrix table
    display_list = bytearray(struct.pack(">BH", GX_DRAW_TRIANGLES, 3 * len(triangles)))
    for triangle in triangles:
        for index in triangle:
            display_list += struct.pack(">HH", index, index)     # position, color
    align(display_list, 32)
    block.add(bytes(display_list), 32, 0x20)
    block.add(struct.pack(">HHI", 0, 1, 0), 4, 0x24)             # matrix group: draw matrix 0
    block.add(struct.pack(">II", len(display_list), 0), 4, 0x28)  # display list size, offset
    return block.finish()


def mat3() -> bytes:
    block = Block(b"MAT3", 0x84)
    block.put(0x08, "HH", 1, 0xFFFF)

    init = bytearray(b"\xff" * 0x14C)
    struct.pack_into(">BBBBBBBB", init, 0x00,
                     1,     # material mode: opaque
                     0,     # cull mode 0
                     0,     # color channel count 0
                     0,     # texgen count 0
                     0,     # TEV stage count 0
                     0,     # z compare location 0
                     0,     # z mode 0
                     0)     # dither 0
    struct.pack_into(">HH", init, 0x08, 0, 0)             # material colors
    struct.pack_into(">HHHH", init, 0x0C, 0, 1, 0, 1)     # COLOR0, ALPHA0, COLOR1, ALPHA1
    struct.pack_into(">HH", init, 0x14, 0, 0)             # ambient colors
    struct.pack_into(">H", init, 0x094, 0)                # konst color 0 (unused)
    struct.pack_into(">H", init, 0x0BC, 0)                # TEV order 0
    struct.pack_into(">H", init, 0x0DC, 0)                # TEV color 0 (unused)
    struct.pack_into(">H", init, 0x0E4, 0)                # TEV stage 0
    struct.pack_into(">H", init, 0x104, 0)                # TEV swap mode 0
    struct.pack_into(">HHHH", init, 0x124, 0, 0, 0, 0)    # swap tables: identity
    struct.pack_into(">HHHH", init, 0x144, 0, 0, 0, 0)    # fog, alpha compare, blend, NBT scale

    block.add(bytes(init), 4, 0x0C)
    block.add(struct.pack(">H", 0), 2, 0x10)                                    # material IDs
    block.add(name_table(["ap_logo_v"]), 4, 0x14)
    # 0x18: no indirect texturing
    block.add(struct.pack(">I", GX_CULL_NONE), 4, 0x1C)
    block.add(struct.pack(">BBBB", 255, 255, 255, 255), 4, 0x20)                # material color
    block.add(bytes([1]), 4, 0x24)                                              # 1 color channel
    # Lighting off, color and alpha from the vertices
    block.add(struct.pack(">BBBBBBBB", 0, GX_SRC_VTX, 0, GX_DF_CLAMP, GX_AF_NONE, GX_SRC_REG, 0xFF, 0xFF)
              + struct.pack(">BBBBBBBB", 0, GX_SRC_VTX, 0, GX_DF_CLAMP, GX_AF_NONE, GX_SRC_REG, 0xFF, 0xFF),
              4, 0x28)
    block.add(struct.pack(">BBBB", 50, 50, 50, 50), 4, 0x2C)                    # ambient color
    # 0x30: no lights
    block.add(bytes([0]), 4, 0x34)                                              # no texgens
    # 0x38..0x48: no texture coordinates, texture matrices or textures
    block.add(struct.pack(">BBBB", GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0, 0xFF), 4, 0x4C)
    block.add(struct.pack(">hhhh", 0, 0, 0, 0), 4, 0x50)                        # TEV color
    block.add(struct.pack(">BBBB", 255, 255, 255, 255), 4, 0x54)                # konst color
    block.add(bytes([1]), 4, 0x58)                                              # 1 TEV stage
    # Stage 0: color = rasterized color, alpha = rasterized alpha
    stage = bytes([0xFF,
                   GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC,
                   GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 1, GX_TEVPREV,
                   GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA,
                   GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 1, GX_TEVPREV,
                   0xFF])
    block.add(stage, 4, 0x5C)
    block.add(struct.pack(">BBBB", 0, 0, 0xFF, 0xFF), 4, 0x60)                  # swap mode
    block.add(struct.pack(">BBBB", 0, 1, 2, 3), 4, 0x64)                        # swap table
    fog = struct.pack(">BBHffff", 0, 0, 320, 0.0, 0.0, 0.0, 0.0) + bytes([255, 255, 255, 0]) + bytes(20)
    block.add(fog, 4, 0x68)                                                     # no fog
    block.add(struct.pack(">BBBBBBBB", GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0, 0xFF, 0xFF, 0xFF), 4, 0x6C)
    block.add(struct.pack(">BBBB", GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY), 4, 0x70)
    block.add(struct.pack(">BBBB", 1, GX_LEQUAL, 1, 0xFF), 4, 0x74)             # depth test + write
    block.add(bytes([1]), 4, 0x78)                                              # z compare before texturing
    block.add(bytes([1]), 4, 0x7C)                                              # dither
    block.add(struct.pack(">BBBBfff", 0, 0xFF, 0xFF, 0xFF, 1.0, 1.0, 1.0), 4, 0x80)  # NBT scale
    return block.finish()


def tex1() -> bytes:
    block = Block(b"TEX1", 0x14)
    block.put(0x08, "HH", 0, 0xFFFF)  # no textures
    return block.finish()


def build_bmd() -> bytes:
    positions, colors, triangles = build_mesh()
    lo, hi, radius = bounds(positions)
    blocks = [inf1(len(positions)), vtx1(positions, colors), evp1(), drw1(), jnt1(lo, hi, radius),
              shp1(triangles, lo, hi, radius), mat3(), tex1()]
    body = b"".join(blocks)
    header = b"J3D2bmd3" + struct.pack(">II", 0x20 + len(body), len(blocks)) + b"SVR3" + b"\xff" * 12
    return header + body


# ---------------------------------------------------------------------------------------------
# Archive (RARC)

def build_arc(files: dict[str, bytes], directory: str, directory_type: bytes, root_name: str) -> bytes:
    """A RARC with one subdirectory holding the files: node 0 ROOT, node 1 the subdirectory.
    Entries, as the game indexes them: root [dir, ., ..], subdirectory [files..., ., ..]."""
    strings = bytearray(b".\0..\0")

    def string(name: str) -> int:
        offset = len(strings)
        strings.extend(name.encode("ascii") + b"\0")
        return offset

    root_name_offset = string(root_name)
    dir_name_offset = string(directory)

    data = bytearray()
    entries = []
    # root: the subdirectory, ".", ".."
    entries.append((0xFFFF, name_hash(directory), 0x02 << 24 | dir_name_offset, 1, 0x10))
    entries.append((0xFFFF, name_hash("."), 0x02 << 24 | 0, 0, 0x10))
    entries.append((0xFFFF, name_hash(".."), 0x02 << 24 | 2, 0xFFFFFFFF, 0x10))
    first_sub = len(entries)
    for name, content in files.items():
        align(data, 32)
        entries.append((len(entries), name_hash(name), 0x11 << 24 | string(name), len(data), len(content)))
        data += content
    entries.append((0xFFFF, name_hash("."), 0x02 << 24 | 0, 1, 0x10))
    entries.append((0xFFFF, name_hash(".."), 0x02 << 24 | 2, 0, 0x10))
    align(data, 32)

    nodes = struct.pack(">4sIHHI", b"ROOT", root_name_offset, name_hash(root_name), 3, 0)
    nodes += struct.pack(">4sIHHI", directory_type, dir_name_offset, name_hash(directory),
                         len(entries) - first_sub, first_sub)

    info = bytearray(0x20)
    tables = bytearray(nodes)
    align(tables, 32)
    entry_offset = 0x20 + len(tables)
    tables += b"".join(struct.pack(">HHIIII", *entry, 0) for entry in entries)
    align(tables, 32)
    string_offset = 0x20 + len(tables)
    align(strings, 32)
    tables += strings
    struct.pack_into(">IIIIIIHB", info, 0, 2, 0x20, len(entries), entry_offset, len(strings), string_offset,
                     len(entries), 1)
    block = bytes(info) + bytes(tables)  # starts at file offset 0x20

    header = struct.pack(">4sIIIIIII", b"RARC", 0x20 + len(block) + len(data), 0x20, len(block), len(data),
                         len(data), 0, 0)
    return header + block + bytes(data)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("-o", "--output", type=Path,
                        default=Path(__file__).resolve().parents[2] / "overlay" / "res" / "Object" / f"{ARC_NAME}.arc")
    args = parser.parse_args()
    bmd = build_bmd()
    arc = build_arc({BMD_NAME: bmd}, "bmdr", b"BMDR", ARC_NAME.lower())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(arc)
    print(f"{args.output}: {len(arc)} bytes (model {len(bmd)} bytes)")


if __name__ == "__main__":
    main()
