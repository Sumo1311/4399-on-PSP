#!/usr/bin/env python3
"""Add typed-event identities to unchanged source-bound object geometry."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

def sha(raw):return hashlib.sha256(raw).hexdigest()
def fnv(raw):
    value=14695981039346656037
    for byte in raw:value=((value^byte)*1099511628211)&0xffffffffffffffff
    return value

def bind(base_header,base_proof,old_package,typed_package,methods,output):
    original=base_header.read_bytes();text=original.decode()
    table=re.search(r'inline constexpr std::array<ObjectAreaBank,(\d+)> ObjectAreaBanks\{\{\n(.*?)\n\}\};',text,re.S)
    assert table
    records={int(key,16):(int(first),int(count)) for key,first,count in re.findall(r'\{0x([0-9a-f]+)ull,(\d+),(\d+)\}',table[2])}
    assert len(records)==int(table[1])==130
    existing=dict(records);rows=[];proof=json.loads(methods.read_text())
    assert proof['banks']==78 and proof['changes']==321
    for row in proof['files']:
        before=(old_package/row['bank']).read_bytes();after=(typed_package/row['bank']).read_bytes()
        assert sha(before)==row['before'] and sha(after)==row['after'] and len(before)==len(after)
        allowed={change['offset']:change['value'] for change in row['changes']}
        assert all(x==y or allowed.get(i)==y for i,(x,y) in enumerate(zip(before,after)))
        start=struct.unpack_from('<I',before,20)[0]
        assert start==struct.unpack_from('<I',after,20)[0] and before[start:]==after[start:]
        old,new=fnv(before[:start]),fnv(after[:start]);assert old in existing
        value=existing[old]
        if new in records:assert records[new]==value
        else:records[new]=value
        rows.append(dict(bank=row['bank'],before=f'{old:016x}',after=f'{new:016x}',first=value[0],count=value[1]))
    replacement=f'inline constexpr std::array<ObjectAreaBank,{len(records)}> ObjectAreaBanks{{{{\n'
    replacement+='\n'.join(f'    {{0x{key:016x}ull,{first},{count}}},' for key,(first,count) in sorted(records.items()))+'\n}};'
    result=text[:table.start()]+replacement+text[table.end():]
    # Only the signature lookup table changes; the entire geometry array stays exact.
    assert result[result.index('inline constexpr std::array<OpticalRect,'):]==text[text.index('inline constexpr std::array<OpticalRect,'):]
    output.write_text(result)
    report=dict(base_geometry_proof=json.loads(base_proof.read_text()),base_header_sha256=sha(original),
        header_sha256=sha(result.encode()),methods_sha256=sha(methods.read_bytes()),
        banks=78,retained_signatures=len(existing),signatures=len(records),new_signatures=len(records)-len(existing),bindings=rows,
        scope='The 78 actor metadata identities changed only through 321 original end-method command bytes. Each new identity aliases exactly the original first/count geometry span. All 130 earlier signatures and the complete ObjectAreas array remain unchanged, including empty areas. Native typed-bank acceptance must also pass hasObjectAreas(), the production load gate.')
    output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Bound',len(rows),'typed actors;',len(records),'signatures;',len(records)-len(existing),'new; geometry bytes unchanged')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['base_header','base_proof','old_package','typed_package','methods','output']:p.add_argument(name,type=Path)
    a=p.parse_args();bind(a.base_header,a.base_proof,a.old_package,a.typed_package,a.methods,a.output)
