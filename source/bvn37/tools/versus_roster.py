"""Bind the original selection grid to the fighter packs included in the PSP build."""
import json
import re
import struct
from pathlib import Path

HEADER = struct.Struct("<8s4H")
FIGHTER = struct.Struct("<16s32sH4BH")
CELL = struct.Struct("<2h2H8H")
MISSING = 0xffff
RANDOM = {"random": 1, "random_bleach": 2, "random_naruto": 3}


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def roster(reference, assists=False):
    config = read_json(reference / "upstream/shared/assets/assets/config/select.json")
    group = "assists" if assists else "fighters"
    extension = ".BVS" if assists else ".BVF"
    section = "assist_list" if assists else "char_list"
    catalog = read_json(reference / "source_catalog.json")[group]
    packs = reference / "tag-assets/shared/assets/assets/packs" / group
    qualified = {}
    internal = []
    files = set()
    manifest = Path(__file__).with_name(f"versus_{group}.txt")
    for row in manifest.read_text(encoding="ascii").splitlines():
        fighter, file, family, *frame = row.split()
        source = catalog.get(family if frame else fighter)
        if (fighter in qualified or any(name == fighter for name, *_ in internal) or file in files or
                not re.fullmatch("[A-Z0-9]{1,8}", file) or source is None or source["pack"] != family or
                source["source"]["kind"] != "swf"):
            raise ValueError(f"Invalid qualified fighter {row}")
        files.add(file)
        if frame:
            if assists or fighter in catalog or len(frame) != 1 or not re.fullmatch("[2-8]", frame[0]):
                raise ValueError(f"Invalid internal form {row}")
            internal.append((fighter, file + extension, family, int(frame[0]) - 1))
            continue
        qualified[fighter] = file + extension
    names = dict.fromkeys(("gin", "hinata") if assists else ("ichigo", "naruto"))
    for row in config[section]:
        for item in row["item"]:
            if item and "id" in item:
                names[item["id"]] = None
                names.update(dict.fromkeys(item.get("more_fighter", ())))
    hidden = [name for name in qualified if name not in names]
    for name in hidden:
        source = catalog[name]
        if assists or (source["start_frame"] or 1) <= 1 or not any(
                catalog[visible]["pack"] == source["pack"] for visible in names):
            raise ValueError(f"Qualified fighter missing from the original select grid: {name}")
    names.update(dict.fromkeys(hidden))
    entries = []
    metadata, families = {}, {}
    for name in names:
        pack = catalog[name]["pack"]
        family = families.setdefault(pack, len(families))
        folder = packs / pack
        if pack not in metadata:
            metadata[pack] = read_json(folder / "meta.json")
        meta = metadata[pack]
        variant = next(v for v in meta.get("variants", (meta,)) if v["id"] == name)
        file = qualified.get(name, "")
        music = variant.get("bgm") if file and not assists else None
        faction = variant.get("comic_type", 2)
        entries.append(dict(name=name, file=file, family=family, form=(variant.get("start_frame") or 1) - 1,
                            faction=faction if faction in (0, 1) else 2, theme=music, random=RANDOM.get(name, 0),
                            title=meta["language"]["zh-CN"]["name"][name], folder=folder / "face",
                            faces=variant["faces"], internal=name in hidden))
    for name, file, family, form in internal:
        siblings = [entry for entry in entries if entry["family"] == families[family]]
        if any(entry["form"] == form for entry in siblings):
            raise ValueError(f"Internal form {name} conflicts with a selectable form")
        previous = max((entry for entry in siblings if entry["form"] < form and entry["file"]),
                       key=lambda entry: entry["form"], default=None)
        if previous is None:
            raise ValueError(f"No loaded presentation for internal form {name}")
        entries.append(dict(previous, name=name, file=file, form=form, theme=None, internal=True))
    return entries, config


def write_grid(entries, config, path, assists=False):
    ids = {entry["name"]: i for i, entry in enumerate(entries)}
    layout = config["stage_setting"]["layout"]
    rows = config["assist_list" if assists else "char_list"]
    columns = max(len(row["item"]) for row in rows)
    origin_x, origin_y = layout["x"] + layout["left"], layout["y"] + layout["top"]
    gap_x = (layout["width"] - 50 - layout["left"] - layout["right"]) / max(1, columns - 1)
    gap_y = (layout["height"] - 50 - layout["top"] - layout["bottom"]) / max(1, len(rows) - 1)
    data = bytearray(HEADER.pack(b"BVNAST01" if assists else b"BVNSEL01", len(rows), columns, len(entries), 0))
    for entry in entries:
        theme = entry["theme"]
        data.extend(FIGHTER.pack(entry["file"].encode("ascii"), entry["name"].encode("ascii") if theme else b"",
                                 entry["family"], entry["form"], entry["faction"], theme["rate"] if theme else 0,
                                 entry["random"], int(entry.get("internal", False))))
    for y, row in enumerate(rows):
        rx, ry = row.get("offset", (0, 0))
        for x in range(columns):
            item = (row["item"][x] or {}) if x < len(row["item"]) else {}
            dx, dy = item.get("offset", (0, 0))
            more = [ids[name] for name in item.get("more_fighter", ())]
            data.extend(CELL.pack(round(origin_x + gap_x*x + rx + dx), round(origin_y + gap_y*y + ry + dy),
                                  ids[item["id"]] if "id" in item else MISSING, len(more),
                                  *(more + [MISSING] * (8 - len(more)))))
    path.write_bytes(data)
