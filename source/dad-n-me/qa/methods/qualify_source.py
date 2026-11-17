from pathlib import Path
import os, subprocess, json, hashlib, datetime, time

R = Path(__file__).resolve().parents[1]
T = R / 'work-r23'
out = T / 'source-differential'
out.mkdir(exist_ok=True)
cases = json.loads((T / 'CONTROL_CASES.json').read_text())['cases']
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
results = []

def replay(exe, fixture, tape, dest, interpreted=False):
    env = dict(os.environ, DNM_APPLY_PAUSE_GUARD='1')
    env.pop('DNM_DISABLE_ACCEL', None)
    if interpreted:
        env['DNM_DISABLE_ACCEL'] = '1'
    start = time.monotonic()
    with dest.open('w') as log, dest.with_suffix('.stderr').open('w') as err:
        subprocess.run([str(exe), str(fixture), str(tape)], env=env,
                       stdout=log, stderr=err, check=True)
    return time.monotonic() - start

def compare(label, a, b, frames, **extra):
    aa, bb = a.read_bytes(), b.read_bytes()
    ar, br = aa.splitlines(), bb.splitlines()
    assert len(ar) == len(br) == frames + 1
    first = next((n for n, (x, y) in enumerate(zip(ar, br)) if x != y), None)
    result = dict(label=label, byte_exact=aa == bb, rows=len(ar),
                  first_difference_tick=first, candidate_sha256=sha(a),
                  interpreted_sha256=sha(b), **extra)
    results.append(result)
    report = dict(status='running' if all(r['byte_exact'] for r in results) else 'fail',
                  candidate='response55 R23', results=results,
                  scope='Host whole displayed-tree state and RNG vs original AVM1 interpreter; accepted visual policy and intro guard in both; not physical FPS or complete independent Flash verification')
    (T / 'SOURCE_DIFFERENTIAL.json').write_text(json.dumps(report, indent=2)+'\n')
    print(label + ': ' + ('byte-exact' if aa == bb else 'DIFFERENCE '+str(first)), flush=True)
    assert aa == bb, result

# Retest the exact failure first, then the remaining combat and control routes.
for case in sorted(cases, key=lambda c: c['label'] != 'legacy-group-birth'):
    tape = R / case['path']
    assert sha(tape) == case['sha256']
    label = case['label']
    (T / 'QA_PROGRESS.json').write_text(json.dumps(dict(status='running', active=label,
        started=datetime.datetime.now().isoformat()), indent=2)+'\n')
    reference = R / 'work-r21/control-differential' / (label+'-interpreted.csv')
    if not reference.exists():
        reference = out / (label+'-interpreted.csv')
        replay(R/'work-r20/audit/dnm_state_audit', R/case['fixture'], tape, reference, True)
    dest = out / (label+'-r23.csv')
    elapsed = replay(T/'audit/dnm_state_audit', R/case['fixture'], tape, dest)
    compare(label, dest, reference, case['source_frames'],
            input_sha256=sha(tape), candidate_elapsed_seconds=elapsed)

tape = R/'source-measure/tests/inputs/campaign.dnm'
reference = out/'canonical-interpreted.csv'
replay(R/'work-r20/audit/dnm_state_audit', R/'work-r7/legacy-source.swf', tape, reference, True)
dest = out/'canonical-r23.csv'
replay(T/'audit/dnm_state_audit', R/'work-r7/legacy-source.swf', tape, dest)
compare('canonical41019', dest, reference, 41019,
        old_qualified_R8_hash=sha(R/'work-r8/current-fixed-legacy41019.csv'),
        matches_old_qualified_R8=dest.read_bytes()==(R/'work-r8/current-fixed-legacy41019.csv').read_bytes())
report = json.loads((T/'SOURCE_DIFFERENTIAL.json').read_text())
report.update(status='pass', completed_cases=len(results),
              source_frames=sum(r['rows']-1 for r in results),
              finished=datetime.datetime.now().isoformat())
(T/'SOURCE_DIFFERENTIAL.json').write_text(json.dumps(report, indent=2)+'\n')
(T/'QA_PROGRESS.json').write_text(json.dumps(dict(status='pass', active='host-source-differential',
        finished=datetime.datetime.now().isoformat()), indent=2)+'\n')
print('R23 complete source differential passed', flush=True)
