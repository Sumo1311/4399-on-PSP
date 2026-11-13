#!/usr/bin/env python3
"""Extract all six-level probability rows from the pinned original AI source."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
from pathlib import Path
import re


def build(reference, output):
    source = reference / "upstream/CORE_KernelLogic/src/net/play5d/game/bvn/fighter/ctrler/ai/FighterAILogic.as"
    text = source.read_text()
    config = reference / "upstream/CORE_KernelLogic/src/net/play5d/game/bvn/GameConfig.as"
    clocks = {}
    for name in ("FPS_GAME", "FPS_ANIMATE"):
        match = re.search(r"public static (?:var|const) " + name + r":int\s*=\s*(\d+);", config.read_text())
        if not match:
            raise ValueError(f"Missing original clock: {name}")
        clocks[name] = int(match.group(1))
    rows = re.findall(r"private static const (R\w+):Array\s*=\s*\[([^]]+)\];", text)
    if len(rows) < 50 or len({key for key, _ in rows}) != len(rows):
        raise ValueError("Unexpected source AI rate declarations")
    lines = ["// Generated from FighterAILogic.as; do not edit probability rows.",
             "// SPDX-License-Identifier: GPL-3.0-or-later",
             "// Source SHA256: " + hashlib.sha256(source.read_bytes()).hexdigest()]
    lines += ["// GameConfig SHA256: " + hashlib.sha256(config.read_bytes()).hexdigest(),
              f"constexpr unsigned SourceGameHz = {clocks['FPS_GAME']};",
              f"constexpr unsigned SourceAiHz = {clocks['FPS_ANIMATE']};"]
    for name, values in rows:
        numbers = [number.strip() for number in values.split(",")]
        if len(numbers) != 6 or any(not 0 <= float(number) <= 10 for number in numbers):
            raise ValueError(f"Unexpected AI rate: {name}")
        lines.append(f"constexpr std::array<double, 6> {name}{{" + ", ".join(numbers) + "};")
    output.write_text("\n".join(lines) + "\n")
    print(f"Extracted {len(rows)} original six-level probability rows")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--reference", type=Path, default=Path("reference"))
    args = parser.parse_args()
    build(args.reference, args.output)
