#!/usr/bin/env python3
"""Prepare declared controller scenarios; no release assets are modified."""
import argparse
import json
from pathlib import Path
import struct


def build(args):
    args.output.mkdir()
    profiles = json.loads(args.profiles.read_text())["banks"]
    (args.output / "STYLES.IN").write_text(str(len(profiles)) + "\n" +
        "".join(f"{r['bank']} {r['defense']}\n" for r in profiles))
    for name, boss, hp, amount in (("MUSMIN", False, 50000, 6), ("MUSBOSS", True, 50000, 1),
                                   ("MUSLETH", True, 1, 1), ("MUSSELF", False, 50000, 1)):
        data = struct.pack("<8s16s4H", b"BVNMUS01", b"qa-world", 1, 1, 1, 1)
        data += struct.pack("<12s16sI2H", b"qa-area", b"qa-area", 0, 0, 1)
        data += struct.pack("<8s24s4H", b"qa", b"jianchang", 120, 1, 0, 1)
        data += struct.pack("<7H", 1, 0, 1, 1, 0, 0, 0)
        data += struct.pack("<32sI4H", b"qa-enemy", hp, 1, 1, 1, amount * 2 + int(boss))
        (args.output / (name + ".BVM")).write_bytes(data)
    print(f"Prepared {len(profiles)} authored defense styles and four declared Musou controller fixtures")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("profiles", type=Path)
    p.add_argument("output", type=Path)
    build(p.parse_args())
