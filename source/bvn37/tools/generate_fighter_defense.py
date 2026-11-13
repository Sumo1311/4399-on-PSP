#!/usr/bin/env python3
"""Bind authored defense styles to immutable packed combat data, not filenames."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from build_musou_xfl import XflProject
from build_actions import verified_source
from fighter_actions import frame_scripts
from generate_ai_hit_queries import strike_records


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def profile(initializer):
    values = re.findall(r"\$fighter_ctrler\.defenseType\s*=\s*([^;]+);", initializer)
    if not values:
        return 0  # FighterMain's declared SWORD default.
    if len(values) != 1 or values[0] not in ("0", "1", "FighterDefenseType.SWORD", "FighterDefenseType.HAND"):
        raise ValueError(f"Unsupported authored defense setter: {values}")
    return int(values[0] in ("1", "FighterDefenseType.HAND"))


def build(args):
    catalog = json.loads((args.reference / "source_catalog.json").read_text())["fighters"]
    original = json.loads(args.original.read_text())
    scripts = {sha(p): p for p in args.original.parent.glob("swf-*/scripts/main_mc.as")}
    main = args.reference / "upstream/CORE_KernelLogic/src/net/play5d/game/bvn/fighter/FighterMain.as"
    if not re.search(r"defenseType\s*:\s*int\s*=\s*FighterDefenseType.SWORD", main.read_text()):
        raise ValueError("FighterMain's default defense style changed")
    evidence, signatures = [], {}

    def add(bank, initializer, proof):
        path = args.package / bank
        stats = []
        for name in ("speed", "jumpPower", "heavy"):
            match = re.search(rf"\$fighter_ctrler\.{name}\s*=\s*(\d+(?:\.\d+)?)\s*;", initializer)
            if not match:
                raise ValueError(f"Missing authored {name} for {bank}")
            stats.append(float(match[1]))
        if path.read_bytes()[28:40] != struct.pack("<3f", *stats):
            raise ValueError(f"Authored stats differ from the selected bank: {bank}")
        _, signature = strike_records(path)
        style = profile(initializer)
        prior = signatures.setdefault(signature, (style, bank))
        if prior[0] != style:
            raise ValueError(f"Different defense styles share a combat-data signature: {prior[1]}, {bank}")
        evidence.append(dict(bank=bank, signature=f"{signature:016x}", defense=style, bank_sha256=sha(path), **proof))

    for row in original["banks"]:
        script = scripts[row["decompiled_main_sha256"]]
        swf = Path(row["swf"])
        if sha(swf) != row["swf_sha256"]:
            raise ValueError(f"Original SWF identity changed: {swf}")
        frames = dict(frame_scripts(script))
        frame = row["main_frame"] + 1
        if row["variant"] == "byakuya_bankai_old":
            if frame not in (2, 3) or set(frames) != {1, 2}:
                raise ValueError("Unexpected legacy Byakuya initializer")
            frame = 2  # The original seek clamps to the last main timeline frame.
        add(row["bank"], frames[frame], {"variant": row["variant"], "initializer_frame": frame,
            "script": str(script), "script_sha256": sha(script), "swf": str(swf), "swf_sha256": sha(swf)})

    for line in (args.package / "MUSOUACT.TXT").read_text().splitlines():
        name, bank, hp = line.split()
        if not name.startswith("xb_"):
            continue
        row = catalog[name]
        if row["source"]["kind"] == "swf":
            _, swf = verified_source(args.reference, "fighters", name)
            script = args.support / "actors" / name / "scripts/main_mc.as"
            frame = row["start_frame"] + 1
            add(bank, dict(frame_scripts(script))[frame], {"variant": name, "initializer_frame": frame,
                "script": str(script), "script_sha256": sha(script), "swf": str(swf), "swf_sha256": sha(swf)})
            continue
        document = args.reference / "flash-source" / row["source"]["path"]
        project = XflProject(document)
        guide = project.document.find("./{*}timelines/{*}DOMTimeline/{*}layers/{*}DOMLayer/{*}frames/{*}DOMFrame/{*}elements/{*}DOMSymbolInstance")
        symbol = guide.get("libraryItemName")
        script = project.root / "LIBRARY" / (symbol + ".xml")
        frame = row["start_frame"] + 1
        add(bank, project.symbol(symbol).scripts[frame], {"variant": name, "initializer_frame": frame,
            "script": str(script), "script_sha256": sha(script), "document_sha256": sha(document)})

    expected = {p.name for p in args.package.glob("*.BVF")} - {"COMBAT.BVF", "REACTION.BVF"}
    if {row["bank"] for row in evidence} != expected:
        raise ValueError("The authored profile set does not cover every fighter bank")
    text = ("// Generated from pinned SWF/XFL initializers by generate_fighter_defense.py.\n"
            "// SPDX-License-Identifier: GPL-3.0-or-later\n"
            "struct FighterDefenseProfile { std::uint64_t signature; unsigned type; };\n"
            "inline constexpr FighterDefenseProfile FighterDefenseProfiles[]{\n" +
            "".join(f"    {{0x{sig:016x}ULL, {style}}}, // {bank}\n"
                    for sig, (style, bank) in sorted(signatures.items())) + "};\n")
    args.output.write_text(text)
    report = {"banks": sorted(evidence, key=lambda r: r["bank"]), "unique_signatures": len(signatures),
        "hand": sum(row["defense"] for row in evidence), "default_source_sha256": sha(main),
        "header_sha256": hashlib.sha256(text.encode()).hexdigest(),
        "scope": "Original initializer defense style bound by FNV64 of stats and complete packed strike table; source inputs and all signature collisions verified"}
    args.manifest.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Verified {len(evidence)} fighter banks: HAND {report['hand']}, SWORD {len(evidence)-report['hand']}")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("output", type=Path)
    p.add_argument("--package", type=Path, required=True)
    p.add_argument("--original", type=Path, required=True)
    p.add_argument("--support", type=Path, required=True)
    p.add_argument("--reference", type=Path, required=True)
    p.add_argument("--manifest", type=Path, required=True)
    build(p.parse_args())
