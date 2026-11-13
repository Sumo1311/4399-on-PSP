#!/usr/bin/env python3
"""Build a seekable PSP PCM bank from the original versus sounds and music."""

import argparse
import json
import struct
import subprocess
import tempfile
import wave
from pathlib import Path

from build_actions import verified_source
from versus_roster import roster

RATE = 22050
CLIP_LIMIT = 256
HEADER = struct.Struct("<8sII")
RECORD = struct.Struct("<32sII")


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def from_audio(path, scratch):
    subprocess.run(["afconvert", "-f", "WAVE", "-d", f"LEI16@{RATE}", "-c", "2",
                    path, scratch], check=True, capture_output=True)
    with wave.open(str(scratch)) as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate()) != (2, 2, RATE):
            raise ValueError(f"Unexpected converted audio format for {path}")
        return source.readframes(source.getnframes())


def raw_pcm(path, channels, expected_frames):
    pcm = path.read_bytes()
    if len(pcm) != channels * expected_frames * 2:
        raise ValueError(f"Unexpected source PCM length {path}")
    if channels == 2:
        return pcm
    stereo = bytearray(expected_frames * 4)
    for i, (sample,) in enumerate(struct.iter_unpack("<h", pcm)):
        struct.pack_into("<2h", stereo, i * 4, sample, sample)
    return bytes(stereo)


def build(reference, output, jpexs=None, java="java"):
    xfl = reference / "flash-source/SRC_XFL"
    assets = reference / "tag-assets/shared/assets/assets"
    music = assets / "bgm"
    fight = xfl / "ui/fight/LIBRARY"
    common = xfl / "#swc/sound"
    paths = {key: music / f"{key}.mp3" for key in ("op", "select", "loading", "continue", "win", "winloop", "back")}
    paths.update({f'fx-step{i}':assets/'sounds'/f'step{i}.mp3' for i in (1,2,3)})
    config = read_json(reference / "upstream/shared/assets/assets/config/packs.json")
    for stage in config["maps"]:
        if stage != "random":
            name = read_json(assets / "packs/maps" / stage / "meta.json").get("bgm")
            if name:
                paths["map-" + stage] = music / name
    fighters, _ = roster(reference)
    for fighter in fighters:
        if fighter["theme"]:
            paths[fighter["name"]] = music / fighter["theme"]["url"]
    paths.update({"fx-" + name: fight / (name + ".wav") for name in
                  ("round", "one", "two", "three", "four", "finalround", "fight", "timeup", "drawgame")})
    paths["fx-ko"] = common / "LIBRARY/ko2.wav"

    data = bytearray()
    clips = {}
    with tempfile.TemporaryDirectory(prefix="bvn-pcm-") as directory:
        scratch = Path(directory)
        converted = scratch / "decoded.wav"
        if any(fighter["name"] == "ulquiorra_resurreccion" and fighter["file"] for fighter in fighters):
            if jpexs is None:
                raise ValueError("Set --jpexs to extract Ulquiorra's authored dash sound")
            _, source = verified_source(reference, "fighters", "ulquiorra_resurreccion")
            subprocess.run([java, "-Xmx512m", "-jar", jpexs / "ffdec.jar", "-selectid", "9", "-export",
                            "sound", scratch, source], check=True, timeout=60)
            paths["fx-ulquiorra-dash"] = scratch / "9_dashSnd.mp3"
        locations = {}
        for name, path in sorted(paths.items()):
            if path not in locations:
                pcm = from_audio(path, converted)
                locations[path] = (len(data), len(pcm) // 2)
                data.extend(pcm)
                converted.unlink()
            clips[name] = locations[path]
    for key, base, filename, channels, frames in (
        ("fx-select", common, "M 1 1496381754.dat", 2, 6341),
        ("fx-confirm", common, "M 2 1496381754.dat", 2, 13750),
        ("fx-over-hit", common, "M 10 1496544283.dat", 1, 57152),
        ("fx-ghost-step", common, "M 13 1496844236.dat", 1, 19936),
        ("fx-ghost-foot", xfl / "effect", "M 306 1496844065.dat", 1, 15968),
        ("fx-assist-bleach", xfl / "effect", "M 355 1496904286.dat", 1, 17952),
        ("fx-assist-naruto", xfl / "effect", "M 77 1484795810.dat", 1, 8992),
        ("fx-rush", xfl / "effect", "M 308 1496900369.dat", 1, 17952),
    ):
        pcm = raw_pcm(base / "bin" / filename, channels, frames)
        clips[key] = (len(data), len(pcm) // 2)
        data.extend(pcm)
    clips['fx-counter-replace'] = clips['fx-assist-naruto']
    pcm = raw_pcm(xfl / 'effect/bin/M 1 1484795810.dat', 1, 31505)
    clips['fx-counter-burst'] = (len(data), len(pcm) // 2)
    data.extend(pcm)
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / 'charge.wav'
        converted = Path(directory) / 'converted.wav'
        original = (xfl / 'effect/bin/M 10 1484795810.dat').read_bytes()
        if len(original) != 43145 * 2: raise ValueError('Unexpected original counter charge audio')
        with wave.open(str(source), 'wb') as wav:
            wav.setnchannels(1);wav.setsampwidth(2);wav.setframerate(44100);wav.writeframes(original)
        pcm = from_audio(source, converted)
        clips['fx-counter-charge'] = (len(data), len(pcm) // 2)
        data.extend(pcm)
    if not 0 < len(clips) <= CLIP_LIMIT:
        raise ValueError(f"Audio bindings exceed the PSP clip table limit: {len(clips)}")
    start = HEADER.size + len(clips) * RECORD.size
    index = b"".join(RECORD.pack(key.encode("ascii"), start + offset, samples)
                     for key, (offset, samples) in sorted(clips.items()))
    output.write_bytes(HEADER.pack(b"BVNAUD01", len(clips), start) + index + data)
    print(f"{len(clips)} audio bindings, {len(data)} PCM bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--reference", type=Path, default=Path("reference"))
    parser.add_argument("--jpexs", type=Path)
    parser.add_argument("--java", default="java")
    args = parser.parse_args()
    build(args.reference, args.output, args.jpexs, args.java)
