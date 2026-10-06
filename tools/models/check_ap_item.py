#!/usr/bin/env python3
"""Reads back the Archipelago item archive (overlay/res/Object/O_gD_ap.arc) independently of the
script that wrote it: archive layout, model blocks and every index of the display list, and can
render a preview.

    python tools/models/check_ap_item.py [archive] [--preview out.png]

The preview needs numpy and Pillow. Exits non-zero on any problem.
"""

import argparse
import math
import struct
import sys
from pathlib import Path


class Problem(Exception):
    pass


def u16(data, at):
    return struct.unpack_from(">H", data, at)[0]


def u32(data, at):
    return struct.unpack_from(">I", data, at)[0]


def read_arc(arc: bytes) -> dict[str, tuple[bytes, bytes, int]]:
    """{file name: (directory type, data, entry index)}"""
    if arc[:4] != b"RARC" or u32(arc, 4) != len(arc):
        raise Problem("not a RARC archive, or its size field is wrong")
    info = 0x20
    data_start = info + u32(arc, 0x0C)
    if data_start + u32(arc, 0x10) != len(arc):
        raise Problem("file data section doesn't end the archive")
    nodes, node_off, entries, entry_off = (u32(arc, info + i) for i in (0, 4, 8, 12))
    strings = info + u32(arc, info + 0x14)
    if not arc[info + 0x1A]:
        raise Problem("file IDs are not synchronized with entry indices")

    def name(offset):
        end = arc.index(b"\0", strings + offset)
        return arc[strings + offset:end].decode("ascii")

    files = {}
    for n in range(nodes):
        node = info + node_off + 16 * n
        kind = arc[node:node + 4]
        count, first = u16(arc, node + 10), u32(arc, node + 12)
        if first + count > entries:
            raise Problem(f"node {n} lists entries past the table")
        names = []
        for i in range(first, first + count):
            entry = info + entry_off + 20 * i
            flags = arc[entry + 4]
            entry_name = name(u32(arc, entry + 4) & 0xFFFFFF)
            names.append(entry_name)
            if flags & 0x01:
                if u16(arc, entry) != i:
                    raise Problem(f"{entry_name}: file ID {u16(arc, entry)} is not its index {i}")
                start, size = data_start + u32(arc, entry + 8), u32(arc, entry + 12)
                if (start % 32) or start + size > len(arc):
                    raise Problem(f"{entry_name}: data out of the archive or unaligned")
                files[entry_name] = (kind, arc[start:start + size], i)
        if names[-2:] != [".", ".."]:
            raise Problem(f"node {n} doesn't end with . and ..")
    return files


def read_bmd(bmd: bytes):
    if bmd[:8] != b"J3D2bmd3" or u32(bmd, 8) != len(bmd):
        raise Problem("not a bmd3 model, or its size field is wrong")
    blocks = {}
    at = 0x20
    for _ in range(u32(bmd, 12)):
        kind, size = bmd[at:at + 4].decode("ascii"), u32(bmd, at + 4)
        if size == 0 or size % 32 or at + size > len(bmd):
            raise Problem(f"{kind}: bad block size {size}")
        blocks[kind] = bmd[at:at + size]
        at += size
    if at != len(bmd):
        raise Problem("blocks don't fill the file")
    order = list(blocks)
    if order[:1] != ["INF1"] or set(order) != {"INF1", "VTX1", "EVP1", "DRW1", "JNT1", "SHP1", "MAT3", "TEX1"}:
        raise Problem(f"unexpected blocks {order}")
    return blocks


def read_mesh(blocks):
    vtx = blocks["VTX1"]
    formats = {}
    at = u32(vtx, 0x08)
    while True:
        attr, count, comp, frac = struct.unpack_from(">IIIB", vtx, at)
        if attr == 0xFF:
            break
        formats[attr] = (count, comp, frac)
        at += 16
    if formats.get(9) != (1, 4, 0) or formats.get(11) != (1, 5, 0):
        raise Problem(f"expected F32 XYZ positions and RGBA8 colors, got {formats}")
    pos_off, clr_off = u32(vtx, 0x0C), u32(vtx, 0x18)
    vertex_count = u32(blocks["INF1"], 0x10)
    positions = [struct.unpack_from(">fff", vtx, pos_off + 12 * i) for i in range(vertex_count)]
    colors = [tuple(vtx[clr_off + 4 * i:clr_off + 4 * i + 4]) for i in range(vertex_count)]
    if pos_off + 12 * vertex_count > clr_off or clr_off + 4 * vertex_count > len(vtx):
        raise Problem("vertex arrays overlap or run past VTX1")

    shp = blocks["SHP1"]
    if u16(shp, 0x08) != 1:
        raise Problem("expected one shape")
    desc_off = u32(shp, 0x18)
    descriptors = []
    at = desc_off
    while u32(shp, at) != 0xFF:
        descriptors.append((u32(shp, at), u32(shp, at + 4)))
        at += 8
    if descriptors != [(9, 3), (11, 3)]:
        raise Problem(f"expected indexed position and color, got {descriptors}")
    dl_off, draw_off = u32(shp, 0x20), u32(shp, 0x28)
    dl_size, dl_index = u32(shp, draw_off), u32(shp, draw_off + 4)
    dl = shp[dl_off + dl_index:dl_off + dl_index + dl_size]
    if len(dl) != dl_size or dl_size % 32:
        raise Problem("display list out of SHP1 or unaligned")
    triangles = []
    at = 0
    while at < len(dl):
        opcode = dl[at]
        if opcode == 0x00:  # NOP padding
            at += 1
            continue
        if opcode != 0x90:
            raise Problem(f"unexpected display list opcode {opcode:#x} at {at}")
        count = u16(dl, at + 1)
        at += 3
        if count % 3:
            raise Problem("triangle list vertex count not a multiple of 3")
        corners = []
        for _ in range(count):
            p, c = u16(dl, at), u16(dl, at + 2)
            at += 4
            if p >= vertex_count or c >= vertex_count:
                raise Problem(f"index {p}/{c} out of range ({vertex_count} vertices)")
            corners.append((p, c))
        triangles += [corners[i:i + 3] for i in range(0, count, 3)]
    return positions, colors, triangles


