"""Prepare rest-pose meshes and textures for the Android probes."""

import argparse
from pathlib import Path
import math
import struct

from PIL import Image


def unpack_array(data, offset, count, record):
    if offset < 0 or count < 0 or offset + count * record.size > len(data):
        raise ValueError("S3O array is outside the file")
    return list(record.iter_unpack(data[offset : offset + count * record.size]))


def triangle_indices(indices, primitive):
    if primitive == 0 and len(indices) % 3 == 0:
        return indices
    if primitive == 2 and len(indices) % 4 == 0:
        return [index for a, b, c, d in zip(*[iter(indices)] * 4) for index in (a, b, c, a, c, d)]
    if primitive == 1:
        result = []
        start = 0
        for i, index in enumerate(indices):
            if index == 0xFFFFFFFF:
                start = i + 1
            elif i - start >= 2:
                a, b = indices[i - 2 : i]
                result.extend((b, a, index) if (i - start) % 2 else (a, b, index))
        return result
    raise ValueError("Invalid S3O primitive or index count")


def load_unit(path, world_scale=False):
    data = path.read_bytes()
    if data[:12] != b"Spring unit\0" or struct.unpack_from("<i", data, 12)[0] != 0:
        raise ValueError("Expected an S3O version 0 model")
    root, _, color, material = struct.unpack_from("<4i", data, 36)
    textures = [data[offset : data.index(0, offset)].decode("ascii") for offset in (color, material)]
    vertices = []
    visited = set()

    def visit(offset, parent):
        if offset in visited:
            raise ValueError("Repeated S3O piece")
        visited.add(offset)
        _, child_count, children, count, source, layout, primitive, size, indices, _, x, y, z = (
            struct.unpack_from("<10i3f", data, offset)
        )
        if layout != 0:
            raise ValueError("Unsupported S3O vertex layout")
        translation = (parent[0] + x, parent[1] + y, parent[2] + z)
        piece = unpack_array(data, source, count, struct.Struct("<8f")) if count else []
        index_list = [n for (n,) in unpack_array(data, indices, size, struct.Struct("<I"))] if size else []
        for index in triangle_indices(index_list, primitive):
            px, py, pz, nx, ny, nz, u, v = piece[index]
            length = math.sqrt(nx * nx + ny * ny + nz * nz)
            normal = (nx / length, ny / length, nz / length) if length else (0, 1, 0)
            vertices.append((px + translation[0], py + translation[1], pz + translation[2], u, v, *normal))
        for (child,) in unpack_array(data, children, child_count, struct.Struct("<I")) if child_count else []:
            visit(child, translation)

    visit(root, (0, 0, 0))
    center = [(min(v[axis] for v in vertices) + max(v[axis] for v in vertices)) / 2 for axis in range(3)]
    radius = max(math.dist(v[:3], center) for v in vertices)
    if world_scale:
        radius = 1
    floor = min(v[1] for v in vertices)
    mesh = bytearray()
    for x, y, z, u, v, nx, ny, nz in vertices:
        mesh.extend(struct.pack("<8f", (x - center[0]) / radius, (y - floor) / radius,
                                (z - center[2]) / radius, u, v, nx, ny, nz))
    return mesh, textures, len(visited)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bar", type=Path)
    parser.add_argument("output", type=Path)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--commander", action="store_true", help="Prepare local Armada commander data at game scale")
    selection.add_argument("--solar", action="store_true", help="Prepare local Armada solar collector data at game scale")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    model = "armsolar" if args.solar else "armcom" if args.commander else "legaabot"
    world_scale = args.commander or args.solar
    mesh, textures, pieces = load_unit(args.bar / f"objects3d/Units/{model}.s3o", world_scale=world_scale)
    (args.output / "unit-mesh.bin").write_bytes(mesh)
    for source, target in zip(textures, ("unit-color.rgba", "unit-material.rgba"), strict=True):
        with Image.open(args.bar / "unittextures" / source) as image:
            image = image.convert("RGBA").resize((1024, 1024), Image.Resampling.BOX)
            # Match Recoil's DDS upload orientation for unit textures.
            image = image.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
            (args.output / target).write_bytes(image.tobytes())

    if world_scale:
        print(f"{pieces} pieces, {len(mesh) // 96} triangles, textures: {', '.join(textures)}")
        return
    (args.output / "unit-credits.txt").write_text(
        "Beyond All Reason Legion Flak Bot by ZephyrSkies.\n"
        "Source: https://github.com/beyond-all-reason/Beyond-All-Reason\n"
        "objects3d/Units/legaabot.s3o, unittextures/leg_color.dds, unittextures/leg_shader.dds\n"
        "CC BY-SA 4.0: https://creativecommons.org/licenses/by-sa/4.0/\n"
        "Adapted for this probe: flattened rest-pose pieces, centered/scaled mesh, "
        "and decoded, resized, and flipped textures to 1024px RGBA.\n",
        encoding="utf-8",
    )
    print(f"{pieces} pieces, {len(mesh) // 96} triangles, textures: {', '.join(textures)}")


if __name__ == "__main__":
    main()
