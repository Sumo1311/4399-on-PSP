from pathlib import Path
import subprocess,json,hashlib,importlib.util,datetime
R=Path(__file__).resolve().parents[1];T=R/'work-r24'
def mod(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
bench=mod('bench',R/'work-r4/analyze_bench_pair.py');source=mod('source',R/'work-r5/read_source_profile.py')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
assert json.loads((T/'RECORDED_MOTION_CHECK.json').read_text())['pass']
results={}
for label,candidate,name in [('R23','response55','response55-r23-r24baseline-333-normal'),('R24','response56','response56-r24-333-normal')]:
 for asset in ['source.swf','art.pak','audio.pak']:assert sha(R/'package/response55'/asset)==sha(R/'package'/candidate/asset)
 cmd=json.loads((R/'work-r15/benchmark-command.json').read_text());cmd=[x.replace('response46-r15-333-normal',name).replace('package/response46','package/'+candidate).replace('build-response46/','build-'+candidate+'/') for x in cmd]
 (T/(label.lower()+'-benchmark-command.json')).write_text(json.dumps(cmd,indent=2)+'\n');(T/'BENCH_PROGRESS.json').write_text(json.dumps(dict(active=label,run=name,started=datetime.datetime.now().isoformat()),indent=2)+'\n')
 with(T/(label.lower()+'-benchmark.log')).open('w') as log:subprocess.run(cmd,cwd=R,stdout=log,stderr=subprocess.STDOUT,check=True)
 run=R/'source-measure/runs'/name;assert json.loads((run/'benchmark-result.json').read_text())['status']=='pass';g=run/'MemoryStick/PSP/GAME/DadNMe'
 b=bench.analyze(g,sha(R/'package'/candidate/'EBOOT.PBP'),sha(R/'work-r4/device-benchmark-1200.dnm'));s=source.analyze(g/'SOURCE.BIN');assert b['complete'] and s['status']=='complete'
 results[label]=dict(run=name,benchmark=b,source=s,normal_profile=json.loads((run/'emulator-test-profile.json').read_text()))
 (T/(label.lower()+'-EMULATOR_BENCHMARK.json')).write_text(json.dumps(b,indent=2)+'\n');(T/(label.lower()+'-EMULATOR_SOURCE_PROFILE.json')).write_text(json.dumps(s,indent=2)+'\n');print(label+': normal333 benchmark pass',flush=True)
metrics={k:{p:d['source']['wall_us'][p+'_wall_us'] for p in ['logic','capture','post','total']} for k,d in results.items()};delta={p:(1-metrics['R24'][p]['mean']/metrics['R23'][p]['mean'])*100 for p in ['logic','capture','post','total']}
report=dict(status='complete-pair',scope='Fresh matched fixed1200 input,source240:1100,normal1x333MHz,model1,480x272; emulator diagnostic timing,not physical PSP or input-to-photon acceptance.',results=results,phase_us=metrics,reduction_percent=delta)
(T/'PERFORMANCE_COMPARISON.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(phase_us=metrics,reduction_percent=delta),indent=2))
