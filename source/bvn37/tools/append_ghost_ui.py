#!/usr/bin/env python3
"""Append source SL/WL training glyphs while preserving the accepted UI records and pixels."""
from pathlib import Path
import argparse,hashlib,json,tempfile
from append_training_highlight import read,HEADER,RECORD
from pack_versus_ui import render_label,pack_images
def build(base,output,font):
 records,payload=read(base)
 with tempfile.TemporaryDirectory() as tmp:
  probe=Path(tmp)/'probe.bvu';pack_images({'log-6':render_label('L',font,29)},probe)
  rows,pixels=read(probe);original=next(r for r in records if r[0].rstrip(b'\0')==b'log-6')
  assert original[2:]==rows[0][2:] and payload[original[1]:original[1]+original[4]*original[5]*2]==pixels
  extra=Path(tmp)/'ghost.bvu';pack_images({key:render_label(text,font,29) for key,text in
   [('log-ghost-step','↓＋L'),('log-ghost-jump','↑＋L')]},extra)
  rows,pixels=read(extra);assert not {r[0] for r in rows}&{r[0] for r in records}
  merged=sorted([*records,*[(r[0],r[1]+len(payload),*r[2:]) for r in rows]])
  table=b''.join(RECORD.pack(*r) for r in merged);start=(HEADER.size+len(table)+15)//16*16
  output.write_bytes(HEADER.pack(b'BVNUI001',len(merged),start)+table+bytes(start-HEADER.size-len(table))+payload+pixels)
  final,decoded=read(output);assert [r for r in final if r[0] not in {v[0] for v in rows}]==records
  assert decoded[:len(payload)]==payload
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 report={'base_sha256':sha(base),'output_sha256':sha(output),'old_records_and_pixels_preserved':True,
  'font_verified_against_old_L':True,'glyphs':['log-ghost-step','log-ghost-jump'],'source_inputs':['SL','WL']}
 output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
 print('Appended SL/WL glyphs; old UI unchanged')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('base',type=Path);p.add_argument('output',type=Path);p.add_argument('--font',required=True)
 a=p.parse_args();build(a.base,a.output,a.font)
