"""Report the matched visual repair cost without borrowing an optimization gate."""
from pathlib import Path
import configparser
import csv
import datetime
import hashlib
import json
import statistics

R = Path(__file__).resolve().parent
read = lambda p: json.loads(p.read_text())
rows = lambda p: [{k: int(v) for k, v in x.items()} for x in csv.DictReader(p.open())]

def stats(values):
    values = sorted(values)
    return dict(count=len(values), mean=statistics.mean(values),
                p95=values[min(len(values)-1, int(.95*len(values)))],
                p99=values[min(len(values)-1, int(.99*len(values)))], maximum=max(values))

results = {}
snapshots = {}
for job in read(R/'B155_TIMING_PLAN.json')['jobs']:
    label = job['label']
    run = R/'runs'/f'logic-{label}-timing-normal'
    live = run/'MemoryStick/PSP/GAME/ScriptProbe'
    final = read(run/'result.json')
    process = read(R/'checks'/(label+'-timing-processes.json'))
    assert final['error'] == 0 and final['frames'] == 2600
    assert final['diagnostic_fast_until'] == 0 and not final['timing_overflow']
    assert final['pbp_sha256'] == job['eboot_sha256']
    assert final['emulation_profile'] == 'psp2000'
    assert process['performance_qualification_eligible']
    assert process['continuous_idle_preflight_seconds'] == 30
    preflight = process['preflight_samples']
    idle_wall = (datetime.datetime.fromisoformat(preflight[-1]['utc'])-
                 datetime.datetime.fromisoformat(preflight[0]['utc'])).total_seconds()
    assert idle_wall >= 30 and all(not x['processes'] for x in preflight)
    assert not process['foreign_processes_observed']
    cfg = configparser.ConfigParser(); cfg.read(run/'ppsspp.ini')
    for section, key, value in [('CPU','CPUSpeed','333'),('CPU','IOTimingMethod','2'),
                                ('SystemParam','PSPModel','1'),('Graphics','FrameSkip','0'),
                                ('Graphics','InternalResolution','1'),('Sound','Enable','True')]:
        assert cfg[section][key] == value
    cpu = rows(live/'SOURCE_CPU.CSV')
    screen = rows(live/'PRESENT60.CSV')
    loads = rows(live/'LOADING.CSV')
    assert len(cpu) == 2600 and all(v['source_tick'] == i for i, v in enumerate(cpu, 1))
    windows = {}
    for a, b in [(1200,2600), (1330,1700)]:
        selected = [v for v in cpu if a <= v['source_tick'] < b]
        loaded = lambda t: any(x['shown_us'] <= t <= x['resumed_us'] for x in loads)
        draws = [v for v in screen if a <= v['source_tick'] < b and not loaded(v['time_us'])]
        windows[f'{a}-{b}'] = dict(
            complete_ms=stats([v['complete_us']/1000 for v in selected]),
            logic_ms=stats([v['logic_us']/1000 for v in selected]),
            over_33_333ms=sum(v['complete_us'] > 33333 for v in selected),
            source_hz=(b-a)/((cpu[b-1]['sample_us']-selected[0]['sample_us'])/1e6),
            marked_fresh_or_interpolated_hz=sum(v['fresh'] for v in draws)/((draws[-1]['time_us']-draws[0]['time_us'])/1e6))
    assert not (live/'SCRIPT_ERRORS.JSONL').read_bytes()
    snapshots[job['candidate']] = [json.loads(s) for s in (live/'FRONTEND.JSONL').open()]
    results[job['candidate']] = dict(windows=windows, heap_peak=final['heap_peak'],
        config_sha256=hashlib.sha256((run/'ppsspp.ini').read_bytes()).hexdigest(),
        eboot_sha256=final['pbp_sha256'], emulator_payload=final['emulator_payload'],
        source_backend=read(live/'SOURCE_BACKEND.JSON'), native_result=str(run/'result.json'),
        idle_preflight_wall_seconds=idle_wall,
        process_receipt=str(R/'checks'/(label+'-timing-processes.json')))

assert snapshots['B154'] == snapshots['B155']
assert results['B154']['config_sha256'] == results['B155']['config_sha256']
assert results['B154']['emulator_payload'] == results['B155']['emulator_payload']
report = dict(status='completed-matched-repair-cost-measurement', results=results,
    full_timed_trace_exact=True, concurrent_load_observed=False,
    scope='Same1025inputs/2600source steps,30Hz source,fixedPSP2000/333MHz/64MiB/480x272/audio/normal/no skips/realisticIO. Repaired visible work intentionally differs. No new performance gain gate;report actual cost. Marked fresh/interpolated output is distinct from complete steps and physical display.',
    stable60=False, physical_psp_tested=False)
(R/'B155_TIMING_COMPARISON.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v['windows']['1330-1700'] for k,v in results.items()},indent=2))
