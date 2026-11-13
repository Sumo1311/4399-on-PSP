#!/usr/bin/env python3
"""Compare the prefix-sum filter against an independent direct weighted sum."""
import hashlib,json,math
from pathlib import Path
import numpy as np
from pack_map_blur import filter_axis

def reference(image,width,axis):
    if width<=1:return image.copy()
    radius=math.ceil((width-1)/2);edge=255 if width%2 else 127
    out=np.empty_like(image)
    for y in range(image.shape[0]):
        for x in range(image.shape[1]):
            for channel in range(4):
                value=0
                for delta in range(-radius,radius+1):
                    sx=x+delta if axis==1 else x;sy=y+delta if axis==0 else y
                    if 0<=sx<image.shape[1] and 0<=sy<image.shape[0]:
                        value+=int(image[sy,sx,channel])*(edge if abs(delta)==radius else 255)
                out[y,x,channel]=value//(255*width)
    return out

def run(output):
    rng=np.random.default_rng(49);cases=0;pixels=0;digest=hashlib.sha256()
    for width,height in [(1,1),(2,3),(7,5),(19,13)]:
        for pattern in range(5):
            image=rng.integers(0,256,(height,width,4),dtype=np.uint8)
            if pattern==0:image[:]=255
            if pattern==1:image[:]=0;image[height//2,width//2]=255
            if pattern==2:image[:,:,3]=127;image[:,:,:3]=image[:,:,:3]//2
            if pattern==3:image[:,:,3]=1;image[:,:,:3]=image[:,:,:3]%2
            for size in [0,1,2,3,4,6,8,9,10,12,16,18,20,24,30]:
                for axis in [0,1]:
                    actual=filter_axis(image,size,axis);expected=reference(image,size,axis)
                    assert np.array_equal(actual,expected),(width,height,pattern,size,axis)
                    cases+=1;pixels+=width*height;digest.update(actual.tobytes())
    report={'pass':True,'cases':cases,'pixels':pixels,'sha256':digest.hexdigest(),
        'scope':'Exact optimized prefix-sum versus independently enumerated fixed-weight transparent-border kernel on synthetic channel/alpha patterns. These are declared integer-model tests, not original Flash raster or a running Ruffle GPU.'}
    Path(output).write_text(json.dumps(report,indent=2)+'\n');print('PASS blur integer filter model',cases,pixels)
if __name__=='__main__':
    import sys
    run(sys.argv[1])
