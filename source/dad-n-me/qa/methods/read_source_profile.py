"""Read optional source phase/run-clock observations without assuming CPU fidelity."""
from pathlib import Path
import argparse
import hashlib
import json
import struct

FIELDS='tick logic_wall_us capture_wall_us post_wall_us total_wall_us logic_run_us capture_run_us post_run_us total_run_us commands coordinates'.split()


def analyze(path,begin=240,end=1100):
    data=path.read_bytes();header=struct.unpack_from('<8I',data)
    assert header[:3]==(0x31435253,1,44)
    count,trailing=divmod(len(data)-32,44)
    rows=[dict(zip(FIELDS,r)) for r in struct.iter_unpack('<11I',data[32:32+count*44])]
    game=[r for r in rows if begin<=r['tick']<end]
    def stats(values):
        v=sorted(values);n=len(v)
        return {'samples':n,'mean':sum(v)/n,'p95':v[min(n-1,int(n*.95))],'maximum':v[-1]} if n else None
    cpu=[r for r in game if r['total_run_us']!=0xffffffff]
    progressed=any(r['total_run_us']>0 for r in cpu)
    consistent=bool(cpu) and progressed and all(r['total_run_us']<=r['total_wall_us']*1.25+2000 for r in cpu)
    return {'scope':'Optional automatic benchmark source-thread observations. Kernel-reported run-clock deltas include instrumentation and require device-specific validation.',
        'status':'complete' if count==header[3] and not trailing else 'partial','records':count,'trailing_bytes':trailing,
        'source_span':[rows[0]['tick'],rows[-1]['tick']] if rows else None,'window':[begin,end],
        'clock_conversion_calibrated':header[4]==1,'clock_conversion_probe':{'first':header[5],'second':header[6],'return_code_raw':header[7]},
        'run_deltas_consistent_with_wall_bound':consistent,'run_clock_fidelity':'not-established-by-format-or-emulator-check',
        'run_clock_values_progressed':progressed,'run_clock_usable_for_attribution':consistent,
        'run_clock_unavailable_reason':None if consistent else 'No positive valid run-clock progress, or deltas exceed wall-time bound',
        'wall_us':{k:stats([r[k] for r in game]) for k in FIELDS if k.endswith('_wall_us')},
        'kernel_reported_run_us':{k:stats([r[k] for r in cpu if r[k]!=0xffffffff]) if consistent else None for k in FIELDS if k.endswith('_run_us')},
        'recorded_commands':stats([r['commands'] for r in game]),'sha256':hashlib.sha256(data).hexdigest(),
        'limits':['Source records stay in RAM and are written after the fixed replay. Other PERF/CSV writes still perturb playback.',
                  'Post-capture includes probes and resource preparation; it is not a pure GE or I/O timer.',
                  'Missing/unsupported run clocks are unavailable, not zero CPU cost. Stage durations and render work overlap.']}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('file',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    r=analyze(a.file);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
