"""Compare matched physical benchmark captures; diagnostic writes affect timing."""
from pathlib import Path
import argparse
import csv
import hashlib
import json
import struct

FIELDS = 'tick rendered phase interval_us work_us logic_us draw_us finish_us io_us decode_us evict_us sync_us vblank_us filter_us alloc_us decoded_bytes misses evictions heap_free audio_mix_us audio_blocks audio_io_us audio_max_us audio_lock_us write_us lag_us present_offset_us'.split()


def stats(values):
    values = sorted(values)
    n = len(values)
    return {'samples': n, 'mean': sum(values) / n, 'p95': values[min(n-1, int(n*.95))],
            'p99': values[min(n-1, int(n*.99))], 'maximum': values[-1]} if n else None


def analyze(folder, expected, fixture_sha):
    data = (folder / 'PERF.BIN').read_bytes()
    assert len(data) >= 16
    magic, size, version, clock = struct.unpack_from('<4I', data)
    assert (magic, size, version) == (0x31465250, 108, 2)
    count, trailing = divmod(len(data)-16, size)
    rows = [dict(zip(FIELDS, r)) for r in struct.iter_unpack('<27I', data[16:16+count*size])]
    assert rows
    actual = hashlib.sha256((folder / 'EBOOT.PBP').read_bytes()).hexdigest()
    assert actual == expected, 'Wrong benchmark executable'
    assert hashlib.sha256((folder / 'REPLAY.DNM').read_bytes()).hexdigest() == fixture_sha, 'Different benchmark input'
    boot = (folder / 'BOOT_D27_03.LOG').read_text(errors='replace')
    complete = 'benchmark-complete' in boot and rows[-1]['tick'] >= 1200 and trailing == 0
    windows = {}
    for start, end in [(240, 1100), (400, 1100)]:
        game = [r for r in rows if start <= r['tick'] < end and r['phase'] == 3]
        seen, logic = set(), []
        for r in game:
            if r['logic_us'] and r['tick'] not in seen:
                seen.add(r['tick'])
                logic.append(r['logic_us'])
        present = []
        for r in csv.DictReader((folder / 'PRESENT60.CSV').open()):
            if all(v and v.isdigit() for v in r.values()):
                r = {k: int(v) for k,v in r.items()}
                if start <= r['source_tick'] < end:
                    present.append(r)
        seconds = (present[-1]['time_us']-present[0]['time_us'])/1e6 if len(present)>1 else 0
        windows[f'{start}:{end}'] = {
            'logic_elapsed_us_unique_source': stats(logic),
            'ge_wait_us': stats([r['sync_us'] for r in game]),
            'draw_and_lock_us': stats([r['draw_us'] for r in game]),
            'audio_mix_us': stats([r['audio_mix_us'] for r in game]),
            'source_hz': (present[-1]['source_tick']-present[0]['source_tick'])/seconds if seconds else None,
            'presentation_hz': (len(present)-1)/seconds if seconds else None,
            'repeated_pairs': sum(not r['fresh'] and not r['subframe'] for r in present)}
    return {'complete': complete, 'records': count, 'trailing_bytes_preserved_in_original': trailing,
            'source_span': [rows[0]['tick'], rows[-1]['tick']], 'start_cpu_mhz': clock,
            'eboot_sha256': actual, 'profile_sha256': hashlib.sha256(data).hexdigest(), 'windows': windows}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('r3', type=Path)
    p.add_argument('r4', type=Path)
    p.add_argument('--identities', type=Path, default=Path(__file__).with_name('BENCHMARK_IDENTITIES.json'))
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--environment', choices=['physical-PSP','emulator','unspecified'], default='unspecified')
    a = p.parse_args()
    identity = json.loads(a.identities.read_text())
    r3 = analyze(a.r3, identity['r3_eboot_sha256'], identity['fixture_sha256'])
    r4 = analyze(a.r4, identity['r4_eboot_sha256'], identity['fixture_sha256'])
    report = {'scope': 'Matched fixed-input diagnostics, not uninstrumented FPS or physical button-to-photon latency. Stage times include preemption/waits and overlap.',
              'environment': a.environment,
              'status': 'complete-pair' if r3['complete'] and r4['complete'] else 'incomplete-pair',
              'R3': r3, 'R4': r4, 'ordinary_speed_target_source_hz': 30,
              'ordinary_fps_acceptance': 'not established by these instrumented captures'}
    a.output.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
