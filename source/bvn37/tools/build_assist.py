#!/usr/bin/env python3
"""Compile an assist's own SWF timeline and detached attacks."""
import argparse
import json
import os
import re
import subprocess
from pathlib import Path

from build_actions import detached_scripts, verified_source
from fighter_actions import frame_scripts, hit_definitions, read_constants
from pack_actions import (ASSIST_CALL, ASSIST_OWNER_PROBE, ClipPlan, child_requests, compile_pack, conditional_parts,
                          detached_calls, statements)
from pack_fighter import read_timeline
from assist_scripts import prepare_assist_scripts, prepare_assist_attackers


def build(args):
    reference = args.reference.resolve()
    _, source = verified_source(reference, "assists", args.name)
    out = args.scratch / args.name
    out.mkdir(parents=True, exist_ok=True)
    jpexs = args.jpexs.resolve()
    scripts = args.decompiled
    if scripts is None:
        subprocess.run([args.java, "-Xmx512m", "-jar", jpexs / "ffdec.jar", "-config", "autoDeobfuscate=1",
                        "-export", "script", out, source], check=True, timeout=180)
        scripts = out / "scripts"
    main = scripts / "main_mc.as"
    frames = dict(frame_scripts(main))
    constants_root = reference / "upstream/CORE_Shared/src/net/play5d/game/bvn/data"
    constants = read_constants([constants_root / "HitType.as", constants_root / "fighter/FighterHurtType.as"])
    script_text = main.read_text(encoding="utf-8")
    frames = prepare_assist_scripts(args.name, script_text, frames)
    hits = hit_definitions(script_text, constants)
    local_constants = {name: float(value) for name, value in
                       re.findall(r"\bconst\s+(\w+)\s*:\s*Number\s*=\s*(-?\d+(?:\.\d+)?)\s*;", script_text)}
    (out / "hits.json").write_text(json.dumps(hits, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    ids = out / "hit-ids.txt"
    ids.write_text("".join(name + "\n" for name in hits), encoding="utf-8")
    classpath = os.pathsep.join(map(str, (args.classes, jpexs / "ffdec.jar", jpexs / "lib/*")))
    extractor = [args.java, "-Xmx512m", "-cp", classpath, "ExtractFighter", source, "--assist", ids, out]
    subprocess.run([*extractor, "--metadata"], check=True, timeout=120)
    metadata = read_timeline(out)
    if metadata.frame_count < 3 or max(frames) > metadata.frame_count:
        raise ValueError("Assist root timeline does not match main_mc scripts")
    launches = detached_calls([ClipPlan("assist", 2, metadata.frame_count + 1)], frames, assist=True)
    probes = set()
    for script in frames.values():
        for statement in statements(script):
            call = ASSIST_CALL.fullmatch(statement)
            if call and call[1] == "setHitTarget":
                probes.add(json.loads(f"[{call[2]}]")[0])
            if (condition := conditional_parts(statement)) and (check := ASSIST_OWNER_PROBE.fullmatch(condition[0].strip())):
                probes.add(json.loads(check[1]))
    requests = out / "objects.tsv"
    rows = [f"{kind}\t{frame}\t{name}\n" for kind, frame, name in dict.fromkeys(key[:3] for key in launches)]
    rows.extend(f"probe\t0\t{name}\n" for name in sorted(probes))
    if any(key[0] == "attacker" for key in launches):
        rows.extend(f"{kind}\t{frame}\t{symbol}\t{name}\n"
                    for kind, frame, symbol, name in child_requests(scripts))
    requests.write_text("".join(rows), encoding="utf-8")
    subprocess.run([*extractor, "--objects", requests, "1", str(metadata.frame_count)], check=True, timeout=180)
    timeline = read_timeline(out)
    detached = detached_scripts(timeline, scripts)
    prepare_assist_attackers(args.name, timeline, scripts, detached)
    compile_pack(out, timeline, frames, [], (1, 1, 1), args.output,
                 detached, assist=True, assist_constants=local_constants)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("name")
    parser.add_argument("output", type=Path)
    parser.add_argument("--reference", type=Path, default=Path("reference"))
    parser.add_argument("--jpexs", type=Path, required=True)
    parser.add_argument("--classes", type=Path, required=True)
    parser.add_argument("--java", default="java")
    parser.add_argument("--scratch", type=Path, required=True)
    parser.add_argument("--decompiled", type=Path)
    build(parser.parse_args())
