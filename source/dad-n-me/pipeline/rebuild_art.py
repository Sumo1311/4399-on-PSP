from pathlib import Path
import argparse, hashlib, json, subprocess

r=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
tested=json.loads((r/'tested-art.json').read_text())
assert sha(r/'source-original-art.pak')==tested['source_sha256']
subprocess.run(['clang','-O2','-c',r.parent/'vendor/lz4/lz4.c','-o',out/'lz4.o'],check=True)
subprocess.run(['clang++','-std=c++17','-O2',r/'pack_tiles.cpp',out/'lz4.o','-lz','-o',out/'pack-tiles'],check=True)
result=subprocess.run([out/'pack-tiles',r/'source-original-art.pak',out/'art.pak',out/'mapping.csv'],text=True,capture_output=True,check=True)
manifest=json.loads(result.stdout)
assert sha(out/'art.pak')==tested['bank_sha256']
subprocess.run(['python3',r/'verify_tiles.py','--source',r/'source-original-art.pak','--output',out,'--lz4-source',r.parent/'vendor/lz4/lz4.c'],check=True)
report=dict(status='pass',art_sha256=sha(out/'art.pak'),original_rgba_sha256=tested['source_sha256'],manifest=manifest,independent_verification=json.loads((out/'independent-verification.json').read_text()))
(out/'REBUILD_ART.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
