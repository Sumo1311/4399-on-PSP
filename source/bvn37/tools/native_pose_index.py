"""Match original PNG raster poses to already-qualified low-density texels."""
from pathlib import Path
import concurrent.futures
import argparse
import hashlib
import json
import os
import struct
import zlib
from PIL import Image

ROOT=Path.cwd()
WORK=Path.cwd()
SCALE=272/600
def power(n):return max(4,1<<(n-1).bit_length())
def index_batch(task):
    root,paths=task
    root=Path(root)
    found={};seen=set()
    for relative in paths:
        p=root/relative
        try:
            encoded=p.read_bytes();identity=hashlib.sha256(encoded).digest()
            if identity in seen:continue
            seen.add(identity)
            with Image.open(p) as src:
                size=tuple(max(1,round(n*SCALE)) for n in src.size)
                if max(size)>256:continue
                low=src.convert('RGBA').resize(size,Image.Resampling.NEAREST)
                bounds=low.getbbox(alpha_only=True)
                if bounds is None:continue
                cropped=low.crop(bounds);stride,rows=map(power,cropped.size)
                if stride>256 or rows>256:continue
                padded=Image.new('RGBA',(stride,rows));padded.paste(cropped)
                for premultiplied in [False,True]:
                    raw=(padded.convert('RGBa') if premultiplied else padded).tobytes()
                    key=f'{stride}x{rows}:'+hashlib.sha256(raw).hexdigest()
                    if key not in found:found[key]=[relative,list(size),list(bounds),premultiplied,identity.hex()]
        except (OSError,ValueError):continue
    return found
def main():
    global ROOT
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--paths',type=Path,required=True,help='Ordered JSON list of original pose PNG paths relative to root')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();ROOT=args.root.resolve()
    paths=json.loads(args.paths.read_text())
    # Preserve deterministic priority when duplicate low-density poses exist.
    batches=[paths[i:i+3000] for i in range(0,len(paths),3000)]
    combined={}
    with concurrent.futures.ProcessPoolExecutor(max_workers=4) as pool:
        for i,result in enumerate(pool.map(index_batch,[(str(ROOT),batch) for batch in batches])):
            for key,value in result.items():combined.setdefault(key,value)
            if i%8==0:print('INDEX',min((i+1)*3000,len(paths)),len(combined),flush=True)
    args.output.write_text(json.dumps(combined,separators=(',',':')))
    print('INDEX COMPLETE',len(paths),len(combined),flush=True)
if __name__=='__main__':main()
