#!/usr/bin/env python3
"""Freshly export complete original SWFs and compile frame effects with source provenance."""
import argparse,hashlib,json,os,subprocess,traceback
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor,as_completed

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--workspace',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
 p.add_argument('--bank');p.add_argument('--jobs',type=int,default=2);p.add_argument('--resume',action='store_true');a=p.parse_args()
 w=a.workspace.resolve();out=a.out.resolve();out.mkdir(exist_ok=a.resume);(out/'packs').mkdir(exist_ok=a.resume);(out/'classes').mkdir(exist_ok=a.resume)
 tools=Path(__file__).resolve().parent;ref=w.parent.parent/'reference';jpexs=w.parent.parent.parent/'bleach_vs_naruto_2010_psp/work/tools/ffdec'
 java=Path('/opt/homebrew/opt/openjdk@17/bin/java');classpath=os.pathsep.join(map(str,[jpexs/'ffdec.jar',jpexs/'lib/*']))
 subprocess.run([str(java.with_name('javac')),'-encoding','UTF-8','-cp',classpath,'-d',str(out/'classes'),str(tools/'FlashLayers.java'),str(tools/'ExtractFighter.java')],check=True)
 original={r['bank']:r for r in json.loads((w/'cpu-recovery-assets-a/verification.json').read_text())['banks']}
 tool_hashes={p.name:sha(p) for p in tools.glob('*.py')}
 tool_hashes.update({p.name:sha(p) for p in tools.glob('*.java')})
 def build(line):
  variant,code,family,*internal=line.split();bank=code+'.BVF'
  if a.bank and bank!=a.bank:return None
  proof=original[bank];script=w/proof['timeline_script'];assert sha(script)==proof['timeline_script_sha256'] and sha(Path(proof['swf']))==proof['swf_sha256']
  scripts=next(p for p in script.parents if p.name=='scripts');actual=family if internal else variant
  command=[str(java)]
  pack=out/'packs'/bank
  result=out/(code+'.proof.json')
  if a.resume and result.exists():
   row=json.loads(result.read_text());assert row['swf_sha256']==proof['swf_sha256'] and row['tools']==tool_hashes and sha(pack)==row['output_sha256'];return row
  command=['python3',str(tools/'build_actions.py'),actual,str(pack),'--reference',str(ref),'--jpexs',str(jpexs),'--java',str(java),
   '--classes',str(out/'classes'),'--scratch',str(out/'actors'),'--decompiled',str(scripts),'--all-actions']
  if internal:command+=['--internal-frame',internal[0]]
  with (out/(code+'.log')).open('w') as log:subprocess.run(command,check=True,stdout=log,stderr=subprocess.STDOUT)
  meta=json.loads(pack.with_suffix('.json').read_text())
  row={'bank':bank,'swf':proof['swf'],'swf_sha256':proof['swf_sha256'],'timeline_script':str(script),'timeline_script_sha256':sha(script),
   'output_sha256':sha(pack),'tools':tool_hashes,'deferred_effects':meta['deferred_effects'],'fresh_swf_raster_export':True}
  result.write_text(json.dumps(row,indent=2)+'\n');return row
 rows=[];errors=[]
 with ThreadPoolExecutor(max_workers=a.jobs) as pool:
  futures={pool.submit(build,line):line for line in (tools/'versus_fighters.txt').read_text().splitlines()}
  for future in as_completed(futures):
   try:row=future.result()
   except Exception:
    code=futures[future].split()[1];path=out/(code+'.failure.log');path.write_text(traceback.format_exc());errors.append(str(path));print('FAILED',code,str(path),flush=True);continue
   if row:rows.append(row);print('Built',row['bank'],row['deferred_effects'],flush=True)
 rows.sort(key=lambda r:r['bank']);(out/'verification.json').write_text(json.dumps({'scope':'Fresh complete original SWF exports and source frame-program rebuild','banks':rows,'errors':errors},indent=2)+'\n')
 if errors:raise SystemExit(1)
if __name__=='__main__':main()
