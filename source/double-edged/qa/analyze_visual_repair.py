"""Retain source-state, command and actual native framebuffer witnesses."""
from pathlib import Path
import hashlib
import json
import re
from PIL import Image, ImageChops, ImageDraw

R = Path(__file__).resolve().parent
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
host = [json.loads(s) for s in (R/'host/ordinary-1700/NATIVE.JSONL').open()]
evidence = {}
for label in ['b154', 'b155']:
    run = R/f'runs/{label}-ordinary1700-native'
    p = run/'MemoryStick/PSP/GAME/ScriptProbe'
    result = json.loads((run/'result.json').read_text())
    assert result['error'] == 0 and result['frames'] == 1700
    rows = [json.loads(s) for s in (p/'FRONTEND.JSONL').open()]
    assert len(rows) == len(host) == 1700
    uid_differences = 0
    for n, h in zip(rows, host):
        n = dict(n); h = dict(h)
        nw = n.pop('qa_visibility'); hw = h.pop('qa_visibility')
        assert n == h
        for field in ['actors', 'coins']:
            assert len(nw[field]) == len(hw[field])
            for x, y in zip(nw[field], hw[field]):
                x = dict(x); y = dict(y)
                uid_differences += x.pop('uid') != y.pop('uid')
                if label == 'b154' and field == 'coins':
                    assert x['frame'] == 2 and y['frame'] == 8
                    x['frame'] = y['frame']
                assert x == y
    commands = re.findall(r'MIRROR_CHECK (\d+) (PASS|FAIL)', (p/'GRAPHICS.LOG').read_text())
    assert len(commands) == 1700 and all(s == 'PASS' for _, s in commands)
    assert not (p/'SCRIPT_ERRORS.JSONL').read_bytes()
    evidence[label] = dict(native_result=str(run/'result.json'), source_steps=1700,
        gameplay_rng_audio_exact=True, mirror_reference_command_passes=len(commands),
        ignored_readonly_display_allocation_uid_samples=uid_differences,
        source_witness_at_death=rows[1297]['qa_visibility'],
        visual_policy=json.loads((p/'VISUAL_POLICY_STATS.JSON').read_text()),
        actual_framebuffers={n:sha(p/(n+'.RGBA')) for n in ['MID60','FINAL']})

sheet = Image.new('RGB', (610, 390), '#eeeeee')
draw = ImageDraw.Draw(sheet)
for column, label in enumerate(['b154','b155']):
    x = 10 + column*300
    draw.text((x, 8), label.upper(), fill='black')
    for row, (n, box, scale) in enumerate([('MID60',(318,185,380,240),4), ('FINAL',(195,211,220,239),5)]):
        image = Image.open(R/'inspection'/f'{label}-{n}.png').convert('RGB')
        crop = image.crop(box)
        sheet.paste(crop.resize((crop.width*scale,crop.height*scale),Image.Resampling.NEAREST),(x,28 if row==0 else 255))
sheet.save(R/'inspection/B154-B155-native-comparison.png')
evidence['pixel_changes'] = {}
for n in ['MID60','FINAL']:
    a=Image.open(R/'inspection'/f'b154-{n}.png').convert('RGB')
    b=Image.open(R/'inspection'/f'b155-{n}.png').convert('RGB')
    evidence['pixel_changes'][n] = dict(bbox=ImageChops.difference(a,b).getbbox())
report = dict(status='passed-actual-native-front-coin-and-dying-body',evidence=evidence,
    note='Frame1298 is actual fatal knockback with source alpha0,landedfalse. Final1700 includes actual dropped coin. Native images were visually inspected;mirror/source command checks pass. Extra display allocation UID values are not gameplay state. Coin frame2 versus8 is the intended repair.',
    timing_scope='Intrusive mirror diagnostics;not FPS evidence',physical_psp_tested=False)
(R/'B155_NATIVE_VISUAL.json').write_text(json.dumps(report,indent=2)+'\n')
print(report['status'])
