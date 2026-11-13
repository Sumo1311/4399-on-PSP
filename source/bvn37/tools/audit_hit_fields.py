#!/usr/bin/env python3
"""Inventory literal hit fields and buff calls in every included actor source."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(args):
    w=args.workspace.resolve(); reference=args.reference.resolve()
    catalog=json.loads((reference/'source_catalog.json').read_text())
    profiles=json.loads((w/'combat-musou-defense-profiles.json').read_text())['banks']
    sources=set(); banks=[]
    for row in profiles:
        script=(w/'combat-musou-qualified-source'/row['script']).resolve()
        if sha(script)!=row['script_sha256'] or sha(args.package/row['bank'])!=row['bank_sha256']:
            raise ValueError(f"Source or bank identity changed: {row['bank']}")
        if script.suffix=='.as':
            root=script.parent
            paths=set(root.rglob('*.as'))
        else:
            document=reference/'flash-source'/catalog['fighters'][row['variant']]['source']['path']
            root=document.parent
            paths=set((root/'LIBRARY').rglob('*.xml'))|{document}
        if not paths:raise ValueError(f"Empty source set: {root}")
        sources.update(paths)
        banks.append({'bank':row['bank'],'root':str(root),'source_files':len(paths)})
    support=json.loads((w/'frame-effects-support-a/verification.json').read_text())['banks']
    for row in support:
        if not row['bank'].endswith('.BVS'):continue
        if sha(args.package/row['bank'])!=row['output_sha256']:
            raise ValueError(f"Support bank identity changed: {row['bank']}")
        root=w/'frame-effects-support-a/assisters'/row['name']/'scripts'
        paths=set(root.rglob('*.as'))
        if not paths:raise ValueError(f"Missing assist scripts: {root}")
        sources.update(paths)
        banks.append({'bank':row['bank'],'root':str(root),'source_files':len(paths)})
    expected={p.name for p in args.package.iterdir() if p.suffix in ('.BVF','.BVS')}-{'COMBAT.BVF','REACTION.BVF'}
    if {row['bank'] for row in banks}!=expected:raise ValueError('Source scan does not cover every included actor bank')
    fields={name:[] for name in ['focusTarget','customEffect','customEffectVO']}
    buffs=[]
    call=re.compile(r'getBuffCtrl\s*\(\s*\)\s*\.\s*(speedUp|speedDown|attackUp|attackDown|defenseUp|defenseDown)\s*\(([^)]*)\)')
    for path in sorted(sources):
        text=path.read_text()
        for name,hits in fields.items():
            for match in re.finditer(r'\b'+name+r'\b',text):
                hits.append({'source':str(path),'line':text.count('\n',0,match.start())+1})
        for match in call.finditer(text):
            buffs.append({'source':str(path),'line':text.count('\n',0,match.start())+1,
                'method':match[1],'arguments':match[2]})
    result={'actor_banks':len(banks),'banks':banks,'unique_source_files':len(sources),'literal_fields':fields,
        'buff_calls':buffs,'source_files':{str(p):sha(p) for p in sorted(sources)},
        'scope':'Literal identifier and buff-call inventory over complete cached original SWF script exports and XFL libraries for every included actor bank. Normal initializer and packed-bank identities are checked. Not a Flash VM, formal reflection analysis, or support-SWF re-export.'}
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(f"Audited {len(banks)} actor banks and {len(sources)} source files; literal hit fields { {k:len(v) for k,v in fields.items()} }; buff calls {len(buffs)}")


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ['workspace','reference','package','output']:p.add_argument(n,type=Path)
    build(p.parse_args())
