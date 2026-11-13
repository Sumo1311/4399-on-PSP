#!/usr/bin/env python3
"""Bake the bitmap-backed Musou enemy timelines from the project's XFL."""
import argparse
from dataclasses import dataclass
from functools import cached_property, lru_cache
import json
import math
from pathlib import Path
import re
import struct
from xml.etree import ElementTree as ET
import zlib

from PIL import Image

from fighter_actions import hit_definitions, read_constants
from pack_actions import AI_RANGES, clip_plan, compile_pack, special_art
from pack_fighter import FighterTimeline

IDENTITY = (1., 0., 0., 1., 0., 0.)
NO_COLOR = (1., 1., 1., 1., 0., 0., 0., 0.)
COLLISION_LAYERS = {"被打面", "攻击面", "AI"}
DAT_HEADER = struct.Struct("<4H4i")


def tag(element):
    return element.tag.rsplit("}", 1)[-1]


def frame_index(frame):
    return int(frame.get("index", "0"))


def end(frame):
    return frame_index(frame) + int(frame.get("duration", "1"))


def concatenate(parent, child):
    a, b, c, d, x, y = parent
    e, f, g, h, u, v = child
    return a*e+c*f, b*e+d*f, a*g+c*h, b*g+d*h, a*u+c*v+x, b*u+d*v+y


def matrix(element):
    value = element.find("./{*}matrix/{*}Matrix")
    if value is None:
        return IDENTITY
    return tuple(float(value.get(k, str(default))) for k, default in zip(("a","b","c","d","tx","ty"), IDENTITY))


def rectangle(transform, box):
    a, b, c, d, x, y = transform
    left, top, right, bottom = box
    corners = [(a*u+c*v+x, b*u+d*v+y) for u in (left,right) for v in (top,bottom)]
    xs, ys = zip(*corners)
    return min(xs), min(ys), max(xs), max(ys)


def union(boxes):
    left, top, right, bottom = zip(*boxes)
    return min(left), min(top), max(right), max(bottom)


def inflate_chunks(data, cursor, path):
    chunks = []
    while size := struct.unpack_from("<H", data, cursor)[0]:
        cursor += 2
        chunks.append(data[cursor:cursor+size])
        cursor += size
    if cursor + 2 != len(data):
        raise ValueError(f"Unexpected bytes after bitmap {path}")
    return zlib.decompress(b"".join(chunks))


def rgba_from_cache(raw, size, stride=0):
    argb = Image.frombytes("RGBA", size, raw, "raw", "ARGB", stride)
    return Image.frombytes("RGBa", size, argb.tobytes()).convert("RGBA")


def dat_bitmap(path):
    data = path.read_bytes()
    magic, stride, width, height, left, right, top, bottom = DAT_HEADER.unpack_from(data)
    if left or top or right != width * 20 or bottom != height * 20 or data[24] != 1:
        raise ValueError(f"Unsupported bitmap dimensions {path}")
    palette = None
    if magic == 0x503 and stride == width * 4:
        flags = 25
    elif magic == 0x303 and stride >= width:
        colors, = struct.unpack_from("<H", data, 25)
        if not 1 <= colors <= 256:
            raise ValueError(f"Unsupported palette size {path}")
        flags = 27 + 4*colors
        palette = rgba_from_cache(data[27:flags], (colors,1))
    else:
        raise ValueError(f"Unsupported bitmap cache {path}")
    if data[flags] != 1:
        raise ValueError(f"Uncompressed bitmap cache {path}")
    raw = inflate_chunks(data, flags+1, path)
    if len(raw) != stride * height:
        raise ValueError(f"Bad decoded size in {path}")
    if palette is None:
        return rgba_from_cache(raw, (width,height), stride)
    indices = Image.frombytes("P", (width,height), raw, "raw", "P", stride)
    indices.putpalette(palette.tobytes(), "RGBA")
    return indices.convert("RGBA")


