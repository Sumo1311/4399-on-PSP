#!/usr/bin/env python3
"""Bind available Musou boss forms and append their original audio to the shared PCM bank."""
import argparse
import json
from pathlib import Path
import struct
import tempfile

from pack_versus_audio import CLIP_LIMIT, HEADER, RECORD, from_audio
from versus_roster import roster


def add_strike_flags(path, report):
    strikes = json.loads(report.read_text())["strikes"]
    data = bytearray(path.read_bytes())
    magic, frames, hits, _, pixel, clips, count, *_ = struct.unpack_from("<8s4I2H3f", data)
    if magic not in (b"BVNFGT11", b"BVNFGT12", b"BVNFGT13"):
        raise ValueError(f"Unexpected combat bank {path}")
    bindings = 22 if magic == b"BVNFGT11" else 24
    base = 52 + bindings * 18 + clips * 8 + frames * 48 + hits * 20
    if len(strikes) != count or base + count * 24 > pixel:
        raise ValueError(f"Unexpected strike table in {path}")
    for name, index in strikes.items():
        if not 0 <= index < count:
            raise ValueError(f"Unexpected strike index in {path}")
        if any(category in name for category in ("tz", "kj", "zh", "sh")):
            offset = base + index * 24 + 14
            flags, = struct.unpack_from("<H", data, offset)
            struct.pack_into("<H", data, offset, flags | 32)
    path.write_bytes(data)


def bind_actors(reference, package, metadata):
    configured = reference / "upstream/shared/assets/assets/config/musou/map1"
    areas = json.loads((configured / "map1.json").read_text())["parts"]
    names = set()
    for area in areas:
        missions = json.loads((configured / (area + ".json")).read_text())["missions"]
        for mission in missions:
            for wave in mission["waves"]:
                for enemy in wave["enemies"] + ((wave.get("repeat") or {}).get("enemies") or []):
                    if enemy["isBoss"]:
                        names.add(enemy["id"])
    catalog = package / "MUSOUACT.TXT"
    minions = {name: (file, hp) for name, file, hp in (row.split() for row in catalog.read_text().splitlines())
               if name.startswith("xb_")}
    fighters, _ = roster(reference)
    bosses = {row["name"]: row for row in fighters if row["name"] in names and row["file"]
              and (package / row["file"]).is_file()}
    forms = {row["family"] for row in bosses.values()}
    assets = {row["file"] for row in fighters if row["file"] and row["family"] in forms}
    for file in sorted(assets):
        add_strike_flags(package / file, metadata / Path(file).with_suffix(".json"))
    bindings = minions | {name: (row["file"], 1000) for name, row in bosses.items()}
    catalog.write_text("".join(f"{name} {file} {hp}\n" for name, (file, hp) in sorted(bindings.items())))
    print(f"{len(bosses)}/{len(names)} boss identities have loaded forms. Missing: {', '.join(sorted(names - bosses.keys()))}")


def append_audio(reference, package):
    bank = package / "AUDIO.BVA"
    raw = bank.read_bytes()
    magic, count, start = HEADER.unpack_from(raw)
    if magic != b"BVNAUD01" or start != HEADER.size + count * RECORD.size:
        raise ValueError("Unexpected shared audio bank")
    clips = {name.rstrip(b"\0"): (offset - start, samples)
             for name, offset, samples in RECORD.iter_unpack(raw[HEADER.size:start])}
    if b"boss-naruto" in clips:
        return
    data = bytearray(raw[start:])
    bgm = reference / "tag-assets/shared/assets/assets/bgm"
    sounds = reference / "flash-source/SRC_XFL/ui/musou/LIBRARY"
    paths = {
        b"boss-bleach": bgm / "bleachboss.mp3",
        b"boss-naruto": bgm / "narutoboss.mp3",
        b"fx-boss-warning": sounds / "s系统-警告-重-001.wav",
    }
    with tempfile.TemporaryDirectory(prefix="bvn-boss-audio-") as directory:
        converted = Path(directory) / "decoded.wav"
        for name, path in paths.items():
            pcm = from_audio(path, converted)
            converted.unlink()
            clips[name] = (len(data), len(pcm) // 2)
            data.extend(pcm)
    new_start = HEADER.size + len(clips) * RECORD.size
    if len(clips) > CLIP_LIMIT or new_start + len(data) > 128 * 1024 * 1024:
        raise ValueError("Boss audio exceeds the PSP bank limits")
    index = b"".join(RECORD.pack(name, new_start + offset, samples)
                     for name, (offset, samples) in sorted(clips.items()))
    bank.write_bytes(HEADER.pack(magic, len(clips), new_start) + index + data)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("metadata", type=Path)
    parser.add_argument("--reference", type=Path, default=Path("reference"))
    args = parser.parse_args()
    bind_actors(args.reference, args.package, args.metadata)
    append_audio(args.reference, args.package)
