#!/usr/bin/env python3
"""Bake the original Musou ninja banks and bind their authored starting HP."""
import argparse
import json
from pathlib import Path
from shutil import move

from build_musou_xfl import build

NINJAS = {
    "xb_ninja_1": "XBNINJA1",
    "xb_ninja_2": "XBNINJA2",
    "xb_ninja_fire1": "XBNFIRE1",
    "xb_ninja_fire2": "XBNFIRE2",
    "xb_ninja_earth1": "XBNEART1",
    "xb_ninja_sand1": "XBNSAND1",
    "xb_ninja_thunder1": "XBNTHUN1",
}


def pack(reference, package, scratch):
    catalog = package / "MUSOUACT.TXT"
    rows = {name: (file, hp) for name, file, hp in (line.split() for line in catalog.read_text().splitlines())}
    for actor, bank in NINJAS.items():
        output = bank + ".BVF"
        trace = scratch / actor
        destination = package / output
        build(reference, actor, destination, trace)
        move(destination.with_suffix(".json"), trace / "bank.json")
        details = json.loads((trace / "source-actor.json").read_text())
        hp = details["baseHp"]
        if hp <= 0 or hp != int(hp):
            raise ValueError(f"Invalid authored HP for {actor}")
        rows[actor] = output, int(hp)
    catalog.write_text("".join(f"{name} {file} {hp}\n" for name, (file, hp) in sorted(rows.items())))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("scratch", type=Path)
    parser.add_argument("--reference", type=Path, default=Path("reference"))
    args = parser.parse_args()
    pack(args.reference, args.package, args.scratch)
