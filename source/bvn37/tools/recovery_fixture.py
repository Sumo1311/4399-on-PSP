#!/usr/bin/env python3
"""Derive target-loader pixel/body checks from separately compiled original recovery exports."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from append_recovery import Bank


def shape(pose):
    return struct.pack('<2f4H',pose[0],pose[1],*pose[3:7])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assets',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    verified=json.loads((args.assets/'verification.json').read_text())
    assert not verified['errors'] and len(verified['banks'])==78
    candidates={}
    for proof in args.assets.glob('*/verification.json'):
        row=json.loads(proof.read_text());candidates.setdefault(row['bank'],[]).append((row,proof.parent))
    lines=[];proofs=[];count=0
    for row in verified['banks']:
        matched=[folder for proof,folder in candidates[row['bank']]
                 if proof['addition_sha256']==row['addition_sha256']]
        auxiliary=next(folder/'recovery-only.BVF' for folder in matched
            if hashlib.sha256((folder/'recovery-only.BVF').read_bytes()).hexdigest()==row['addition_sha256'])
        bank=Bank.read(auxiliary);assert not bank.lz4
        clips=bank.tables['clips'][bank.escape:]
        assert clips[0][1]==row['recovery_frames']
        lines.append(f"{row['bank']} {len(clips)}")
        crcs=[]
        for index,(first,frames,repeat,timeline) in enumerate(clips,start=bank.escape):
          lines.append(f'{index} {timeline} {frames} {repeat}')
          for frame in bank.tables['frames'][first:first+frames]:
            crc=zlib.crc32(struct.pack('<4f',*frame[7:11]))
            crc=zlib.crc32(shape(frame[:7]),crc)
            poses=bank.tables['tiles'][frame[2]:frame[2]+frame[3]] if frame[5]==0 else [frame[:7]]
            for pose in poses:
                crc=zlib.crc32(shape(pose),crc)
                if not pose[5] or not pose[3] or not pose[4]:continue
                offset,packed,size=bank.tables['textures'][pose[2]]
                raw=zlib.decompress(bank.payload[offset:offset+packed]);assert len(raw)==size
                crc=zlib.crc32(raw,crc)
            lines.append(str(crc));crcs.append(crc);count+=1
        proofs.append({'bank':row['bank'],'clips':len(clips),'frames':len(crcs),'crc32':crcs,
                       'auxiliary':str(auxiliary),'auxiliary_sha256':row['addition_sha256'],
                       'merged_sha256':row['output_sha256']})
    assert count==866
    args.output.write_text('\n'.join(lines)+'\n')
    args.output.with_suffix('.json').write_text(json.dumps({'scope':'Original auxiliary export versus merged target loader; no gameplay control claim',
        'banks':proofs,'frames':count,'fixture_sha256':hashlib.sha256(args.output.read_bytes()).hexdigest()},indent=2)+'\n')
    print('Generated original export checks for 78 banks, 820 recovery and 46 attached object frames')


if __name__=='__main__':main()