def color_transform(parent, instance):
    color = instance.find("./{*}color/{*}Color")
    if color is None:
        return parent
    channels = ("red", "green", "blue", "alpha")
    multipliers = tuple(k + "Multiplier" for k in channels)
    offsets = tuple(k + "Offset" for k in channels)
    keys = set(color.attrib)
    if keys == {"brightness"}:
        amount = float(color.get("brightness"))
        if not -1 <= amount <= 1:
            raise ValueError(f"Invalid XFL brightness {amount}")
        scale = (1 - abs(amount),) * 3 + (1,)
        bias = (255 * max(amount, 0),) * 3 + (0,)
    elif keys == {"tintColor", "tintMultiplier"}:
        amount = float(color.get("tintMultiplier"))
        if not 0 <= amount <= 1:
            raise ValueError(f"Invalid XFL tint multiplier {amount}")
        rgb = int(color.get("tintColor").removeprefix("#"), 16)
        scale = (1 - amount,) * 3 + (1,)
        bias = tuple(((rgb >> bits) & 255) * amount for bits in (16, 8, 0)) + (0,)
    else:
        if keys - set(multipliers + offsets):
            raise ValueError(f"Unsupported XFL color transform {color.attrib}")
        scale = tuple(float(color.get(key, "1")) for key in multipliers)
        bias = tuple(float(color.get(key, "0")) for key in offsets)
    return tuple(parent[i] * scale[i] for i in range(4)) + tuple(
        parent[i] * bias[i] + parent[i+4] for i in range(4))


@dataclass
class XflTimeline:
    xml: ET.Element

    @cached_property
    def layers(self):
        return [(layer, layer.findall("./{*}frames/{*}DOMFrame"))
                for layer in self.xml.findall("./{*}layers/{*}DOMLayer")
                if layer.get("layerType") not in ("guide","folder")]

    @cached_property
    def count(self):
        return max(end(frame) for _, frames in self.layers for frame in frames)

    @cached_property
    def labels(self):
        return {frame.get("name"): frame_index(frame)+1 for _, frames in self.layers for frame in frames
                if frame.get("name")}

    @cached_property
    def scripts(self):
        result = {}
        for _, frames in self.layers:
            for frame in frames:
                bodies = frame.findall("./{*}Actionscript/{*}script")
                if bodies:
                    source = re.sub(r"//[^\n]*", "", "\n".join(x.text or "" for x in bodies))
                    index = frame_index(frame)+1
                    result[index] = result.get(index, "") + "\n" + source
        return result

    def entries(self, frame):
        # XFL layers list the foremost layer first, so composite from the rear.
        for layer, frames in reversed(self.layers):
            active = next((key for key in reversed(frames) if frame_index(key) <= frame < end(key)), None)
            if active is None:
                continue
            if active.get("tweenType"):
                raise ValueError(f"Tween on {self.xml.get('name')} frame {frame+1}")
            for instance in active.findall("./{*}elements/*"):
                yield layer.get("name"), instance, frame - frame_index(active)


