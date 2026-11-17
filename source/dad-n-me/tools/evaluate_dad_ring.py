"""Validate lossless native presentation records, separately from source tracing."""
import argparse,csv,hashlib,json,statistics
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('run',type=Path)
p.add_argument('--begin',type=int,default=1)
p.add_argument('--end',type=int,required=True,help='Exclusive source-tick end')
p.add_argument('--loading-ticks',default='')
p.add_argument('--source-reference',type=Path)
a=p.parse_args()
i=a.run/'MemoryStick/PSP/GAME/DadNMe'
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
capture=json.loads((a.run/'presentation-ring.json').read_text())
assert capture['descriptor_ready'] and not any(capture[k] for k in ['read_failures','overruns','discarded_records']),capture
rows=[{k:int(v) for k,v in row.items()} for row in csv.DictReader((a.run/'presentation-ring.csv').open())]
assert rows and len(rows)==capture['records']==capture['last_presentation']
for index,row in enumerate(rows):
    assert row['presentation']==index+1 and row['subframe']==index%2,row
    assert row['source_tick']>0 and row['time_us']>0,row
    assert row['fresh'] in [0,1] and row['has_motion'] in [0,1]
    if row['subframe'] or not row['fresh']:assert not row['has_motion'],row
    if index:
        before=rows[index-1]
        assert row['time_us']>before['time_us'] and row['vcount']>before['vcount']
        advance=0 if row['subframe'] or not row['fresh'] else 1
        assert row['source_tick']==before['source_tick']+advance,(before,row)
        if row['subframe']:assert row['fresh']==before['fresh']
    else:assert row['source_tick']==1 and row['fresh']==1
loads=[{k:int(v) for k,v in row.items()} for row in csv.DictReader((a.run/'presentation-loading.csv').open())]
for row in loads:
    assert row['visible']==1 and row['requested_us']<=row['shown_us']<=row['init_begin_us']<=row['ready_us']<=row['first_submit_us']<=row['resumed_us']
expected=[int(x) for x in a.loading_ticks.split(',') if x]
assert [x['source_tick'] for x in loads]==expected,(loads,expected)
selected=[row for row in rows if a.begin<=row['source_tick']<a.end]
assert selected and selected[0]['source_tick']==a.begin and selected[-1]['source_tick']==a.end-1
assert selected[0]['subframe']==0 and selected[-1]['subframe']==1,'Incomplete endpoint pair'
# The first resumed game presentation replaces the card. Its next interval is
# checked as gameplay, including the second half of that first resumed pair.
loading_interval=lambda left,right:any(left['time_us']<x['first_submit_us'] and right['time_us']>=x['shown_us'] for x in loads)
intervals=[(left,right) for left,right in zip(selected,selected[1:]) if not loading_interval(left,right)]
dt=[right['time_us']-left['time_us'] for left,right in intervals]
quantile=lambda f:sorted(dt)[min(len(dt)-1,int(len(dt)*f))]
held={}
for left,right in intervals:
    if right['subframe']==0 and not right['fresh']:held[str(right['source_tick'])]=held.get(str(right['source_tick']),0)+1
missed=sum(right['vcount']-left['vcount']>1 for left,right in intervals)
source={'status':'not-collected','scope':'Pacing is separate from source/visual/audio correctness'}
if (i/'ENGINE.CSV').exists():
    assert a.source_reference,'Source trace needs a declared comparison reference'
    actual=(i/'ENGINE.CSV').read_bytes();expected_bytes=b''.join(a.source_reference.read_bytes().splitlines(keepends=True)[:len(actual.splitlines())])
    assert actual==expected_bytes,'Source trace differs'
    source={'status':'pass','rows':len(actual.splitlines())-1,'byte_exact_reference':True,'sha256':sha(i/'ENGINE.CSV')}
diagnostic=any((i/name).exists() for name in ['TRACE.DNM','PERF.QA','LOAD_CAPTURE.QA','CAPTURE60.QA','FRAMES.QA','LOG.QA','RESPONSE.QA'])
report={'status':'diagnostic-not-fps-qualification' if diagnostic else 'performance-only-pacing-pass' if not missed and not held else 'not-qualified',
        'scope':'PSP-2000 default-clock PPSSPP; source30Hz/interpolated59.94Hz. Native fixed RAM records drained read-only on host. Separate source/render/audio and physical-device gates.',
        'source_span_exclusive':[a.begin,a.end],'complete_native_records':len(rows),'capture':capture,
        'presentations_measured':len(selected),'presentation_hz':len(dt)*1e6/sum(dt),
        'fresh_transform_hz':sum(right['fresh'] for left,right in intervals)*1e6/sum(dt),
        'interval_us':{'mean':statistics.mean(dt),'p95':quantile(.95),'p99':quantile(.99),'maximum':max(dt)},
        'missed_display_intervals':missed,'repeated_pairs':sum(held.values()),'held_source_ticks':held,
        'loading_intervals_excluded':len(selected)-1-len(intervals),'loading_exclusion_end':'first resumed game presentation',
        'loads':loads,'source_parity':source,'diagnostic':diagnostic,
        'target_loading_log_absent':not (i/'LOADING.CSV').exists(),
        'eboot_sha256':sha(i/'EBOOT.PBP'),'presentation_sha256':sha(a.run/'presentation-ring.csv')}
(a.run/'ring-qualification.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
