"""Extract terrain draw data from a local SMF/SMT map."""

import argparse
from io import BytesIO
import math
from pathlib import Path
import struct

from PIL import Image


def export(smf, destination, selected_square):
    data = smf.read_bytes()
    magic, version, _, width, height, square, texels, tile, low, high, height_ptr, _, tile_ptr = (
        struct.unpack_from("<16s7i2f3i", data)
    )
    if (magic, version, square, texels, tile) != (b"spring map file\0", 1, 8, 8, 32):
        raise ValueError("Unsupported SMF format")
    square_x, square_z = selected_square or (0, 0)
    squares_x, squares_z = (1, 1) if selected_square else (width // 128, height // 128)
    if not (0 <= square_x < width // 128 and 0 <= square_z < height // 128):
        raise ValueError("Terrain square is outside the map")

    samples = (width + 1) * (height + 1)
    heights = [low + raw * (high - low) / 65536
               for raw in struct.unpack_from(f"<{samples}H", data, height_ptr)]
    files, count = struct.unpack_from("<2I", data, tile_ptr)
    if files != 1:
        raise ValueError("This draw test expects one SMT file")
    name_end = data.index(0, tile_ptr + 12)
    name = data[tile_ptr + 12:name_end].decode("ascii")
    source = smf.with_name(Path(name).name).read_bytes()
    if struct.unpack_from("<16s4I", source) != (b"spring tilefile\0", 1, count, 32, 1):
        raise ValueError("Unsupported SMT format or tile count")
    index_ptr = name_end + 1

    # Pillow decodes the top mip of each DXT1 tile. The atlas stays on this device.
    header = b"DDS " + struct.pack("<7I11I8I5I", 124, 0x81007, 32, 32, 512, 0, 0,
        *([0] * 11), 32, 4, int.from_bytes(b"DXT1", "little"), 0, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0)
    shade = bytearray(samples)
    stride = width + 1
    for z in range(height + 1):
        for x in range(width + 1):
            dx = heights[z * stride + min(x + 1, width)] - heights[z * stride + max(x - 1, 0)]
            dz = heights[min(z + 1, height) * stride + x] - heights[max(z - 1, 0) * stride + x]
            sun = max(0, (0.35 * dx + 13.6 + 0.4 * dz) / math.sqrt(dx * dx + 256 + dz * dz))
            shade[z * stride + x] = int(255 * (0.5 + 0.5 * sun))

    with destination.open("wb") as output:
        texture_side = 1024 if selected_square else 512
        output.write(struct.pack("<4s7I", b"SMF2", width, height, square_x, square_z,
                                 squares_x, squares_z, texture_side))
        output.write(struct.pack(f"<{samples}f", *heights))
        output.write(shade)
        for z in range(square_z, square_z + squares_z):
            for x in range(square_x, square_x + squares_x):
                atlas = Image.new("RGBA", (1024, 1024))
                for row in range(32):
                    for col in range(32):
                        index = (z * 32 + row) * (width // 4) + x * 32 + col
                        tile_id, = struct.unpack_from("<I", data, index_ptr + index * 4)
                        if tile_id >= count:
                            raise ValueError("Tile index is outside the SMT file")
                        start = 32 + tile_id * 680
                        with Image.open(BytesIO(header + source[start:start + 512])) as image:
                            atlas.paste(image, (col * 32, row * 32))
                if texture_side != 1024:
                    atlas = atlas.resize((texture_side, texture_side), Image.Resampling.BOX)
                output.write(atlas.tobytes())
    print(f"Exported {squares_x} × {squares_z} terrain squares with {texture_side}px textures")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("smf", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--square", nargs=2, type=int)
    args = parser.parse_args()
    export(args.smf, args.output, args.square)
