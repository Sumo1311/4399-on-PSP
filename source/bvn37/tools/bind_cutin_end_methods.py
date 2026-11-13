#!/usr/bin/env python3
"""Bind original end-method identity without rebaking any animation pixels."""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

from build_actions import verified_source
from fighter_actions import frame_scripts
from pack_actions import HEADER, CLIP, FRAME, HIT, STRIKE, EVENT, OPS
import re

METHODS = {"endBisha": 1, "endWanKai": 2, "endGhostStep": 3}
CALL = re.compile(r"\.(endBisha|endWanKai|endGhostStep)\s*\(")
SHA = lambda data: hashlib.sha256(data).hexdigest()


def bank_events(data):
    magic, frames, hits, events, pixels, clips, strikes, *_ = HEADER.unpack_from(data)
    assert magic.startswith(b"BVNFGT"), magic
    version = int(magic[-2:]);assert 11 <= version <= 15
    commands = 24 if version >= 12 else 22
    clip_start = HEADER.size + 12 + 2 * (version >= 14) + commands * (2 + 16)
    frame_start = clip_start + clips * CLIP.size
    event_start = frame_start + frames * FRAME.size + hits * HIT.size + strikes * STRIKE.size
    assert event_start + events * EVENT.size <= pixels
    ends = []
    for clip in range(clips):
        first, count, _, source_start = CLIP.unpack_from(data, clip_start + clip * CLIP.size)
        for phase in range(count):
            frame = FRAME.unpack_from(data, frame_start + (first + phase) * FRAME.size)
            _, begin, _, length = frame[-4:]
            row = []
            for index in range(begin, begin + length):
                at = event_start + index * EVENT.size
                event = EVENT.unpack_from(data, at)
                if event[0] == OPS.index("endCutin"):
                    assert event[1] == 0 and event[2] == 0xffff and event[3:] == (0, 0)
                    row.append((index, at))
            if row:
                assert source_start
                ends.append(dict(clip=clip, phase=phase, frame=source_start+phase, events=row))
    return ends, pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("reference", type=Path)
    parser.add_argument("caches", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("scratch", type=Path)
    parser.add_argument("--java", required=True)
    parser.add_argument("--jpexs", type=Path, required=True)
    args = parser.parse_args()
    assert not args.output.exists()
    caches = json.loads(args.caches.read_text())
    exports, bindings = {}, []
    for line in Path(__file__).with_name("versus_fighters.txt").read_text().splitlines():
        name, basename, family, *internal = line.split();bank = basename + ".BVF"
        variant, swf = verified_source(args.reference, "fighters", family if internal else name)
        data = (args.package / bank).read_bytes();ends, pixels = bank_events(data)
        cache = Path(caches[bank]);tsv = (cache / "fighter.tsv").read_text()
        symbols = [line.split("\t", 2)[2] for line in tsv.splitlines() if line.startswith("symbol\t")]
        assert symbols, bank
        keys = []
        for symbol in symbols:
            key = (str(swf), symbol)
            if key not in exports:
                digest = SHA(swf.read_bytes())
                folder = args.scratch / (digest[:16] + "-" + SHA(symbol.encode())[:12])
                exports[key] = dict(swf=swf, swf_sha256=digest, symbol=symbol, folder=folder)
            keys.append(key)
        bindings.append(dict(name=name, bank=bank, keys=keys, ends=ends, pixels=pixels, data=data))

    def export(item):
        folder = item["folder"];folder.mkdir(parents=True, exist_ok=True)
        script = folder / "scripts" / Path(*item["symbol"].split(".")).with_suffix(".as")
        manifest = folder / "export.json"
        expected = {k:str(item[k]) for k in ["swf", "swf_sha256", "symbol"]}
        if not manifest.exists() or json.loads(manifest.read_text()) != expected or not script.is_file():
            command = [args.java, "-Xmx512m", "-jar", str(args.jpexs / "ffdec.jar"), "-config", "autoDeobfuscate=1",
                       "-selectclass", item["symbol"], "-export", "script", str(folder), str(item["swf"])]
            with (folder / "export.log").open("w") as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
            assert script.is_file(), script
            manifest.write_text(json.dumps(expected, sort_keys=True) + "\n")
        bodies = dict(frame_scripts(script))
        return dict(item, script=script, script_sha256=SHA(script.read_bytes()),
                    methods={frame:CALL.findall(body) for frame,body in bodies.items()})

    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as executor:
        exported = list(executor.map(export, exports.values()))
    exports = {(str(row["swf"]), row["symbol"]):row for row in exported}
    rows, counts = [], dict.fromkeys(METHODS, 0)
    for binding in bindings:
        candidates = []
        for key in binding["keys"]:
            item = exports[key]
            if all(len(item["methods"].get(row["frame"], [])) == len(row["events"]) for row in binding["ends"]):
                candidates.append(item)
        assert candidates, (binding["bank"], "No original class matches every packed end-event frame")
        variants = {tuple(tuple(item["methods"].get(row["frame"], [])) for row in binding["ends"]) for item in candidates}
        assert len(variants) == 1, (binding["bank"], "Ambiguous original end-method identity")
        source = candidates[0];data = bytearray(binding["data"]);changes = []
        for row in binding["ends"]:
            for (index, at), method in zip(row["events"], source["methods"][row["frame"]]):
                data[at+1] = METHODS[method];counts[method] += 1
                changes.append(dict(event=index,offset=at+1,clip=row["clip"],frame=row["frame"],method=method,value=METHODS[method]))
        changed = {row["offset"] for row in changes}
        assert all(a == b or i in changed for i,(a,b) in enumerate(zip(binding["data"],data)))
        assert data[binding["pixels"]:] == binding["data"][binding["pixels"]:]
        rows.append(dict(bank=binding["bank"],before=SHA(binding["data"]),after=SHA(data),
            tail_sha256=SHA(data[binding["pixels"]:]),pixel_start=binding["pixels"],
            original_swf=str(source["swf"]),original_swf_sha256=source["swf_sha256"],
            original_class=source["symbol"],script=str(source["script"]),script_sha256=source["script_sha256"],changes=changes))
        binding["patched"] = data

    args.output.mkdir(parents=True)
    by_name = {row["bank"]: row for row in bindings}
    for source in args.package.iterdir():
        if not source.is_file():continue
        target = args.output / source.name
        if source.name in by_name:target.write_bytes(by_name[source.name]["patched"])
        else:shutil.copyfile(source,target)
    report = dict(banks=len(rows),counts=counts,changes=sum(counts.values()),exports=len(exports),files=rows,
        scope="Original SWF blobs are verified against the pinned tag manifest. FFDec exports only the original actor classes; their frame methods bind each packed endCutin command byte. Every changed byte is one of these command fields. All other tables, compressed pixels, constructor areas, files and EBOOT remain byte-identical. Full runtime controller behavior is a separate acceptance gate.")
    (args.output.parent / (args.output.name + ".end-methods.json")).write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n")
    print(json.dumps({k:report[k] for k in ["banks", "counts", "changes", "exports"]}),flush=True)


if __name__ == "__main__":
    main()
