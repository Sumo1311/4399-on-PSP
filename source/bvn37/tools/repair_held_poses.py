"""Restore pinned transparent locomotion poses from corrected original SWF exports."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct
from PIL import Image
from pack_fighter import read_timeline
from promote_native_textures import load_lz4

SCALE=272/600
POSE=struct.Struct('<ffI4H')
TEXTURE=struct.Struct('<III')
def digest(data):return hashlib.sha256(data).hexdigest()
def power(n):return max(4,1<<(n-1).bit_length())

def repair(source,export,spec,output):
    data=source.read_bytes();assert digest(data)==spec['baseline_sha256']
    assert data[:8] in (b'BVNFGH15',b'BVNFGH16')
    _,frames,hits,events,pixel_start,clips,strikes,*_=struct.unpack_from('<8s4I2H3f',data)
    projectiles,attackers,count,tiles,_=struct.unpack_from('<HHIIH',data,40)
    clip_start=54+24*2+24*16;frame_start=clip_start+clips*8
    table_start=frame_start+frames*48+hits*20+strikes*24+events*12+projectiles*92+attackers*24
    tile_start=table_start+count*12;tile_end=tile_start+tiles*20
    assert pixel_start==(tile_end+15)&~15
    records=[TEXTURE.unpack_from(data,table_start+i*12) for i in range(count)]
    payload_end=pixel_start+sum(n for _,n,_ in records)
    footer=data[payload_end:];assert footer[-8:]==struct.pack('<Q',int(spec['metadata_identity']))
    if data[:8]==b'BVNFGH16':
        n,marker=struct.unpack_from('<II',footer,len(footer)-16)
        assert marker==0x33594c42 and len(footer)==n*12+16
    else:assert len(footer)==8
    prefix=bytearray(data[:table_start]);timeline=read_timeline(export)
    payload=bytearray(data[pixel_start:payload_end]);new_records=list(records)
    lib=load_lz4();dedup={};changes=[]
    for action in spec['clips']:
        clip=action['clip'];first,n,repeat,parent=struct.unpack_from('<4H',data,clip_start+clip*8)
        assert parent==action['timeline'] and n==action['count']
        if (parent,0) in timeline.children:assert timeline.cycles[parent]==(n,repeat)
        for phase in action['phases']:
            at=frame_start+(first+phase)*48
            old_pose=POSE.unpack_from(data,at);x,y,index,width,height,stride,rows=old_pose
            assert stride>0
            offset,packed,raw_size=records[index];raw=ctypes.create_string_buffer(raw_size)
            assert lib.LZ4_decompress_safe(data[pixel_start+offset:pixel_start+offset+packed],raw,packed,raw_size)==raw_size
            assert not any(raw.raw[3::4]),'Only replace a completely transparent original frame'
            held=(parent,phase) in timeline.children
            key=f'{parent}-{phase}' if held else str(parent+phase)
            pose=timeline.children[parent,phase] if held else timeline.poses[parent+phase]
            px,py,pw,ph=map(float,pose);png=(export/'poses'/(key+'.png')).read_bytes()
            with Image.open(export/'poses'/(key+'.png')) as im:
                assert im.size==(int(pw),int(ph));size=tuple(max(1,round(v*SCALE))for v in im.size)
                low=im.convert('RGBA').resize(size,Image.Resampling.NEAREST)
                bounds=low.getbbox(alpha_only=True);assert bounds is not None
                high=im.convert('RGBA').convert('RGBa').resize(tuple(v*2 for v in size),Image.Resampling.LANCZOS).convert('RGBA')
            width,height=bounds[2]-bounds[0],bounds[3]-bounds[1]
            stride,rows=power(width),power(height);assert max(stride,rows)<=256
            image=Image.new('RGBA',(stride*2,rows*2));image.paste(high.crop(tuple(v*2 for v in bounds)))
            encoded=image.tobytes();identity=digest(encoded)
            if identity not in dedup:
                packed=ctypes.create_string_buffer(lib.LZ4_compressBound(len(encoded)))
                length=lib.LZ4_compress_default(encoded,packed,len(encoded),len(packed));assert length>0
                check=ctypes.create_string_buffer(len(encoded))
                assert lib.LZ4_decompress_safe(packed.raw,check,length,len(encoded))==len(encoded) and check.raw==encoded
                dedup[identity]=len(new_records)
                new_records.append((len(payload),length,len(encoded)));payload.extend(packed.raw[:length])
            new_pose=(px*SCALE+bounds[0],py*SCALE+bounds[1],dedup[identity],width,height,stride,rows)
            POSE.pack_into(prefix,at,*new_pose)
            changes.append(dict(clip=clip,phase=phase,frame=first+phase,source_pose=key,
                source_png_sha256=digest(png),before_pose=old_pose,after_pose=new_pose,texture_sha256=identity))
    new_pixel_start=(table_start+len(new_records)*12+tiles*20+15)&~15
    struct.pack_into('<I',prefix,20,new_pixel_start);struct.pack_into('<I',prefix,44,len(new_records))
    header=prefix+b''.join(TEXTURE.pack(*r)for r in new_records)+data[tile_start:tile_end]
    header+=bytes(new_pixel_start-len(header));assert len(header)==new_pixel_start
    check=bytearray(prefix)
    for c in changes:
        at=frame_start+c['frame']*48;check[at:at+20]=data[at:at+20]
    check[20:24]=data[20:24];check[44:48]=data[44:48]
    assert check==data[:table_start], 'All gameplay metadata and retained frame poses must remain identical'
    assert payload[:payload_end-pixel_start]==data[pixel_start:payload_end]
    result=header+payload+footer;output.write_bytes(result)
    return dict(bank=source.name,before_sha256=digest(data),sha256=digest(result),
        restored_frames=len(changes),appended_textures=len(new_records)-count,changes=changes,
        all_original_payloads_and_visible_frames_retained=True,gameplay_tables_unchanged=True,
        original_metadata_identity_preserved=True,original_blend_footer_preserved=True)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ['input','exports','spec','output','report']:p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);spec=json.loads(a.spec.read_text())
    results=[]
    for b in spec['banks']:
        result=repair(a.input/b['bank'],a.exports/b['export'],b,a.output/b['bank']);results.append(result)
        print(b['bank'],result['restored_frames'],'frames',result['appended_textures'],'textures',flush=True)
    a.report.write_text(json.dumps(dict(banks=results),ensure_ascii=False,indent=2)+'\n')
if __name__=='__main__':main()
