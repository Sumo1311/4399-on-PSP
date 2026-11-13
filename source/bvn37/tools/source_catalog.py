#!/usr/bin/env python3
"""Resolve the original pack and selection IDs to their version-bound source files."""
import argparse
import json
from pathlib import Path


def load(path):
    return json.loads(path.read_text(encoding="utf-8"))


def source_catalog(config, compiled, flash_source):
    pack_list = load(config / "packs.json")
    select = load(config / "select.json")
    result = {}
    for group, names in pack_list.items():
        entries = {}
        for name in names:
            folder = compiled / group / name
            meta = load(folder / "meta.json")
            if meta["pack"] != name or meta["kind"] != {"fighters": "fighter", "assists": "assist", "maps": "map"}[group]:
                raise ValueError(f"Mismatched metadata for {group}/{name}")
            file = meta.get("file")
            if file and (folder / file).is_file():
                source = {"kind": "swf", "path": (folder / file).relative_to(compiled).as_posix()}
            elif file and group == "fighters" and (xfl := flash_source / "SRC_XFL/fighter/zako" / name / "DOMDocument.xml").is_file():
                source = {"kind": "xfl", "path": xfl.relative_to(flash_source).as_posix()}
            elif not file and name in ("random", "random_bleach", "random_naruto"):
                source = {"kind": "random"}
            else:
                raise ValueError(f"Missing source for {group}/{name}/{file}")
            for variant in meta.get("variants", [meta]):
                variant_id = variant["id"]
                if variant_id in entries:
                    raise ValueError(f"Duplicate ID {group}/{variant_id}")
                entries[variant_id] = {"pack": name, "source": source, "start_frame": variant.get("start_frame")}
        result[group] = entries

    for group, section in (("fighters", "char_list"), ("assists", "assist_list")):
        for page in select[section]:
            for item in page["item"]:
                choices = [item["id"]] if "id" in item else []
                choices.extend(item.get("more_fighter", ()))
                for variant_id in choices:
                    if variant_id not in result[group]:
                        raise ValueError(f"Selection refers to unknown {group} ID {variant_id}")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path)
    parser.add_argument("compiled_packs", type=Path)
    parser.add_argument("flash_source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    catalog = source_catalog(args.config, args.compiled_packs, args.flash_source)
    args.output.write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(", ".join(f"{len(entries)} {group}" for group, entries in catalog.items()))
