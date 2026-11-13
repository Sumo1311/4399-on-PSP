#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse,hashlib,json,struct
from pathlib import Path
from pack_actions import strike_key
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def fnv(raw):
    value=14695981039346656037
    for b in raw:value=((value^b)*1099511628211)&0xffffffffffffffff
    return value
def make(package,normal,support,layout,reference,output):
    native={};bank=None
    for line in layout.read_text().splitlines():
        kind,*fields=line.split('\t')
        if kind=='bank':
            name,assist,count=fields;bank={'assister':bool(int(assist)),'count':int(count),'objects':[]};native[name]=bank
        else:bank['objects'].append(dict(zip(['index','clip','initial','key'],map(int,fields))))
    reports={};origins={}
    for root in [normal,support]:
        for row in json.loads((root/'verification.json').read_text())['banks']:
            reports[row['bank']]=row;origins[row['bank']]=root
    assert len(native)==len(reports)==131 and native.keys()==reports.keys()
    jobs=[];proof=[]
    for name,bank in sorted(native.items()):
        row=reports[name];root=origins[name];asset=package/name
        assert sha(asset)==row['output_sha256']
        metaPath=(root/'packs'/name).with_suffix('.json');meta=json.loads(metaPath.read_text());records=meta['attackers']
        assert len(records)+bank['assister']==bank['count']==len(bank['objects']),name
        raw=asset.read_bytes();start=struct.unpack_from('<I',raw,20)[0];assert start<len(raw)
        identity=fnv(raw[:start]);binding=[]
        if bank['assister']:
            rootObject=bank['objects'][0];assert rootObject['index']==0 and rootObject['clip']==0 and rootObject['initial']==1
            binding.append({'index':0,'frame':0,'name':'--root','initial':0})
        for definition,record in zip(bank['objects'][int(bank['assister']):],records):
            assert definition['key']==strike_key(record['instance']),name
            binding.append({'index':definition['index'],'frame':record['frame'],'name':record['instance'],'initial':definition['initial']})
        source=None;owner='-'
        if binding:
            if 'swf'in row:
                source=Path(row['swf']);assert sha(source)==row['swf_sha256']
                script=Path(row['timeline_script']);parts=script.parts;at=parts.index('scripts')
                owner='.'.join(parts[at+1:]).removesuffix('.as')
            else:
                assert row['source']['kind']=='swf';source=reference/'tag-assets/shared/assets/assets/packs'/row['source']['path']
            jobs.append('\t'.join(['bank',name,str(source.resolve()),owner,str(int(bank['assister']))]))
            jobs.extend('\t'.join(['area',str(r['index']),str(r['frame']),r['name'],str(r['initial'])]) for r in binding)
        proof.append({'bank':name,'asset_sha256':sha(asset),'metadata_bytes':start,'metadata_fnv':f'{identity:016x}',
            'attacker_count':bank['count'],'assister':bank['assister'],'source':str(source) if source else None,
            'source_sha256':sha(source) if source else None,'source_owner_class':owner,'bindings':binding,
            'prior_metadata':str(metaPath),'prior_metadata_sha256':sha(metaPath)})
    output.write_text('\n'.join(jobs)+'\n');output.with_suffix('.json').write_text(json.dumps({'banks':proof,
        'layout_sha256':sha(layout),'scope':'All 131 actor banks match the source-qualified frame-effects reports byte-for-byte. Each native attacker index/key is matched to its original owner-frame instance; assists include the BaseGameSprite constructor root at its original first frame. Geometry extraction follows these exact bindings.'},indent=2)+'\n')
    print('Bound',len(proof),'banks and',sum(r['attacker_count']for r in proof),'original object areas')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('package',type=Path);p.add_argument('normal',type=Path);p.add_argument('support',type=Path);p.add_argument('layout',type=Path);p.add_argument('reference',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();make(a.package,a.normal,a.support,a.layout,a.reference,a.output)
