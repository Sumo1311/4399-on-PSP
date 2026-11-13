#!/usr/bin/env python3
"""Pack extracted map geometry and GPU-ready tiles for the PSP stage runtime."""
import argparse
import struct
from pathlib import Path

from PIL import Image

HEADER = struct.Struct("<8s4I8d")
FLOOR = struct.Struct("<3d")
TILE = struct.Struct("<ffII6H")
LAYERS = {"bg": 0, "map": 1, "front": 2, "front_fix": 3}
SCALE = 272 / 600
TILE_SIZE = 128


def aligned(value, alignment=16):
    return (value + alignment - 1) & -alignment


def power_of_two(size):
    return max(4, 1 << (size - 1).bit_length())


def pack(source, destination):
    bounds, starts, floors, tiles, pixels = None, {}, [], [], bytearray()
    camera_width = None
    for row in (source / "stage.tsv").read_text().splitlines():
        kind, *fields = row.split("\t")
        if kind == "bounds":
            bounds = tuple(map(float, fields))
        elif kind in ("p1", "p2"):
            starts[kind] = tuple(map(float, fields))
        elif kind == "floor":
            floors.append(tuple(map(float, fields)))
        elif kind == "camera":
            camera_width = float(fields[0])
        elif kind == "layer":
            name, x, y, width, height, frame_count = fields
            if int(frame_count) < 1:
                raise ValueError(f"Invalid frame count for {name}")
            with Image.open(source / f"{name}.png") as raw:
                if raw.size != (int(width), int(height)):
                    raise ValueError(f"Unexpected layer dimensions for {name}")
                size = tuple(max(1, round(n * SCALE)) for n in raw.size)
                image = raw.convert("RGBA").resize(size, Image.Resampling.NEAREST)
            for top in range(0, image.height, TILE_SIZE):
                for left in range(0, image.width, TILE_SIZE):
                    tile = image.crop((left, top, min(left + TILE_SIZE, image.width),
                                       min(top + TILE_SIZE, image.height)))
                    if not tile.getbbox(alpha_only=True):
                        continue
                    stride, rows = power_of_two(tile.width), power_of_two(tile.height)
                    # Pad the texture for GU sizing and keep rows aligned to 16 bytes.
                    padded = Image.new("RGBA", (stride, rows))
                    padded.paste(tile)
                    packed = padded.tobytes()
                    tiles.append((float(x) * SCALE + left, float(y) * SCALE + top, len(pixels),
                                  len(packed), LAYERS[name], tile.width, tile.height, stride, rows, 0))
                    pixels.extend(packed)
        elif kind != "timeline":
            raise ValueError(f"Unknown stage field {kind!r}")

    if bounds is None or not tiles or not any(t[4] == LAYERS["map"] for t in tiles):
        raise ValueError("Missing map bounds or textures")
    flags = int("p1" in starts) | (int("p2" in starts) << 1)
    points = (*starts.get("p1", (0, 0)), *starts.get("p2", (0, 0)))
    camera = struct.pack("<d", camera_width) if camera_width is not None else b""
    offset = aligned(HEADER.size + len(camera) + FLOOR.size * len(floors) + TILE.size * len(tiles))
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("wb") as out:
        out.write(HEADER.pack(b"BVNSTG02" if camera else b"BVNSTG01", len(floors), len(tiles), offset, flags, *bounds, *points))
        out.write(camera)
        for floor in floors:
            out.write(FLOOR.pack(*floor))
        for tile in tiles:
            out.write(TILE.pack(*tile))
        out.write(bytes(offset - out.tell()))
        out.write(pixels)
    print(f"{destination}: {len(floors)} platforms, {len(tiles)} tiles, {len(pixels)} texture bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    pack(args.source, args.destination)
