from pathlib import Path
import json,subprocess,hashlib,datetime
R=Path(__file__).resolve().parents[1];T=R/'work-r24';name='response56-r24-ramonly6000-333-normal'
cmd=json.loads((R/'work-r17/dense-command.json').read_text());cmd=[x.replace('response49-dense6000-333-normal',name).replace('package/response49','package/response56').replace('build-response49/','build-response56/') for x in cmd]
for option in ['--perf-frames','--capture-ticks']:
 at=cmd.index(option);del cmd[at:at+2]
cmd[cmd.index('--driver')+1]='work-r20/ram_replay_driver.mjs'
assert '--diagnostic-fast-until' not in cmd and '--response-metrics' not in cmd and '--trace-frames' not in cmd
(T/'ram-baseline-command.json').write_text(json.dumps(cmd,indent=2)+'\n');(T/'WORK_STATE.json').write_text(json.dumps(dict(status='ordinary-R24-RAM-only-candidate-running',candidate='response56 stack snapshots',physical='R20 card previously written/remount-verified;no new physical sample;card offline',started=datetime.datetime.now().isoformat()),indent=2)+'\n')
with(T/'ram-baseline-run.log').open('w') as log:subprocess.run(cmd,cwd=R,stdout=log,stderr=subprocess.STDOUT,check=True)
run=R/'source-measure/runs'/name;assert json.loads((run/'ram-replay-result.json').read_text())['status']=='pass'
with(T/'ram-baseline-analysis.log').open('w') as log:subprocess.run(['python3','source-response49/tools/evaluate_dad_ring.py',str(run),'--begin','240','--end','5900'],cwd=R,stdout=log,stderr=subprocess.STDOUT,check=True)
d=json.loads((run/'ring-qualification.json').read_text());assert not d['diagnostic'];assert d['eboot_sha256']==hashlib.sha256((R/'package/response56/EBOOT.PBP').read_bytes()).hexdigest()
(T/'R24_RAM_ONLY_PACING.json').write_text(json.dumps(d,indent=2)+'\n');print(json.dumps({k:d[k] for k in ['status','presentation_hz','fresh_transform_hz','interval_us','missed_display_intervals','repeated_pairs','held_source_ticks','capture']},indent=2))
