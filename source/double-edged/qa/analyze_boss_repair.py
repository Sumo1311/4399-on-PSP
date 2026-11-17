from pathlib import Path
import hashlib
import json
import re
from PIL import Image

R=Path(__file__).resolve().parent
run=R/'runs/b155-boss7250-native';p=run/'MemoryStick/PSP/GAME/ScriptProbe'
final=json.loads((run/'result.json').read_text())
assert final['error']==0 and final['frames']==7250
rows=[json.loads(s) for s in (p/'FRONTEND.JSONL').open()]
host=[json.loads(s) for s in (R/'host/ordinary-7250/NATIVE.JSONL').open()]
assert len(rows)==len(host)==7250
uid_differences=0
for i,(ns,hs) in enumerate(zip(rows,host),1):
    n=dict(ns);h=dict(hs);nw=n.pop('qa_visibility');hw=h.pop('qa_visibility');assert n==h,i
    for name in ['actors','coins']:
        assert len(nw[name])==len(hw[name])
        for xx,yy in zip(nw[name],hw[name]):
            x=dict(xx);y=dict(yy);uid_differences+=x.pop('uid')!=y.pop('uid');assert x==y,i
checks=re.findall(r'MIRROR_CHECK (\d+) (PASS|FAIL)',(p/'GRAPHICS.LOG').read_text())
assert len(checks)==7250 and all(s=='PASS' for _,s in checks)
errors=[json.loads(s) for s in (p/'SCRIPT_ERRORS.JSONL').open()]
gold=[json.loads(s) for s in (R.parent/'double_edged_campaign_native_20261116/host/visual-prefix-02/SCRIPT_ERRORS.JSONL').open()]
gold=[x for x in gold if x['tick']<=7250]
assert errors==gold and len(errors)==7
assert (p/'SCRIPT_ERRORS.JSONL').read_bytes()==(R/'host/ordinary-7250/SCRIPT_ERRORS.JSONL').read_bytes()
dying=[dict(tick=j['tick'],**a) for j in rows for a in j['qa_visibility']['actors']
       if a.get('symbol')==1094 and a.get('health')==0 and a.get('alive') is False and a.get('landed') is False]
assert dying and any(v['tick']==7180 for v in dying)
Image.frombytes('RGBA',(480,272),(p/'MID60.RGBA').read_bytes()).save(R/'inspection/b155-BOSS.png')
report=dict(status='passed-native-original-input-Brute-death-prefix',source_steps=7250,
    source_gameplay_rng_audio_exact=True,all_non_uid_visibility_fields_exact=True,
    ignored_readonly_display_allocation_uid_samples=uid_differences,
    mirror_reference_command_passes=len(checks),original_caught_errors_preserved_exact=errors,
    first_dying=dying[:12],visual_policy=json.loads((p/'VISUAL_POLICY_STATS.JSON').read_text()),
    native_result=str(run/'result.json'),screenshot='inspection/b155-BOSS.png',capture_tick=7180,
    framebuffer_sha256=hashlib.sha256((p/'MID60.RGBA').read_bytes()).hexdigest(),
    timing_capacity_overflow=final['timing_overflow'],
    scope='Actual original-qualified inputs through first Brute death;complete native source trace and7250mirror checks. Intrusive functional diagnostic,not FPS or full-campaign qualification. Seven previously qualified caught source errors preserved including tick/message/stack. Special Boss boundaries separately covered by actual QuickJS helper cases.',
    physical_psp_tested=False)
(R/'B155_NATIVE_BOSS.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:report[k] for k in ['status','source_steps','mirror_reference_command_passes','visual_policy']},indent=2))
