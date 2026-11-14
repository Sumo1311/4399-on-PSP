"""Retain authored normal/screen/add passes without changing gameplay tables."""
import argparse,ctypes,hashlib,json,struct,zlib
from pathlib import Path
from PIL import Image,ImageChops
from promote_native_textures import table,load_lz4,TEXTURE
from pack_fighter import FighterTextures
SCALE=272/600
def power(n):return max(4,1<<(n-1).bit_length())
def digest(raw):return hashlib.sha256(raw).hexdigest()
def index_poses(folder):
    result={}
    for path in sorted((folder/'poses').glob('*.png')):
        with Image.open(path) as src:
            source_size=src.size;size=tuple(max(1,round(n*SCALE)) for n in source_size)
            low=src.convert('RGBA').resize(size,Image.Resampling.NEAREST)
        bounds=low.getbbox(alpha_only=True)
        if bounds is None:continue
        crop=low.crop(bounds)
        for y in range(0,crop.height,512):
            for x in range(0,crop.width,512):
                tile=crop.crop((x,y,min(x+512,crop.width),min(y+512,crop.height)))
                if not tile.getbbox(alpha_only=True):continue
                dims=tuple(map(power,tile.size));pad=Image.new('RGBA',dims);pad.paste(tile)
                key=(dims,digest(pad.tobytes()))
                rec=(path.name,size,(bounds[0]+x,bounds[1]+y,bounds[0]+x+tile.width,bounds[1]+y+tile.height))
                if key not in result:result[key]=rec
                key=(dims,digest(pad.convert('RGBa').tobytes()))
                if key not in result:result[key]=rec
        if max(crop.size)>=128:
            compact=FighterTextures(folder,compact=True)
            pose=compact.bake(path.stem,(0,0,*source_size),allow_tiles=True)
            pieces=compact.tiles if pose[5]==0 else [pose]
            for px,py,offset,width,height,stride,rows in pieces:
                raw=bytes(compact.pixels[offset:offset+4*stride*rows]);dims=(stride,rows)
                rec=(path.name,size,(int(px),int(py),int(px)+width,int(py)+height))
                result.setdefault((dims,digest(raw)),rec)
                image=Image.frombytes('RGBA',dims,raw)
                result.setdefault((dims,digest(image.convert('RGBa').tobytes())),rec)
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ['baseline','current','export','dimensions','output','report']:p.add_argument('--'+n,required=True,type=Path)
    p.add_argument('--canonical',type=Path);p.add_argument('--suspects',type=Path);a=p.parse_args();old=a.baseline.read_bytes();current=a.current.read_bytes()
    os_,of,oe=table(old);cs,cf,ce=table(current[:-8]);assert len(oe)==len(ce)
    dims={int(f[2]):tuple(map(int,f[3:5])) for line in a.dimensions.read_text().splitlines()
          if (f:=line.split('\t'))[0]=='TEX' and f[1]==a.current.name}
    index=index_poses(a.canonical or a.export);lib=load_lz4()
    suspect_ids={int(f[2]) for line in a.suspects.read_text().splitlines() if (f:=line.split('\t'))[0]=='SUSPECT' and f[1]==a.current.name} if a.suspects else set()
    lib.LZ4_compress_HC.argtypes=[ctypes.c_char_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_int]
    header=bytearray(current[:cs]);payload=bytearray();blobs=[];appended=[];splits={};affected=[];matched=[];unmatched=[];results=[];exempt=[]
    for i,((o,n,raw_size),(co,cn,craw_size)) in enumerate(zip(oe,ce)):
        encoded_old=old[os_+o:os_+o+n]
        if old[:8] in [b'BVNFGT13',b'BVNFGT15']:
            raw=ctypes.create_string_buffer(raw_size)
            assert lib.LZ4_decompress_safe(encoded_old,raw,n,raw_size)==raw_size
            raw=raw.raw
        else:raw=zlib.decompress(encoded_old)
        assert len(raw)==raw_size
        key=(dims.get(i),digest(raw));rec=index.get(key);encoded=current[cs+co:cs+co+cn];output_size=craw_size
        if not rec:
            if any(raw):unmatched.append(i)
        else:
            matched.append(i);name,size,bounds=rec;spec=a.export/'render-layers'/(name.removesuffix('.png')+'.json')
            if name.startswith(('face-','screen-')):exempt.append(i)
            elif spec.exists():
                modes=json.loads(spec.read_text());assert 1<=len(modes)<=15 and all(m in [0,3,4,5,6,8,9,10,14] for m in modes)
                if any(modes):
                    expanded=[m for mode in modes for m in ([15,8] if mode==14 else [15] if mode==3 else [mode])]
                    assert len(expanded)<=15,(name,len(expanded))
                    stride,rows=dims[i];density=2 if craw_size==raw_size*4 else 1
                    planes=[];sources=[]
                    for plane,mode in enumerate(modes):
                        png=a.export/'render-layers'/name.removesuffix('.png')/(str(plane)+'.png')
                        with Image.open(png) as im:
                            image=im.convert('RGBA')
                            if density==2:image=image.convert('RGBa').resize(tuple(density*n for n in size),Image.Resampling.LANCZOS).convert('RGBA')
                            else:image=image.resize(size,Image.Resampling.NEAREST)
                        image=image.crop(tuple(density*n for n in bounds));pad=Image.new('RGBA',(stride*density,rows*density));pad.paste(image)
                        if mode==10:
                            alpha=pad.getchannel('A');planes.append(Image.merge('RGB',(alpha,alpha,alpha)).convert('RGBA').tobytes())
                        elif mode in (3,14):
                            channels=pad.convert('RGB').split();alpha=pad.getchannel('A')
                            if mode==3:
                                coefficient=Image.merge('RGB',tuple(ImageChops.add(ImageChops.invert(alpha),ImageChops.multiply(c,alpha)) for c in channels)).convert('RGBA')
                                planes.append(coefficient.tobytes())
                            else:
                                multiply=Image.merge('RGB',tuple(ImageChops.invert(ImageChops.multiply(c.point([abs(2*x-255) for x in range(256)]),alpha)) for c in channels)).convert('RGBA')
                                add=Image.merge('RGB',tuple(ImageChops.multiply(c.point([max(2*x-255,0) for x in range(256)]),alpha) for c in channels)).convert('RGBA')
                                planes.extend([multiply.tobytes(),add.tobytes()])
                        else:planes.append(pad.tobytes())
                        sources.append({'path':str(png),'sha256':digest(png.read_bytes())})
                    def encode(combined):
                        packed=ctypes.create_string_buffer(lib.LZ4_compressBound(len(combined)))
                        count=lib.LZ4_compress_HC(combined,packed,len(combined),len(packed),10);assert count>0
                        data=packed.raw[:count];check=ctypes.create_string_buffer(len(combined))
                        assert lib.LZ4_decompress_safe(data,check,count,len(combined))==len(combined) and check.raw==combined
                        return data,len(combined)
                    mode_word=len(expanded)|sum(m<<(4+4*p) for p,m in enumerate(expanded))
                    combined=b''.join(planes);full_size=len(combined)
                    if full_size<=3*1024*1024:
                        encoded,output_size=encode(combined);affected.append((i,mode_word))
                    else:
                        side=512
                        while 4*side*side*len(expanded)>3*1024*1024:side//=2
                        step=side//density;pieces=[]
                        images=[Image.frombytes('RGBA',(stride*density,rows*density),data) for data in planes]
                        for py in range(0,rows,step):
                            for px in range(0,stride,step):
                                width=min(step,stride-px);height=min(step,rows-py)
                                chunk=b''.join(im.crop((px*density,py*density,(px+width)*density,(py+height)*density)).tobytes() for im in images)
                                assert len(chunk)<=3*1024*1024
                                identity=len(ce)+len(appended);appended.append(encode(chunk));affected.append((identity,mode_word))
                                pieces.append((px,py,width,height,identity,width,height))
                        splits[i]=pieces
                    
                    results.append(dict(texture=i,png=name,density=density,source_modes=modes,modes=expanded,sources=sources,bytes=full_size,split_tiles=len(splits.get(i,[]))))
        blobs.append((encoded,output_size))
    assert not (set(unmatched)&suspect_ids), ('suspect source PNG not matched',set(unmatched)&suspect_ids)
    if not a.suspects:assert not unmatched,unmatched
    pose_fmt=struct.Struct('<ffI4H')
    tile_count=struct.unpack_from('<I',current,48)[0]
    old_tiles=[pose_fmt.unpack_from(current,cf+len(ce)*12+n*20) for n in range(tile_count)]
    tiles=list(old_tiles);prefix=bytearray(current[:cf]);rewritten=0
    if splits:
        frame_count=struct.unpack_from('<I',current,8)[0];clip_count=struct.unpack_from('<H',current,24)[0]
        is_assist=current[:6]==b'BVNASS';bindings=22 if is_assist else 24
        frame_start=(52 if is_assist else 54)+bindings*18+clip_count*8
        assert frame_start+frame_count*48<=cf
        def expand(pose):
            x,y,index,w,h,s,r=pose
            if index not in splits:return [pose]
            out=[]
            for px,py,pw,ph,new_id,ns,nr in splits[index]:
                if px<w and py<h:out.append((x+px,y+py,new_id,min(pw,w-px),min(ph,h-py),ns,nr))
            assert sum(p[3]*p[4] for p in out)==w*h
            return out
        for frame in range(frame_count):
            offset=frame_start+frame*48;pose=pose_fmt.unpack_from(prefix,offset)
            if pose[5]:
                assert pose[2]<len(ce) and pose[3]<=pose[5] and pose[4]<=pose[6]
                if pose[2] not in splits:continue
                parts=expand(pose)
            else:
                assert pose[0]==pose[1]==0 and pose[2]+pose[3]<=len(old_tiles)
                span=old_tiles[pose[2]:pose[2]+pose[3]]
                if not any(p[2] in splits for p in span):continue
                parts=[p for tile in span for p in expand(tile)]
            first=len(tiles);tiles.extend(parts);pose_fmt.pack_into(prefix,offset,0.,0.,first,len(parts),0,0,0);rewritten+=1
            assert prefix[offset+20:offset+48]==current[offset+20:offset+48]
        assert prefix[frame_start+frame_count*48:]==current[frame_start+frame_count*48:cf]
        assert prefix[54:frame_start]==current[54:frame_start]
    else:assert prefix==current[:cf]
    entries=[]
    for encoded,size in [*blobs,*appended]:
        assert size<=3*1024*1024
        entries.append((len(payload),len(encoded),size));payload.extend(encoded)
    assert len(payload)<=64*1024*1024
    if affected:
        start=(len(prefix)+len(entries)*12+len(tiles)*20+15)&~15
        struct.pack_into('<I',prefix,20,start);struct.pack_into('<2I',prefix,44,len(entries),len(tiles))
        prefix[:8]=b'BVNASSTX' if current[:6]==b'BVNASS' else b'BVNFGH16'
        metadata=prefix+b''.join(TEXTURE.pack(*entry) for entry in entries)+b''.join(pose_fmt.pack(*tile) for tile in tiles)
        metadata+=bytes(start-len(metadata))
        footer=b''.join(struct.pack('<IQ',*rec) for rec in sorted(affected))+struct.pack('<II',len(affected),0x33594c42)+current[-8:]
        output=bytes(metadata)+payload+footer
    else:output=current
    a.output.write_bytes(output)
    report=dict(bank=a.current.name,textures=len(ce),matched=len(matched),unmatched=unmatched,
        source_export=str(a.export),normal_portrait_or_existing_screen_textures=exempt,
        layered_textures=len(affected),gameplay_tables_unchanged=True,logical_coverage_preserved=True,split_source_textures=list(splits),rewritten_pose_frames=rewritten,decoded_entry_max_bytes=max(e[2] for e in entries),metadata_identity=struct.unpack('<Q',current[-8:])[0],
        payload_bytes=len(payload),bytes=len(output),sha256=digest(output),entries=results)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print('PASS',len(ce),'textures checked;',len(affected),'authored layered textures;',len(payload),'payload bytes',flush=True)
if __name__=='__main__':main()
