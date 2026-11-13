#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse,hashlib,json
from pathlib import Path
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def generate(jobs,areas,header,fixture):
    source=json.loads(jobs.read_text());rects={}
    for line in areas.read_text().splitlines():
        bank,index,*values=line.split('\t');key=(bank,int(index));assert key not in rects
        rects[key]=tuple(map(float,values));assert len(rects[key])==4 and rects[key][2]>=0 and rects[key][3]>=0
    banks={};records=[];lines=[str(len(source['banks']))]
    for bank in source['banks']:
        name=bank['bank'];count=bank['attacker_count'];values=[rects[name,i] for i in range(count)];identity=int(bank['metadata_fnv'],16)
        assert all((name,i) in rects for i in range(count))
        if identity in banks:assert banks[identity]['areas']==values,'Different areas share a metadata signature'
        else:banks[identity]={'areas':values,'name':name}
        lines.append(f'{name} {identity:016x} {count}')
        lines.extend(' '.join(f'{v:.17g}'for v in rect) for rect in values)
    assert len(rects)==sum(b['attacker_count'] for b in source['banks'])==282
    out=['// Generated from source-bound BaseGameSprite constructor geometry.','// SPDX-License-Identifier: GPL-3.0-or-later','#pragma once',
         '#include "map_optical.hpp"','#include <array>','#include <cstdint>','namespace bvn {',
         'struct ObjectAreaBank {std::uint64_t signature;unsigned first,count;};']
    offsets=[]
    for identity,b in sorted(banks.items()):offsets.append((identity,len(records),len(b['areas'])));records.extend(b['areas'])
    out.append(f'inline constexpr std::array<ObjectAreaBank,{len(offsets)}> ObjectAreaBanks{{{{')
    out.extend(f'    {{0x{id:016x}ull,{first},{count}}},' for id,first,count in offsets);out.append('}};')
    out.append(f'inline constexpr std::array<OpticalRect,{len(records)}> ObjectAreas{{{{')
    out.extend('    {'+','.join(f'{v:.17g}' for v in r)+'},'for r in records);out.extend(['}};','} // namespace bvn'])
    header.write_text('\n'.join(out)+'\n');fixture.write_text('\n'.join(lines)+'\n')
    proof={'banks':len(source['banks']),'unique_metadata_signatures':len(banks),'source_object_areas':len(rects),'compiled_areas':len(records),
        'empty_areas':sum(not r[2] or not r[3] for r in rects.values()),
        'sha256':{str(p):sha(p) for p in [jobs,areas,header,fixture]},
        'scope':'Source constructor-time local geometry for all 282 attached-attacker/assist objects in 131 byte-bound actor banks. Metadata signatures bind the untouched native assets to generated double-precision rectangles; duplicate signatures require identical areas. BaseGameSprite optical areas translate their initial local rectangle without facing reflection. Actual engine area-call cadence and Flash getBounds raster/mask details remain separate verification.'}
    header.with_suffix('.json').write_text(json.dumps(proof,indent=2)+'\n');print('Generated object areas',proof['banks'],proof['source_object_areas'],'empty',proof['empty_areas'])
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('jobs',type=Path);p.add_argument('areas',type=Path);p.add_argument('header',type=Path);p.add_argument('fixture',type=Path)
    a=p.parse_args();generate(a.jobs,a.areas,a.header,a.fixture)
