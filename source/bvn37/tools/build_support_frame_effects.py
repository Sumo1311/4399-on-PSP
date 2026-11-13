#!/usr/bin/env python3
"""Freshly rebuild assist and Musou actor frame effects from their original sources."""
import argparse,hashlib,json,os,subprocess,traceback
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor,as_completed
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--workspace',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
 p.add_argument('--jobs',type=int,default=3);a=p.parse_args();w=a.workspace.resolve();out=a.out.resolve();out.mkdir();(out/'packs').mkdir();(out/'classes').mkdir()
 tools=Path(__file__).resolve().parent;ref=w.parent.parent/'reference';jpexs=w.parent.parent.parent/'bleach_vs_naruto_2010_psp/work/tools/ffdec';java=Path('/opt/homebrew/opt/openjdk@17/bin/java')
 cp=os.pathsep.join(map(str,[jpexs/'ffdec.jar',jpexs/'lib/*']))
 subprocess.run([str(java.with_name('javac')),'-encoding','UTF-8','-cp',cp,'-d',str(out/'classes'),str(tools/'FlashLayers.java'),str(tools/'ExtractFighter.java')],check=True)
 normal={r['bank'] for r in json.loads((w/'frame-effects-fresh-a/verification.json').read_text())['banks']};catalog=json.loads((ref/'source_catalog.json').read_text())
 jobs=[('assist',*line.split()[:2])for line in (tools/'versus_assists.txt').read_text().splitlines()]
 jobs += [('fighter',name,Path(file).stem)for name,file,hp in [s.split()for s in (w/'cpu-ghost-dev-package-b/MUSOUACT.TXT').read_text().splitlines()] if file not in normal]
 def build(row):
  kind,name,code=row;bank=code+('.BVS'if kind=='assist'else'.BVF');source=catalog['assists'if kind=='assist'else'fighters'][name]['source']
  if source['kind']=='xfl':
   from build_musou_xfl import build as build_xfl
   build_xfl(ref,name,out/'packs'/bank,out/'actors'/name)
  else:
   command=['python3',str(tools/('build_assist.py'if kind=='assist'else'build_actions.py')),name,str(out/'packs'/bank),
    '--reference',str(ref),'--jpexs',str(jpexs),'--java',str(java),'--classes',str(out/'classes'),'--scratch',str(out/('assisters'if kind=='assist'else'actors'))]
   if kind=='fighter':command+=['--all-actions']
   with (out/(code+'.log')).open('w')as log:subprocess.run(command,check=True,stdout=log,stderr=subprocess.STDOUT)
  meta=json.loads((out/'packs'/bank).with_suffix('.json').read_text())
  return {'bank':bank,'name':name,'source':source,'output_sha256':sha(out/'packs'/bank),'deferred_effects':meta['deferred_effects']}
 rows=[];errors=[]
 with ThreadPoolExecutor(max_workers=a.jobs)as pool:
  futures={pool.submit(build,row):row for row in jobs}
  for future in as_completed(futures):
   try:r=future.result();rows.append(r);print('Built',r['bank'],r['deferred_effects'],flush=True)
   except Exception:
    row=futures[future];path=out/(row[2]+'.failure.log');path.write_text(traceback.format_exc());errors.append(str(path));print('FAILED',row,str(path),flush=True)
 rows.sort(key=lambda r:r['bank']);(out/'verification.json').write_text(json.dumps({'scope':'Complete original support-actor exports with frame-effect programs','banks':rows,'errors':errors},indent=2)+'\n')
 if errors:raise SystemExit(1)
if __name__=='__main__':main()
