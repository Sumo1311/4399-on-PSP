#!/usr/bin/env python3
"""Read the authored hit definitions registered by a fighter's variant frame."""
import argparse
import json
import re
from pathlib import Path

FRAME = re.compile(r"\bfunction\s+frame(\d+)\s*\([^)]*\)\s*:\s*\*\s*\{")
TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/|[{}]', re.S)
DEFINE = re.compile(r'\bdefineAction\s*\(\s*("(?:\\.|[^"\\])*")\s*,\s*')
SYMBOLS = re.compile(r'"(?:\\.|[^"\\])*"|\b\w+\.\w+\b')
CONSTANT = re.compile(r'\bconst\s+(\w+)\s*:\s*int\s*=\s*(-?\d+)\s*;')


def block_end(source, opening):
    depth = 0
    for token in TOKEN.finditer(source, opening):
        if token[0] == "{":
            depth += 1
        elif token[0] == "}":
            depth -= 1
            if depth == 0:
                return token.end()
    raise ValueError("Unclosed ActionScript block")


def frame_scripts(path):
    text = path.read_text(encoding="utf-8")
    for match in FRAME.finditer(text):
        start = match.end() - 1
        yield int(match[1]), text[start + 1:block_end(text, start) - 1].strip()


def read_constants(paths):
    return {f"{path.stem}.{name}": value for path in paths
            for name, value in CONSTANT.findall(path.read_text(encoding="utf-8"))}


def extract(script, start_frame, constants=None):
    # FighterMain.initialize skips the preload frame before selecting the variant.
    frames = dict(frame_scripts(script))
    if start_frame + 1 not in frames:
        raise ValueError(f"No initializer on authored frame {start_frame + 1}")
    actions = hit_definitions(frames[start_frame + 1], constants)
    if not actions:
        raise ValueError("Variant has no hit definitions")
    return actions


def hit_definitions(body, constants=None):
    actions = {}
    for match in DEFINE.finditer(body):
        name = json.loads(match[1])
        first = match.end()
        if first == len(body) or body[first] != "{":
            raise ValueError(f"Non-literal definition for {name}")
        last = block_end(body, first)
        definition = body[first:last]
        if constants:
            definition = SYMBOLS.sub(lambda token: constants.get(token[0], token[0]), definition)
        actions[name] = json.loads(definition)
    if len(list(DEFINE.finditer(body))) != len(re.findall(r"\bdefineAction\s*\(", body)):
        raise ValueError("Variant contains unhandled hit definitions")
    return actions


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("main_mc", type=Path)
    parser.add_argument("start_frame", type=int)
    parser.add_argument("output", type=Path)
    parser.add_argument("--constants", type=Path, nargs="*", default=[])
    args = parser.parse_args()
    actions = extract(args.main_mc, args.start_frame, read_constants(args.constants))
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "hits.json").write_text(json.dumps(actions, ensure_ascii=False, indent=2) + "\n")
    (args.output / "hit-ids.txt").write_text("\n".join(actions) + "\n")
    print(f"Imported {len(actions)} hit definitions for start_frame {args.start_frame}")
