#!/usr/bin/env python3
"""Compile the pinned Flash Musou world, missions, and wave definitions."""
import argparse
import json
from pathlib import Path
import struct

HEADER = struct.Struct("<8s16s4H")
AREA = struct.Struct("<12s16sIHH")
MISSION = struct.Struct("<8s24s4H")
WAVE = struct.Struct("<7H")
ENEMY = struct.Struct("<32sI4H")
MAGIC = b"BVNMUS01"


def load(path):
    return json.loads(path.read_text(encoding="utf-8"))


def label(value, size):
    raw = value.encode("ascii")
    if not raw or len(raw) >= size or b"\0" in raw:
        raise ValueError(f"Invalid source identifier {value!r}")
    return raw


def number(value, lo=0, hi=65535):
    if type(value) is not int or not lo <= value <= hi:
        raise ValueError(f"Invalid Musou integer {value!r}")
    return value


def build(reference, target):
    config = reference / "upstream/shared/assets/assets/config"
    world_dir = config / "musou/map1"
    world = load(world_dir / "map1.json")
    known_fighters = load(reference / "source_catalog.json")["fighters"]
    known_maps = set(load(config / "packs.json")["maps"]) - {"random"}
    route = world["way"]
    indices = {edge["P"]: i for i, edge in enumerate(route)}
    if not 1 <= len(route) <= 32 or len(indices) != len(route) or set(world["parts"]) != set(indices):
        raise ValueError("The world graph and area list disagree")

    areas, missions, waves, enemies = [], [], [], []

    def add_group(group):
        begin = len(enemies)
        for enemy in group:
            if set(enemy) - {"id", "amount", "isBoss", "hp", "atk", "exp", "money"}:
                raise ValueError(f"Unexpected enemy fields {enemy}")
            identity, boss = enemy["id"], enemy["isBoss"]
            if identity not in known_fighters or type(boss) is not bool:
                raise ValueError(f"Unknown or invalid enemy {identity}")
            reward = 100 if boss else 10
            enemies.append(ENEMY.pack(label(identity, 32), number(enemy.get("hp", 0), hi=2**31 - 1),
                                      number(enemy.get("atk", 0)), number(enemy.get("exp", 0) or reward),
                                      number(enemy.get("money", 0) or reward),
                                      (number(enemy["amount"], 1, 32767) << 1) | int(boss)))
        return begin, len(enemies) - begin

    for index, edge in enumerate(route):
        identity, previous = edge["P"], edge.get("N")
        prerequisites = previous if isinstance(previous, list) else [previous] if previous else []
        if len(set(prerequisites)) != len(prerequisites) or any(indices[parent] >= index for parent in prerequisites):
            raise ValueError(f"Invalid prerequisites for {identity}")
        if bool(index) != bool(prerequisites):
            raise ValueError(f"Unexpected starting area {identity}")
        area = load(world_dir / f"{identity}.json")
        if area["id"] != identity or not area["missions"]:
            raise ValueError(f"Invalid area {identity}")
        mission_begin = len(missions)
        mission_ids = set()
        for mission in area["missions"]:
            if set(mission) != {"id", "map", "time", "enemyLevel", "waves"} or mission["map"] not in known_maps:
                raise ValueError(f"Invalid mission in {identity}")
            if mission["id"] in mission_ids or not mission["waves"]:
                raise ValueError(f"Duplicate or empty mission in {identity}")
            mission_ids.add(mission["id"])
            wave_begin = len(waves)
            for wave in mission["waves"]:
                if set(wave) - {"hold", "enemies", "repeat"} or not wave["enemies"]:
                    raise ValueError(f"Invalid wave in {identity}")
                first, count = add_group(wave["enemies"])
                first_repeat, repeat_count, interval, mode = len(enemies), 0, 0, 0
                if repeat := wave.get("repeat"):
                    if set(repeat) != {"type", "hold", "enemies"} or not repeat["enemies"]:
                        raise ValueError(f"Invalid repeat in {identity}")
                    first_repeat, repeat_count = add_group(repeat["enemies"])
                    interval, mode = number(repeat["hold"]), number(repeat["type"], 0, 1) + 1
                waves.append(WAVE.pack(number(wave["hold"]), first, count, first_repeat, repeat_count, interval, mode))
            missions.append(MISSION.pack(label(mission["id"], 8), label(mission["map"].upper(), 24),
                                         number(mission["time"], 1), number(max(1, mission["enemyLevel"]), 1),
                                         wave_begin, len(waves) - wave_begin))
        mask = sum(1 << indices[parent] for parent in prerequisites)
        areas.append(AREA.pack(label(identity, 12), label(area["name"], 16), mask,
                               mission_begin, len(missions) - mission_begin))

    tables = (areas, missions, waves, enemies)
    target.write_bytes(HEADER.pack(MAGIC, label(world["id"], 16), *(len(table) for table in tables)) +
                       b"".join(record for table in tables for record in table))
    print(f"{len(areas)} areas, {len(missions)} missions, {len(waves)} waves and {len(enemies)} spawn groups")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--reference", type=Path, default=Path("reference"))
    args = parser.parse_args()
    build(args.reference, args.output)