class XflProject:
    def __init__(self, document):
        self.root = document.parent
        self.document = ET.parse(document).getroot()
        self.media = {item.get("name"): item for item in self.document.findall("./{*}media/{*}DOMBitmapItem")}

    @lru_cache(maxsize=None)
    def symbol(self, name):
        tree = ET.parse(self.root / "LIBRARY" / (name + ".xml")).getroot()
        return XflTimeline(tree.find("./{*}timeline/{*}DOMTimeline"))

    @lru_cache(maxsize=None)
    def bitmap(self, name):
        item = self.media[name]
        return dat_bitmap(self.root / "bin" / item.get("bitmapDataHRef"))

    @lru_cache(maxsize=None)
    def colored_bitmap(self, name, color):
        image = self.bitmap(name)
        if color == NO_COLOR:
            return image
        lut = [max(0,min(255,round(value * color[i] + color[i+4]))) for i in range(4) for value in range(256)]
        return image.point(lut)

    def child_frame(self, instance, child, elapsed, held):
        first = int(instance.get("firstFrame", "0"))
        if instance.get("symbolType") == "graphic":
            loop = instance.get("loop","loop")
            if loop == "single frame":
                return first
            if loop == "play once":
                return min(first + elapsed, child.count - 1)
            return (first + elapsed) % child.count
        frame = (first + (held if held is not None else elapsed)) % child.count
        if "stop" in child.labels:
            return min(frame, child.labels["stop"]-1)
        return frame

    def images(self, timeline, frame, placement, held=None, color=NO_COLOR):
        for layer, instance, elapsed in timeline.entries(frame):
            if layer in COLLISION_LAYERS:
                continue
            transform = concatenate(placement, matrix(instance))
            name = instance.get("libraryItemName")
            if instance.get("blendMode") not in (None,"normal"):
                raise ValueError(f"Unsupported blend effect on {name}")
            transformed_color = color_transform(color, instance)
            if tag(instance) == "DOMBitmapInstance":
                yield transform, self.colored_bitmap(name, transformed_color)
            elif tag(instance) == "DOMSymbolInstance":
                child = self.symbol(name)
                yield from self.images(child, self.child_frame(instance, child, elapsed, held), transform,
                                       color=transformed_color)
            else:
                raise ValueError(f"Unsupported visible {tag(instance)} in {timeline.xml.get('name')}")

    def bounds(self, instance, placement):
        name = instance.get("libraryItemName")
        transform = concatenate(placement, matrix(instance))
        if tag(instance) == "DOMShape":
            boxes = []
            for edge in instance.findall("./{*}edges/{*}Edge"):
                encoded = edge.get("edges", "")
                points = re.findall(r"[!|]\s*(-?\d+)\s+(-?\d+)", encoded)
                if not points or re.sub(r"[!|]\s*-?\d+\s+-?\d+", "", encoded).strip():
                    raise ValueError(f"Unsupported collision curve: {encoded}")
                xs, ys = zip(*[(int(x)/20,int(y)/20) for x,y in points])
                boxes.append(rectangle(transform, (min(xs),min(ys),max(xs),max(ys))))
        elif tag(instance) == "DOMSymbolInstance":
            boxes = [self.bounds(child,transform) for _,child,_ in self.symbol(name).entries(0)]
        else:
            raise ValueError(f"Unsupported collision element {tag(instance)}")
        return union(boxes)

    def pose(self, timeline, frame, placement, destination, key, held=None):
        images = list(self.images(timeline, frame, placement, held))
        if not images:
            return None
        bounds = union(rectangle(t, (0,0,*im.size)) for t, im in images)
        x, y = (math.floor(v) for v in bounds[:2])
        size = math.ceil(bounds[2])-x, math.ceil(bounds[3])-y
        out = Image.new("RGBA", size)
        for (a,b,c,d,u,v), image in images:
            determinant = a*d-b*c
            u, v = u-x, v-y
            inverse = (d/determinant, -c/determinant, (c*v-d*u)/determinant,
                       -b/determinant, a/determinant, (b*u-a*v)/determinant)
            out.alpha_composite(image.transform(size, Image.Transform.AFFINE, inverse, Image.Resampling.BILINEAR))
        out.save(destination / "poses" / f"{key}.png")
        return x, y, *size

    def cycle(self, timeline, frame):
        periods = [self.symbol(inst.get("libraryItemName")).count for layer,inst,_ in timeline.entries(frame)
                   if layer not in COLLISION_LAYERS and tag(inst) == "DOMSymbolInstance"
                   and inst.get("symbolType") != "graphic"]
        length = math.lcm(*periods)
        if length > 4096:
            raise ValueError(f"Unsupported held cycle length {length}")
        return length


