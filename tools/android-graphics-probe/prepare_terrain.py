"""Build a terrain probe mesh from a generated or existing SMF height field."""

import argparse
from io import BytesIO
import math
from pathlib import Path
import struct

from PIL import Image


GRID = 32


def read_smf(path):
    data = path.read_bytes()
    magic, version, _, width, height, square, _, _, low, high, heights, _, _, minimap = (
        struct.unpack_from("<16s7i2f4i", data)
    )
    if magic != b"spring map file\0" or version != 1 or square != 8:
        raise ValueError("Unsupported SMF header")
    crop = 64
    x0, y0 = (width - crop) // 2, (height - crop) // 2
    if min(x0, y0) < 0 or heights < 80 or heights + 2 * (width + 1) * (height + 1) > len(data):
        raise ValueError("Invalid SMF height field")

    def sample(x, y):
        offset = heights + 2 * ((y0 + y * 2) * (width + 1) + x0 + x * 2)
        value, = struct.unpack_from("<H", data, offset)
        return low + value * (high - low) / 65536

    # The SMF minimap contains a DXT1 image. Wrap its first mip in a DDS header.
    block_size = 1024 * 1024 // 2
    payload = data[minimap : minimap + block_size]
    if minimap < 80 or len(payload) != block_size:
        raise ValueError("Invalid SMF minimap")
    header = struct.pack("<7I11I8I5I", 124, 0x81007, 1024, 1024, block_size, 0, 0,
                         *([0] * 11), 32, 4, int.from_bytes(b"DXT1", "little"),
                         0, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0)
    with Image.open(BytesIO(b"DDS " + header + payload)) as image:
        region = (1024 * x0 / width, 1024 * y0 / height,
                  1024 * (x0 + crop) / width, 1024 * (y0 + crop) / height)
        texture = image.convert("RGBA").resize((1024, 1024), Image.Resampling.BILINEAR, box=region)
    return sample, texture, 2 / (crop * square)


def original_terrain():
    def sample(x, y):
        x, y = x / GRID * 2 - 1, y / GRID * 2 - 1
        edge = 1 - math.exp(-6 * (x * x + y * y))
        return edge * (0.13 * math.sin(3.5 * x + y) + 0.09 * math.cos(5 * y - x))

    texture = Image.new("RGBA", (1024, 1024))
    pixels = bytearray()
    for y in range(1024):
        for x in range(1024):
            wave = math.sin(x * 0.019 + 2 * math.sin(y * 0.013)) * math.cos(y * 0.016)
            grain = ((x * 92821 ^ y * 68917) & 31) - 15
            pixels.extend((int(119 + wave * 27 + grain), int(102 + wave * 27 + grain),
                           int(72 + wave * 19 + grain), 255))
    return sample, Image.frombytes("RGBA", texture.size, bytes(pixels)), 1


def write_patch(output, sample, texture, height_scale):
    center = sample(GRID // 2, GRID // 2)
    heights = [[(sample(x, y) - center) * height_scale for x in range(GRID + 1)]
               for y in range(GRID + 1)]
    vertices = []
    for y in range(GRID + 1):
        for x in range(GRID + 1):
            u, v = x / GRID, y / GRID
            dx = heights[y][min(x + 1, GRID)] - heights[y][max(x - 1, 0)]
            dz = heights[min(y + 1, GRID)][x] - heights[max(y - 1, 0)][x]
            normal = (-dx, 4 / GRID, -dz)
            length = math.sqrt(sum(n * n for n in normal))
            vertices.append((2 * u - 1, heights[y][x], 2 * v - 1, u, v,
                             *(n / length for n in normal)))
    with (output / "terrain-mesh.bin").open("wb") as file:
        for y in range(GRID):
            for x in range(GRID):
                a = y * (GRID + 1) + x
                b = a + GRID + 1
                for index in (a, b, a + 1, a + 1, b, b + 1):
                    file.write(struct.pack("<8f", *vertices[index]))
    (output / "terrain-color.rgba").write_bytes(texture.tobytes())
    print(f"{2 * GRID * GRID} triangles, elevation {min(map(min, heights)):.3f} to {max(map(max, heights)):.3f}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--smf", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    write_patch(args.output, *(read_smf(args.smf) if args.smf else original_terrain()))


if __name__ == "__main__":
    main()
