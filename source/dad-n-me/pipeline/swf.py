"""Read and rasterization-wrap the SWF 6 display lists used by the supplied SWF6 game."""

from dataclasses import dataclass, replace
from pathlib import Path
from struct import pack, unpack_from
import zlib


class Bits:
    def __init__(self, data, start=0):
        self.data = data
        self.bit = start * 8

    def unsigned(self, count):
        result = 0
        for _ in range(count):
            result = (result << 1) | (self.data[self.bit // 8] >> (7 - self.bit % 8) & 1)
            self.bit += 1
        return result

    def signed(self, count):
        result = self.unsigned(count)
        return result - (1 << count) if count and result >= 1 << (count - 1) else result

    @property
    def end(self):
        return (self.bit + 7) // 8


def text_at(data, offset):
    end = data.index(0, offset)
    return data[offset:end].decode("utf-8"), end + 1


def rectangle(data, offset=0):
    reader = Bits(data, offset)
    width = reader.unsigned(5)
    bounds = tuple(reader.signed(width) / 20 for _ in range(4))
    return bounds, reader.end


IDENTITY = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)
COLOR_IDENTITY = (256, 256, 256, 256, 0, 0, 0, 0)


def matrix(data, offset):
    reader = Bits(data, offset)
    a, d = 1.0, 1.0
    b, c = 0.0, 0.0
    if reader.unsigned(1):
        width = reader.unsigned(5)
        a, d = (reader.signed(width) / 65536 for _ in range(2))
    if reader.unsigned(1):
        width = reader.unsigned(5)
        b, c = (reader.signed(width) / 65536 for _ in range(2))
    width = reader.unsigned(5)
    x, y = (reader.signed(width) / 20 for _ in range(2))
    return (a, b, c, d, x, y), reader.end


def color_transform(data, offset):
    reader = Bits(data, offset)
    has_add = reader.unsigned(1)
    has_multiplier = reader.unsigned(1)
    width = reader.unsigned(4)
    multipliers = tuple(reader.signed(width) for _ in range(4)) if has_multiplier else COLOR_IDENTITY[:4]
    offsets = tuple(reader.signed(width) for _ in range(4)) if has_add else COLOR_IDENTITY[4:]
    return multipliers + offsets, reader.end


def tags(data, offset=0):
    while offset + 2 <= len(data):
        (header,) = unpack_from("<H", data, offset)
        offset += 2
        code, size = header >> 6, header & 63
        if size == 63:
            (size,) = unpack_from("<I", data, offset)
            offset += 4
        if offset + size > len(data):
            raise ValueError(f"SWF tag {code} exceeds the file")
        yield code, data[offset:offset + size]
        offset += size
        if code == 0:
            return
    raise ValueError("Missing SWF end tag")


def tag(code, body=b""):
    size = len(body)
    if size < 63:
        return pack("<H", (code << 6) | size) + body
    return pack("<HI", (code << 6) | 63, size) + body


@dataclass(frozen=True)
class Placement:
    depth: int
    character: int = 0
    transform: tuple = IDENTITY
    color: tuple = COLOR_IDENTITY
    name: str = ""
    born: int = 0
    mask_depth: int = 0
    ratio: int = 0


def read_placement(data, frame, previous):
    flags, depth = unpack_from("<BH", data)
    offset = 3
    placement = previous[depth] if flags & 1 else Placement(depth, born=frame)
    if flags & 2:
        (character,) = unpack_from("<H", data, offset)
        offset += 2
        placement = replace(placement, character=character, born=frame)
    if flags & 4:
        transform, offset = matrix(data, offset)
        placement = replace(placement, transform=transform)
    if flags & 8:
        color, offset = color_transform(data, offset)
        placement = replace(placement, color=color)
    if flags & 16:
        placement = replace(placement, ratio=unpack_from("<H", data, offset)[0])
        offset += 2
    if flags & 32:
        name, offset = text_at(data, offset)
        placement = replace(placement, name=name)
    if flags & 64:
        (mask_depth,) = unpack_from("<H", data, offset)
        placement = replace(placement, mask_depth=mask_depth)
    return placement


def timeline(records):
    display_list = {}
    frames = []
    labels = {}
    for code, body in records:
        if code == 1:
            frames.append(tuple(display_list[depth] for depth in sorted(display_list)))
        elif code == 26:
            item = read_placement(body, len(frames), display_list)
            display_list[item.depth] = item
        elif code == 28:
            (depth,) = unpack_from("<H", body)
            display_list.pop(depth, None)
        elif code == 43:
            label, _ = text_at(body, 0)
            labels[label] = len(frames)
        elif code in (4, 5, 70, 94):
            raise ValueError(f"Unsupported placement tag {code}")
    return frames, labels


# Only resource definitions are copied into the extraction SWF. No root script runs.
DEFINITION_CODES = {2, 7, 10, 11, 13, 14, 20, 21, 22, 32, 33, 34, 35, 36, 37, 39, 46, 48, 62, 73, 75, 83, 84, 88, 90, 91}


class Movie:
    def __init__(self, path):
        raw = Path(path).read_bytes()
        if raw[:4] not in (b"CWS\x06", b"FWS\x06"):
            raise ValueError("Expected the SWF 6 source format")
        data = zlib.decompress(raw[8:]) if raw[:3] == b"CWS" else raw[8:]
        if len(data) + 8 != unpack_from("<I", raw, 4)[0]:
            raise ValueError("SWF decompressed size differs from its header")
        self.bounds, offset = rectangle(data)
        fps, frame_count = unpack_from("<HH", data, offset)
        self.fps = fps / 256
        self.records = list(tags(data, offset + 4))
        self.definitions = {
            unpack_from("<H", body)[0]: (code, body)
            for code, body in self.records
            if code in DEFINITION_CODES
        }
        self.frames, self.labels = timeline(self.records)
        if len(self.frames) != frame_count:
            raise ValueError("Invalid root frame count")

    def sprite(self, character):
        code, body = self.definitions[character]
        if code != 39:
            raise ValueError(f"{character} is not a sprite")
        frames, labels = timeline(tags(body, 4))
        if len(frames) != unpack_from("<H", body, 2)[0]:
            raise ValueError(f"Invalid frame count for sprite {character}")
        return frames, labels


def bit_fields(*fields):
    value, bits = 0, 0
    for width, field in fields:
        value = value << width | (field & ((1 << width) - 1))
        bits += width
    padding = (-bits) % 8
    return (value << padding).to_bytes((bits + padding) // 8, "big")


def signed_width(values):
    return max((v.bit_length() if v >= 0 else (~v).bit_length()) + 1 for v in values)


def encode_rectangle(bounds):
    coords = [round(coordinate * 20) for coordinate in bounds]
    width = signed_width(coords)
    return bit_fields((5, width), *((width, value) for value in coords))


def encode_matrix(transform):
    a, b, c, d, x, y = transform
    fields = []
    for active, pair in [((a, d) != (1, 1), (a, d)), ((b, c) != (0, 0), (b, c))]:
        fields.append((1, int(active)))
        if active:
            values = [round(v * 65536) for v in pair]
            width = signed_width(values)
            fields.extend([(5, width), *((width, value) for value in values)])
    values = [round(x * 20), round(y * 20)]
    width = signed_width(values)
    fields.extend([(5, width), *((width, value) for value in values)])
    return bit_fields(*fields)


def place_tag(character, depth, transform):
    return tag(26, pack("<BHH", 6, depth, character) + encode_matrix(transform))


def raster_swf(movie, width, height, frames, added_definitions=()):
    body = encode_rectangle((0, width, 0, height)) + pack("<HH", round(movie.fps * 256), len(frames))
    body += b"".join(tag(code, record) for code, record in movie.definitions.values())
    body += b"".join(tag(code, record) for code, record in added_definitions)
    count = 0
    for placements in frames:
        body += b"".join(tag(28, pack("<H", depth)) for depth in range(1, count + 1))
        body += b"".join(
            place_tag(character, depth, transform)
            for depth, (character, transform) in enumerate(placements, start=1)
        )
        body += tag(1)
        count = len(placements)
    body += tag(0)
    return b"FWS\x06" + pack("<I", len(body) + 8) + body
