from pathlib import Path
import json,subprocess,hashlib,csv,datetime
R=Path(__file__).resolve().parents[1];T=R/'work-r24';RUNS=R/'source-measure/runs'
def read(p):return json.loads(p.read_text())
def save(n,d):(T/n).write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def game(n):return RUNS/n/'MemoryStick/PSP/GAME/DadNMe'
common=['--package','package/response56','--map','build-response56/dad_n_me.map','--app','/Users/sumo/Documents/flash-to-psp/performance_60fps_20261112/dad_n_me_psp/work/batch-baseline03-prefix5000-20261114/Dad n Me QA.app','--wall-sample-ms','2','--wall-snapshot-every','8','--presentation-ring']
def run(label,args,result):
 name='response56-r24-'+label;c=['python3','source-measure/tools/run_ppsspp.py','source-measure/runs/'+name]+common+args
 save(label+'-command.json',c)
 save('QA_PROGRESS.json',dict(active=label,started=datetime.datetime.now().isoformat(),candidate='response56'))
 if not (RUNS/name).exists():
  with (T/(label+'-run.log')).open('w') as out:subprocess.run(c,cwd=R,stdout=out,stderr=subprocess.STDOUT,check=True)
 else:
  installed=game(name)/'EBOOT.PBP';assert sha(installed)==sha(R/'package/response56/EBOOT.PBP')
 d=read(RUNS/name/result);assert d['status'] in ['pass','measurement_complete'],(label,d.get('status'))
 print(label+': runner pass',flush=True);return name
legacy=run('legacy-full-diagnostic',['--fixture','work-r7/legacy-source.swf','--replay','source-measure/tests/inputs/campaign.dnm','--trace-frames','41019','--capture-ticks','12951,1408,14944,15547,220,300,379,380,381,382,383,384,385,389,39775,399,40383,40500,4425,4523,600,8611,9746','--diagnostic-fast-until','41000'],'replay-result.json')
g=game(legacy);gold=read(R/'work-r8/FINAL_LEGACY_ROUTE_CHECK.json');assert sha(g/'ENGINE.CSV')==gold['csv_sha256']
images=[]
for old in gold['images']:
 f=g/old['file'];assert sha(f)==old['sha256'],f
 images.append(dict(old,byte_exact=True))
rows=list(csv.DictReader((g/'ENGINE.CSV').open()));assert len(rows)==41019
save('FINAL_LEGACY_ROUTE_CHECK.json',dict(gold,candidate='response56',run=legacy,images=images))
health=run('health-333-normal',['--replay','work-r7/health-and-restart-5000.dnm','--trace-frames','5000','--scenario','gameover','--driver','source-measure/tools/trace_completion_driver.mjs'],'replay-result.json')
g=game(health);gold=read(R/'work-r8/NATIVE_HEALTH_CHECK.json');assert sha(g/'ENGINE.CSV')==gold['csv_sha256']
save('NATIVE_HEALTH_CHECK.json',dict(gold,candidate='response56',run=health,scope='R24 native MIPS,333MHz,normal1x,5000 finite100HP source frames; exact R7/R8 health,gameover,restart CSV; no fixture override'))
controls=run('controls-333-normal',['--driver','source-measure/tools/control_watch_driver.mjs','--response-metrics'],'response-controls.json')
g=game(controls)
for n in ['CONTROL00.TXT','CONTROL00.CSV']:assert (g/n).is_file() and (g/n).stat().st_size>0
save('FINAL_CONTROLS.json',dict(status='pass',candidate='response56',run=controls,manual_snapshot_files=['CONTROL00.TXT','CONTROL00.CSV'],scope='Original package,normal1x333MHz,input events only; movement,attack,pause/resume,manual control snapshot; no state writes'))
pair=run('pair-333-normal',['--replay','work-r4/device-benchmark-1200.dnm','--stop-at','340','--capture-pair-tick','300'],'prefix-result.json')
g=game(pair);gold=read(R/'work-r8/INTERPOLATION_PIXEL_CHECK.json')
for old in gold['images']:assert sha(g/old['file'])==old['sha256'],old['file']
save('INTERPOLATION_PIXEL_CHECK.json',dict(gold,candidate='response56',run=pair))
interruption=run('interrupt-diagnostic',['--fixture','work-r8/intro-interruption-qa.swf','--replay','work-r4/device-benchmark-1200.dnm','--trace-frames','1200','--stop-at','1200','--diagnostic-fast-until','1100'],'prefix-result.json')
g=game(interruption);base=game('response39-interrupt-r8-diagnostic');assert sha(g/'ENGINE.CSV')==sha(base/'ENGINE.CSV')
gold=read(R/'work-r8/NATIVE_INTRO_INTERRUPTION_CHECK.json')
save('NATIVE_INTRO_INTERRUPTION_CHECK.json',dict(status='pass',candidate='response56',run=interruption,scope=gold['scope'],fixture=gold['fixture'],result=gold['results']['r8'],csv_byte_exact_to_R8=True,csv_sha256=sha(g/'ENGINE.CSV')))
save('QA_PROGRESS.json',dict(status='pass',candidate='response56',finished=datetime.datetime.now().isoformat(),checks=['41019 native source frames+23 pixels','5000 normal100HP health/gameover/restart','normal controls+snapshot','midpoint/exact pixels','interrupted-intro guard']))
print('all R24 native qualification checks pass',flush=True)
