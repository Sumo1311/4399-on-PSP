#!/usr/bin/env python3
"""Bake the source fighter's movement and pose textures into a PSP asset."""
import argparse
import json
import re
import struct
from dataclasses import dataclass, field
from pathlib import Path

from PIL import Image

from fighter_actions import frame_scripts
from pack_stage import SCALE, aligned, power_of_two

HEADER = struct.Struct("<8sII5f2H")
CLIP = struct.Struct("<4H")
POSE = struct.Struct("<ffI4H9fI")
CLIPS = ("站立", "走", "起跳", "跳", "跳中", "落", "落地", "砍1", "被打")
EMPTY_BOX = (0, 0, 0, 0)
MOVE = re.compile(r"\.movePercent\(\s*(-?\d+(?:\.\d+)?)\s*,\s*0\s*\)")


@dataclass
class FighterTimeline:
    variant: int = 0
    frame_count: int = 0
    labels: dict = field(default_factory=dict)
    poses: dict = field(default_factory=dict)
    children: dict = field(default_factory=dict)
    cycles: dict = field(default_factory=dict)
    bodies: dict = field(default_factory=dict)
    displays: dict = field(default_factory=dict)
    hits: dict = field(default_factory=dict)
    probes: dict = field(default_factory=dict)
    anchors: dict = field(default_factory=dict)
    faces: dict = field(default_factory=dict)
    flight_poses: dict = field(default_factory=dict)
    screen_poses: dict = field(default_factory=dict)
    bankai_poses: dict = field(default_factory=dict)
    water_poses: dict = field(default_factory=dict)
    twin_lion_poses: dict = field(default_factory=dict)
    facing_poses: dict = field(default_factory=dict)
    projectiles: dict = field(default_factory=dict)
    attackers: dict = field(default_factory=dict)
    missing_objects: set = field(default_factory=set)


@dataclass
class DetachedTimeline:
    count: int
    initial: int
    position: tuple
    bounds: tuple
    labels: dict = field(default_factory=dict)
    poses: dict = field(default_factory=dict)
    hits: dict = field(default_factory=dict)
    probes: dict = field(default_factory=dict)
    symbols: list = field(default_factory=list)
    anchors: dict = field(default_factory=dict)


def read_timeline(source):
    timeline = FighterTimeline()
    for row in (source / "fighter.tsv").read_text(encoding="utf-8").splitlines():
        kind, number, *fields = row.split("\t")
        number = int(number)
        if kind == "variant":
            timeline.variant = number
            timeline.frame_count = int(fields[1])
        elif kind == "label":
            timeline.labels[fields[0]] = number
        elif kind == "missingObject":
            timeline.missing_objects.add((fields[0], number, fields[1]))
        elif kind == "pose":
            timeline.poses[number] = fields
        elif kind == "blank":
            timeline.poses[number] = None
        elif kind == "subpose":
            timeline.children[number, int(fields[0])] = fields[1:]
        elif kind == "cycle":
            timeline.cycles[number] = tuple(map(int, fields))
        elif kind == "body":
            timeline.bodies[number] = tuple(map(float, fields[1:]))
        elif kind == "display":
            timeline.displays[number] = tuple(map(float, fields[1:]))
        elif kind == "hit":
            timeline.hits.setdefault(number, []).append((fields[0], tuple(map(float, fields[1:]))))
        elif kind == "probe":
            timeline.probes.setdefault(number, []).append((fields[0], tuple(map(float, fields[1:]))))
        elif kind == "anchor":
            timeline.anchors.setdefault(number, []).append((fields[0], tuple(map(float, fields[1:]))))
        elif kind == "face":
            timeline.faces[fields[0]] = fields[1:]
        elif kind == "flightPose":
            timeline.flight_poses[number] = fields
        elif kind == "screenPose":
            timeline.screen_poses[number] = fields
        elif kind == "bankaiPose":
            timeline.bankai_poses[number] = fields
        elif kind == "waterPose":
            timeline.water_poses[number, int(fields[0])] = fields[1:]
        elif kind == "twinLionPose":
            timeline.twin_lion_poses[number, int(fields[0])] = fields[1:]
        elif kind == "facingPose":
            timeline.facing_poses[number] = fields
        elif kind.startswith(("projectile", "attacker", "nestedProjectile")):
            if kind.startswith("nestedProjectile"):
                kind = "projectile" + kind.removeprefix("nestedProjectile")
                parent, phase, name, *values = fields
                key = number, parent, int(phase), name
            else:
                name, *values = fields
                key = number, name
            table = timeline.projectiles if kind.startswith("projectile") else timeline.attackers
            if kind in ("projectile", "attacker"):
                count, initial, *geometry = values
                table[key] = DetachedTimeline(int(count), int(initial),
                    tuple(map(float, geometry[:3])), tuple(map(float, geometry[3:])))
            elif kind.endswith("Symbol"):
                table[key].symbols.append(values[0])
            else:
                phase, *values = values
                phase = int(phase)
                sprite = table[key]
                if kind.endswith("Label"):
                    sprite.labels[values[0]] = phase
                elif kind.endswith("Hit"):
                    sprite.hits.setdefault(phase, []).append((values[0], tuple(map(float, values[1:]))))
                elif kind.endswith("Probe"):
                    sprite.probes.setdefault(phase, []).append((values[0], tuple(map(float, values[1:]))))
                elif kind.endswith("Anchor"):
                    sprite.anchors.setdefault(phase, {})[values[0]] = tuple(map(float, values[1:]))
                else:
                    sprite.poses[phase] = values if kind.endswith("Pose") else None
    return timeline