def check_material(blocks):
    mat = blocks["MAT3"]
    if u16(mat, 0x08) != 1:
        raise Problem("expected one material")
    chan = u32(mat, 0x28)
    enable, mat_src = mat[chan], mat[chan + 1]
    if enable != 0 or mat_src != 1:
        raise Problem("expected unlit vertex colors")
    stage = mat[u32(mat, 0x5C):u32(mat, 0x5C) + 20]
    if stage[1:5] != bytes([15, 15, 15, 10]) or stage[10:14] != bytes([7, 7, 7, 5]):
        raise Problem("expected one TEV stage passing the vertex color through")


def render(positions, colors, triangles, path: Path, size=512):
    import numpy as np
    from PIL import Image

    # A three-quarter view, as when Link holds the item up (camera slightly below, in front)
    yaw, pitch = math.radians(-25), math.radians(8)
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    center = [sum(p[i] for p in positions) / len(positions) for i in range(3)]
    extent = max(math.dist(p, center) for p in positions)

    def project(p):
        x, y, z = (p[i] - center[i] for i in range(3))
        x, z = x * cy + z * sy, -x * sy + z * cy
        y, z = y * cp - z * sp, y * sp + z * cp
        distance = extent * 4.0
        scale = size * 0.42 * distance / (distance - z) / extent
        return size / 2 + x * scale, size / 2 - y * scale, z

    screen = [project(p) for p in positions]
    image = np.zeros((size, size, 3), dtype=np.float32)
    image[:] = (0.13, 0.12, 0.16)
    depth = np.full((size, size), -1e9, dtype=np.float32)
    ys, xs = np.mgrid[0:size, 0:size]
    for tri in triangles:
        (a, ca), (b, cb), (c, cc) = tri
        (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = screen[a], screen[b], screen[c]
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if abs(area) < 1e-9:
            continue
        lo_x, hi_x = int(max(0, min(x0, x1, x2))), int(min(size - 1, max(x0, x1, x2)) + 1)
        lo_y, hi_y = int(max(0, min(y0, y1, y2))), int(min(size - 1, max(y0, y1, y2)) + 1)
        if lo_x >= hi_x or lo_y >= hi_y:
            continue
        px, py = xs[lo_y:hi_y, lo_x:hi_x] + 0.5, ys[lo_y:hi_y, lo_x:hi_x] + 0.5
        w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area
        w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area
        w2 = 1 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        z = w0 * z0 + w1 * z1 + w2 * z2
        region = depth[lo_y:hi_y, lo_x:hi_x]
        visible = inside & (z > region)
        if not visible.any():
            continue
        region[visible] = z[visible]
        col = [np.array(colors[i][:3], dtype=np.float32) / 255 for i in (ca, cb, cc)]
        shade = w0[..., None] * col[0] + w1[..., None] * col[1] + w2[..., None] * col[2]
        image[lo_y:hi_y, lo_x:hi_x][visible] = shade[visible]
    Image.fromarray((np.clip(image, 0, 1) * 255).astype(np.uint8)).save(path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("archive", type=Path, nargs="?",
                        default=Path(__file__).resolve().parents[2] / "overlay" / "res" / "Object" / "O_gD_ap.arc")
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()
    try:
        files = read_arc(args.archive.read_bytes())
        if list(files) != ["O_gD_ap.bmd"]:
            raise Problem(f"expected only O_gD_ap.bmd, found {list(files)}")
        kind, bmd, index = files["O_gD_ap.bmd"]
        if kind != b"BMDR" or index != 3:
            raise Problem(f"the model must be resource 3 of a BMDR directory (src/item.cpp), not {index} in {kind}")
        blocks = read_bmd(bmd)
        check_material(blocks)
        positions, colors, triangles = read_mesh(blocks)
    except Problem as problem:
        print(f"FAIL {args.archive}: {problem}")
        return 1
    lo = [min(p[i] for p in positions) for i in range(3)]
    hi = [max(p[i] for p in positions) for i in range(3)]
    print(f"OK {args.archive}: {len(positions)} vertices, {len(triangles)} triangles, "
          f"size {hi[0] - lo[0]:.1f} x {hi[1] - lo[1]:.1f} x {hi[2] - lo[2]:.1f}, bottom at y={lo[1]:.1f}")
    if args.preview:
        render(positions, colors, triangles, args.preview)
        print(f"preview: {args.preview}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
