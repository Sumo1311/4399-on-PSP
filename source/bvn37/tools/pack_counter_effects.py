#!/usr/bin/env python3
"""Bake the four pinned bitmap-backed counter symbols into a shared world-effect bank."""
import argparse
from copy import deepcopy
from functools import lru_cache
import hashlib
import json
import math
from pathlib import Path
import struct
import xml.etree.ElementTree as ET
import zlib
from PIL import Image
from build_musou_xfl import XflProject, XflTimeline, IDENTITY, frame_index, end, matrix, rectangle, union
from pack_fighter import FighterTextures, EMPTY_BOX
from pack_actions import HEADER, CLIP, FRAME, TEXTURE, POSE, FINISH, compress_textures
from pack_stage import aligned

SYMBOLS = ('_AS链接/元件/tishenXG', '_AS链接/元件/tishenXG2', '_AS链接/元件/bsqxg', '_AS链接/声音/baoqiXG',
           '_AS链接/元件/XG_ghost_step')
COUNTS = (34, 17, 15, 14, 16)


class CounterTimeline(XflTimeline):
    def entries(self, frame):
        for layer, frames in reversed(self.layers):
            active = next((key for key in reversed(frames) if frame_index(key) <= frame < end(key)), None)
            if active is None:
                continue
            instances = active.findall('./{*}elements/*')
            if active.get('tweenType'):
                # The only authored tween in these eight symbols is this translation-only classic tween.
                assert self.xml.get('name') == 'tishenXG' and active.get('tweenType') == 'motion'
                assert frame_index(active) == 8 and end(active) == 19 and active.get('acceleration') == '100'
                following = next(key for key in frames if frame_index(key) == 19)
                targets = following.findall('./{*}elements/*')
                assert len(instances) == len(targets) == 1
                original, target = instances[0], targets[0]
                a, b = matrix(original), matrix(target)
                assert a[:4] == b[:4] and a[4] == b[4]
                instance = deepcopy(original)
                m = instance.find('./{*}matrix/{*}Matrix')
                t = (frame - 8) / 11
                ease = 2 * t - t * t
                m.set('ty', str(a[5] + (b[5] - a[5]) * ease))
                instances = [instance]
            for instance in instances:
                yield layer.get('name'), instance, frame - frame_index(active)


class Project(XflProject):
    def __init__(self, document):
        super().__init__(document)
        self.sources = {document}

    @lru_cache(maxsize=None)
    def symbol(self, name):
        path = self.root / 'LIBRARY' / (name + '.xml')
        self.sources.add(path)
        return CounterTimeline(ET.parse(path).find('./{*}timeline/{*}DOMTimeline'))

    @lru_cache(maxsize=None)
    def bitmap(self, name):
        self.sources.add(self.root / 'bin' / self.media[name].get('bitmapDataHRef'))
        return super().bitmap(name)

    def cache_pose(self, timeline, frame, directory, key):
        layers = list(self.images(timeline, frame, IDENTITY))
        if not layers:
            return None
        bounds = union(rectangle(transform, (0, 0, *image.size)) for transform, image in layers)
        x, y = bounds[:2]
        # EffectVO allocates BitmapData(mc.width,mc.height) and draws at -getBounds().x/y.
        size = max(1, int(bounds[2] - x)), max(1, int(bounds[3] - y))
        canvas = Image.new('RGBA', size)
        for (a, b, c, d, u, v), image in layers:
            determinant = a * d - b * c
            u -= x; v -= y
            inverse = (d / determinant, -c / determinant, (c * v - d * u) / determinant,
                       -b / determinant, a / determinant, (b * u - a * v) / determinant)
            canvas.alpha_composite(image.transform(size, Image.Transform.AFFINE, inverse, Image.Resampling.NEAREST))
        canvas.save(directory / 'poses' / (key + '.png'))
        return x, y, *size


def build(reference, destination, scratch):
    scratch.mkdir(parents=True, exist_ok=True)
    (scratch / 'poses').mkdir(exist_ok=True)
    project = Project(reference / 'flash-source/SRC_XFL/effect/DOMDocument.xml')
    textures = FighterTextures(scratch, compact=True)
    frames, clips = [], []
    for _ in range(9):
        clips.append((len(frames), 1, 0, 1))
        frames.append((*textures.blank(), *EMPTY_BOX, 0, 0, 0, 0))
    report = []
    for cue, (symbol, expected) in enumerate(zip(SYMBOLS, COUNTS)):
        timeline = project.symbol(symbol)
        assert timeline.count - 1 == expected
        assert not timeline.labels
        clips.append((len(frames), expected, FINISH, 1))
        poses = []
        for frame in range(expected):
            key = f'{cue}-{frame}'
            pose = project.cache_pose(timeline, frame, scratch, key)
            baked = textures.bake(key, pose, allow_tiles=True)
            frames.append((*baked, *EMPTY_BOX, 0, 0, 0, 0))
            poses.append(dict(key=key, source_pose=pose, packed_pose=baked))
        report.append(dict(cue=cue, symbol=symbol, cache_frames=expected, poses=poses))
    packed, tiles, entries, payload = compress_textures(textures, frames, lambda data: zlib.compress(data, 1))
    start = aligned(52 + 24 * 18 + len(clips) * CLIP.size + len(packed) * FRAME.size + len(entries) * TEXTURE.size + len(tiles) * POSE.size)
    data = bytearray(HEADER.pack(b'BVNFGT12', len(packed), 0, 0, start, len(clips), 0, 1., 1., 1.))
    data.extend(struct.pack('<2H2I', 0, 0, len(entries), len(tiles)))
    data.extend(struct.pack('<24H', *([FINISH] * 24)))
    data.extend(bytes(24 * 16))
    for fmt, rows in [(CLIP, clips), (FRAME, packed), (TEXTURE, entries), (POSE, tiles)]:
        data.extend(b''.join(fmt.pack(*row) for row in rows))
    data.extend(bytes(start - len(data))); data.extend(payload)
    destination.write_bytes(data)
    document = dict(scope='Pinned XFL counter and ghost-foot bitmap timelines; original Flash raster equivalence remains to be established',
                    frames=sum(COUNTS), clips=report, format='BVNFGT12', output_sha256=hashlib.sha256(data).hexdigest(),
                    sources={str(p.relative_to(reference)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(project.sources)},
                    frame_sampling='EffectVO excludes the final authored frame; EffectView display uses the native source timing',
                    classic_tween='translation-only, intensity 100, t from authored frame spacing, quadratic ease out')
    destination.with_suffix('.json').write_text(json.dumps(document, indent=2) + '\n')
    print(f'Packed {destination}: {sum(COUNTS)} original XFL reaction frames, {len(payload)} compressed texture bytes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('scratch', type=Path)
    args = parser.parse_args()
    build(args.reference, args.output, args.scratch)