class FighterTextures:
    def __init__(self, source, compact=False):
        self.source = source
        self.compact = compact
        self.offsets = {}
        self.pixels = bytearray()
        self.blank_poses = 0
        self.tiles = []

    def store(self, image, premultiply=False):
        stride, rows = power_of_two(image.width), power_of_two(image.height)
        padded = Image.new("RGBA", (stride, rows))
        padded.paste(image)
        texture = (stride, rows, (padded.convert("RGBa") if premultiply else padded).tobytes())
        if texture not in self.offsets:
            self.offsets[texture] = len(self.pixels)
            self.pixels.extend(texture[2])
        return self.offsets[texture], image.width, image.height, stride, rows

    def blank(self):
        self.blank_poses += 1
        return (0, 0, *self.store(Image.new("RGBA", (1, 1))))

    def bake(self, key, pose, allow_tiles=False, premultiply=False):
        if pose is None:
            return self.blank()
        x, y, width, height = pose
        with Image.open(self.source / "poses" / f"{key}.png") as raw:
            if raw.size != (int(width), int(height)):
                raise ValueError(f"Unexpected pose dimensions for {key}")
            image = raw.convert("RGBA").resize(
                tuple(max(1, round(n * SCALE)) for n in raw.size), Image.Resampling.NEAREST
            )
        bounds = image.getbbox(alpha_only=True)
        if bounds is None:
            return self.blank()
        cropped = image.crop(bounds)
        x, y = float(x) * SCALE + bounds[0], float(y) * SCALE + bounds[1]
        if allow_tiles and self.compact and max(cropped.size) >= 128:
            return self.bake_compact(cropped, x, y, premultiply)
        if max(cropped.size) <= 512:
            return x, y, *self.store(cropped, premultiply)
        if not allow_tiles:
            raise ValueError(f"Pose {key} exceeds a GU texture")
        first = len(self.tiles)
        for top in range(0, cropped.height, 512):
            for left in range(0, cropped.width, 512):
                tile = cropped.crop((left, top, min(left + 512, cropped.width), min(top + 512, cropped.height)))
                if tile.getbbox(alpha_only=True):
                    self.tiles.append((x + left, y + top, *self.store(tile, premultiply)))
        return 0, 0, first, len(self.tiles) - first, 0, 0, 0

    def bake_compact(self, cropped, x, y, premultiply):
        def parts(xs, ys, trim=False):
            for top, bottom in zip(ys, ys[1:]):
                for left, right in zip(xs, xs[1:]):
                    image = cropped.crop((left, top, right, bottom))
                    visible = image.getbbox(alpha_only=True)
                    if visible:
                        if trim:
                            yield left + visible[0], top + visible[1], image.crop(visible)
                        else:
                            yield left, top, image

        def exact_edges(size):
            edges = [0]
            while size - edges[-1] >= 128:
                remaining = size - edges[-1]
                edges.append(edges[-1] + min(512, 1 << (remaining.bit_length() - 1)))
            if edges[-1] != size:
                edges.append(size)
            return edges

        def bytes_for(tiles):
            return sum(power_of_two(image.width) * power_of_two(image.height) * 4 for _, _, image in tiles)

        if max(cropped.size) <= 512:
            classic = [(0, 0, cropped)]
        else:
            classic = list(parts([*range(0, cropped.width, 512), cropped.width],
                                 [*range(0, cropped.height, 512), cropped.height]))
        candidate = list(parts(exact_edges(cropped.width), exact_edges(cropped.height), trim=True))
        tiles = candidate if len(candidate) <= len(classic) + 4 and bytes_for(candidate) * 5 <= bytes_for(classic) * 4 else classic
        if len(tiles) == 1:
            left, top, image = tiles[0]
            return x + left, y + top, *self.store(image, premultiply)
        first = len(self.tiles)
        self.tiles.extend((x + left, y + top, *self.store(image, premultiply)) for left, top, image in tiles)
        return 0, 0, first, len(self.tiles) - first, 0, 0, 0


