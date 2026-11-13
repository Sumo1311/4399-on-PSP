#!/usr/bin/env python3
"""Fetch the official tag assets individually and check each Git blob before installing it."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
from urllib.parse import quote


def valid(path, entry):
    if not path.is_file() or path.stat().st_size != entry["size"]:
        return False
    digest = hashlib.sha1(f'blob {entry["size"]}\0'.encode())
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest() == entry["sha"]


def fetch(entry, mirrors, destination, seed):
    name = entry["path"]
    target = destination / name
    if valid(target, entry):
        return False
    target.parent.mkdir(parents=True, exist_ok=True)
    pending = target.with_name(target.name + ".part")
    try:
        if seed and valid(seed / name, entry):
            shutil.copyfile(seed / name, pending)
        else:
            errors = []
            transfer_timeout = str(max(45, entry["size"] // 35000 + 30))
            for mirror in mirrors:
                url = mirror + quote(name, safe="/")
                result = subprocess.run(["curl", "-4", "--http1.1", "--fail", "--location", "--silent",
                                         "--show-error", "--connect-timeout", "10", "--max-time", transfer_timeout,
                                         "--output", pending, url], capture_output=True, text=True)
                if result.returncode == 0 and valid(pending, entry):
                    pending.replace(target)
                    return True
                errors.append(result.stderr.strip() or "Size or Git blob hash mismatch")
            raise ValueError("; ".join(errors))
        if not valid(pending, entry):
            raise ValueError("Size or Git blob hash mismatch")
        pending.replace(target)
        return True
    finally:
        pending.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tree", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--manifest", type=Path, default=Path(__file__).resolve().parents[1] / "reference/manifest.json")
    parser.add_argument("--seed", type=Path)
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()

    source = json.loads(args.manifest.read_text())["tag_assets"]
    tree = json.loads(args.tree.read_text())
    if tree.get("truncated") or tree["sha"] != source["commit"]:
        raise ValueError("Tree does not match the pinned, complete TagAssets snapshot")
    repository = source["url"].removeprefix("https://github.com/")
    commit = source["commit"]
    mirrors = (f"https://cdn.jsdelivr.net/gh/{repository}@{commit}/",
               f"https://raw.githubusercontent.com/{repository}/{commit}/")
    entries = []
    seen = set()
    for entry in tree["tree"]:
        if entry["type"] != "blob":
            continue
        path = PurePosixPath(entry["path"])
        if path.is_absolute() or ".." in path.parts or str(path).casefold() in seen or entry["mode"] not in ("100644", "100755"):
            raise ValueError(f"Unsupported asset path or file type: {path}")
        seen.add(str(path).casefold())
        entries.append(entry)
    entries.sort(key=lambda entry: entry["size"])
    failures = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        downloads = {pool.submit(fetch, entry, mirrors, args.destination, args.seed): entry for entry in entries}
        for done, future in enumerate(as_completed(downloads), 1):
            try:
                future.result()
            except (OSError, ValueError) as error:
                failures += 1
                print(f'{downloads[future]["path"]}: {error}', flush=True)
            if done % 25 == 0 or done == len(entries):
                print(f"{done}/{len(entries)} blobs checked, {failures} failed", flush=True)
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
