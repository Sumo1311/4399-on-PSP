#!/usr/bin/env python3
"""Generate immutable C++ EffectVO value records from the source-executed catalog."""
import argparse,json
from pathlib import Path

def generate(source,output):
    report=json.loads(source.read_text())
    rows=[]
    q=lambda s:json.dumps(s or '',ensure_ascii=True)
    b=lambda v:'true' if v else 'false'
    for e in sorted(report['effects'],key=lambda e:e['id']):
        shine=e['shine'] or {};shake=e['shake'] or {};slow=e['slowDown'] or {}
        tint=e['targetColorOffset'] or [0,0,0]
        assert len(tint)==3
        values=[q(e['id']),q(e['className']),q(e['sound']),q(e['specialEffectId']),
                'EffectBlend::'+e['blendMode'].capitalize(),str(e['freeze']),b(e['shine'] is not None),
                hex(shine.get('color',0xffffff)),repr(shine.get('alpha',.2)),
                repr(shake.get('x',0)),repr(shake.get('y',0)),repr(shake.get('pow',0)),str(shake.get('time',0)),
                repr(slow.get('rate',1.5)),str(slow.get('time',0)),
                *[b(e[k]) for k in ['randRotate','followDirect','isSpecial','isBuff','isSteelHit']],
                b(e['targetColorOffset'] is not None),'{'+','.join(map(str,tint))+'}']
        rows.append('    {'+', '.join(values)+'},')
    text='// Generated from the pinned AS3 EffectModel by export_effect_model.mjs.\n'
    text+='// SPDX-License-Identifier: GPL-3.0-or-later\n#pragma once\n#include "combat_effects.hpp"\n'
    text+='namespace bvn {\ninline constexpr CombatEffect CombatEffectData[]{\n'
    text+='\n'.join(rows)+'\n};\n} // namespace bvn\n'
    output.write_text(text)
    print(f'Generated {len(rows)} source effect definitions')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();generate(a.source,a.output)