def build(reference, name, destination, scratch):
    row = json.loads((reference / "source_catalog.json").read_text())["fighters"][name]
    if row["source"]["kind"] != "xfl" or not name.startswith("xb_"):
        raise ValueError(f"{name} is not an XFL minion")
    project = XflProject(reference / "flash-source" / row["source"]["path"])
    main = project.document.find("./{*}timelines/{*}DOMTimeline/{*}layers/{*}DOMLayer/{*}frames/{*}DOMFrame/{*}elements/{*}DOMSymbolInstance")
    if main is None:
        raise ValueError(f"No linkage guide in {name}")
    outer = project.symbol(main.get("libraryItemName"))
    variant = row["start_frame"]
    (_, actor, _), = [entry for entry in outer.entries(variant) if entry[1].get("name") == "mc"]
    action = project.symbol(actor.get("libraryItemName"))
    placement = matrix(actor)
    initializer = outer.scripts[variant+1]
    # XFL preserves literal AS3 property names. Match the JSON form emitted by FFDec.
    literal = re.sub(r'([{,]\s*)([A-Za-z_]\w*)(\s*:)', r'\1"\2"\3', initializer)
    const = reference / "upstream/CORE_Shared/src/net/play5d/game/bvn/data"
    hits = hit_definitions(literal, read_constants([const/"HitType.as", const/"fighter/FighterHurtType.as"]))
    if not hits:
        raise ValueError(f"{name} has no hit definitions")
    numbers = [re.search(rf"\$fighter_ctrler\.{field}\s*=\s*(\d+(?:\.\d+)?)\s*;", initializer)
               for field in ("speed","jumpPower","heavy","hp")]
    if any(value is None for value in numbers):
        raise ValueError(f"Missing authored stats on {name}")
    speed, jump, weight, hp = (float(value[1]) for value in numbers)

    scratch.mkdir(parents=True, exist_ok=True)
    (scratch / "poses").mkdir(exist_ok=True)
    (scratch / "hits.json").write_text(json.dumps(hits, ensure_ascii=False, indent=2)+"\n")
    timeline = FighterTimeline(variant=variant, frame_count=action.count, labels=action.labels)
    first_attack = "砍1" if "砍1" in action.labels else "招1"
    clips = clip_plan(timeline, action.scripts, [], full=True, first_attack=first_attack)
    faces, probes = special_art(clips, action.scripts, initializer)
    if faces:
        raise ValueError(f"{name} needs unsupported portrait assets")
    selected = sorted({number for clip in clips for number in range(clip.start, clip.end)})
    for number in selected:
        index = number-1
        timeline.poses[number] = project.pose(action,index,placement,scratch,number)
        display = [rectangle(transform, (0,0,*image.size))
                   for transform,image in project.images(action,index,placement)]
        for layer, instance, _ in action.entries(index):
            instance_name = instance.get("name", "")
            if layer in COLLISION_LAYERS and layer != "AI":
                box = project.bounds(instance, placement)
                display.append(box)
                if instance_name == "bdmn":
                    timeline.bodies[number] = box
                hit = instance_name.replace("atm", "", 1)
                if hit in hits:
                    timeline.hits.setdefault(number, []).append((hit, box))
            if instance_name in probes:
                timeline.probes.setdefault(number, []).append((instance_name, project.bounds(instance, placement)))
        if display:
            timeline.displays[number] = union(display)
    for clip in clips:
        if not clip.held:
            continue
        period = project.cycle(action, clip.start-1)
        if period > 1:
            timeline.cycles[clip.start] = period, 0
            for phase in range(period):
                key = f"{clip.start}-{phase}"
                timeline.children[clip.start, phase] = project.pose(action,clip.start-1,placement,scratch,key,phase+1)
    ranges = []
    for _, instance, _ in action.entries(0):
        if instance.get("name") != "AImain":
            continue
        ai_placement = concatenate(placement, matrix(instance))
        ai_timeline = project.symbol(instance.get("libraryItemName"))
        for _, node, _ in ai_timeline.entries(1):
            label = node.get("name")
            if label in AI_RANGES:
                bounds = project.bounds(node, ai_placement)
                ranges.append("\t".join(("range","2",label,*map(str,bounds)))+"\n")
    (scratch / "fighter.tsv").write_text("".join(ranges), encoding="utf-8")
    compile_pack(scratch, timeline, action.scripts, clips, (speed,jump,weight), destination, probes=probes)
    (scratch / "source-actor.json").write_text(json.dumps({"fighter":name,"baseHp":hp,"labels":action.labels,
        "source":row["source"]["path"],"poses":len(timeline.poses),"hitFrames":len(timeline.hits)},ensure_ascii=False,indent=2)+"\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fighter")
    parser.add_argument("output", type=Path)
    parser.add_argument("scratch", type=Path)
    parser.add_argument("--reference", type=Path, default=Path("reference"))
    args = parser.parse_args()
    build(args.reference, args.fighter, args.output, args.scratch)