def authored_stats(script, variant):
    init = dict(frame_scripts(script))[variant + 1]
    values = []
    for name in ("speed", "jumpPower", "heavy"):
        match = re.search(rf"\$fighter_ctrler\.{name}\s*=\s*(\d+(?:\.\d+)?)\s*;", init)
        if not match:
            raise ValueError(f"Missing literal {name} for start_frame {variant}")
        values.append(float(match[1]))
    return values


def pack(source, main_script, timeline_script, destination):
    timeline = read_timeline(source)
    labels, poses, children, cycles = timeline.labels, timeline.poses, timeline.children, timeline.cycles
    scripts = dict(frame_scripts(timeline_script))
    stats = authored_stats(main_script, timeline.variant)
    walk = labels["走"]
    land = labels["落地"]
    attack = labels["砍1"]
    hurt = labels["被打"]
    walk_end = min(frame for frame, body in scripts.items() if frame > walk and re.search(r'\.loop\("走"\)', body))
    land_end = min(frame for frame, body in scripts.items() if frame > land and re.search(r'\.idle\(\)', body))
    attack_end = min(frame for frame, body in scripts.items() if frame > attack and re.search(r'\.idle\(\)', body))
    ranges = {walk: walk_end, land: land_end, attack: attack_end, hurt: hurt + 8}
    for start, end in ranges.items():
        if not start < end <= min(frame for frame in labels.values() if frame > start):
            raise ValueError(f"Script starting at frame {start} runs outside its timeline span")

    strike_boxes, move_events = {}, {}
    strike_ids = set()
    for frame in range(attack, attack_end):
        authored = timeline.hits.get(frame, [])
        if authored:
            if len(authored) != 1:
                raise ValueError(f"Frame {frame} contains several strikes")
            hit_id, strike_boxes[frame] = authored[0]
            strike_ids.add(hit_id)
        script = scripts.get(frame, "")
        if move := MOVE.search(script):
            move_events[frame] = float(move[1]) * stats[0]
        elif ".stopMove()" in script:
            move_events[frame] = 0
        elif ".movePercent(" in script:
            raise ValueError(f"Unhandled movement at frame {frame}")

    if len(strike_ids) != 1:
        raise ValueError("The first normal attack must use a single hit definition")
    strike = json.loads((source / "hits.json").read_text(encoding="utf-8"))[strike_ids.pop()]
    if strike["hurtType"] != 0:
        raise ValueError("The first normal attack uses an unsupported hurt type")
    hurt_frames = max(4, (strike["hurtTime"] * 30 + 500) // 1000 + 3)

    clips, records = [], []
    textures = FighterTextures(source)

    def bake(key, frame, pose):
        records.append((*textures.bake(key, pose), *timeline.bodies.get(frame, EMPTY_BOX),
                        *strike_boxes.get(frame, EMPTY_BOX), move_events.get(frame, 0), int(frame in move_events)))

    for label in CLIPS:
        first = labels[label]
        start = len(records)
        repeat = 0
        if first in ranges:
            for frame in range(first, ranges[first]):
                bake(frame, frame, poses[frame])
            if first == hurt:
                repeat = ranges[first] - first - 1
        elif first in cycles:
            count, repeat = cycles[first]
            for step in range(count):
                bake(f"{first}-{step}", first, children[first, step])
        else:
            bake(first, first, poses[first])
        clips.append((start, len(records) - start, repeat, first))

    offset = aligned(HEADER.size + len(clips) * CLIP.size + len(records) * POSE.size)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("wb") as out:
        out.write(HEADER.pack(b"BVNFGT02", len(records), offset, *stats, strike["hitx"], strike["hity"],
                              strike["power"], hurt_frames))
        for clip in clips:
            out.write(CLIP.pack(*clip))
        for record in records:
            out.write(POSE.pack(*record))
        out.write(bytes(offset - out.tell()))
        out.write(textures.pixels)
    print(f"{destination}: {len(records)} poses, {len(textures.offsets)} textures, {len(textures.pixels)} texture bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("main_script", type=Path)
    parser.add_argument("timeline_script", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    pack(args.source, args.main_script, args.timeline_script, args.destination)
