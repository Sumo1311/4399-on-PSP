// Execute the pinned freeze methods with declared Musou boss-pause transitions.
// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const [reference, output] = process.argv.slice(2);
if (!output) throw Error('usage: export_musou_freeze_reference.mjs reference MUSFREEZE.IN');
const relative = 'upstream/CORE_KernelLogic/src/net/play5d/game/bvn/ctrler/EffectCtrl.as';
const bytes = fs.readFileSync(path.join(reference, relative));
const source = bytes.toString().replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');
function make(hz) {
    const control = {paused: false, slowRate: 0, pause() { this.paused = true; }, resume() { this.paused = false; }};
    let frame = 0, completions = 0;
    const h = {freezeEnabled: true, _onFreezeOver: null,
        _hitHandler: {clearHitFocusOnFreezeEnd() { completions++; }}, _slowHandler: {bgBlur() {}}};
    Object.defineProperty(h, '_freezeFrame', {get: () => frame, set: v => { frame = v | 0; }});
    for (const name of ['freeze', 'renderFreeze']) {
        const match = new RegExp(`(?:public|private) function ${name}\\(([^)]*)\\):\\w+\\s*\\{`).exec(source);
        let end = match.index + match[0].length, depth = 1;
        const start = end;
        for (; depth; end++) { if (source[end] === '{') depth++; if (source[end] === '}') depth--; }
        const args = match[1].split(',').map(v => v.trim().replace(/:\w+/, ''));
        const body = source.slice(start, end - 1)
            .replace(/var (\w+):int\s*=\s*([^;]+);/g, 'var $1=(($2)|0);').replace(/var (\w+):\w+/g, 'var $1');
        h[name] = Function('GameConfig', 'GameCtrl', `return function(${args}){with(this){${body}}}`)({FPS_GAME: hz}, {I: control});
    }
    return {h, control, completions: () => completions};
}
const lines = [];
for (const hz of [30, 60]) for (const phase0 of [0, hz % 60]) for (const pattern of [0, 1]) {
    const scenario = lines.length / 180;
    const {h, control, completions} = make(hz);
    let phase = phase0;
    for (let tick = 0; tick < 180; tick++) {
        const mode = pattern && tick === 7 ? 1 : pattern && tick === 20 ? 2 : 0;
        if (mode === 1) h.freezeEnabled = false;
        if (mode === 2) { h.freezeEnabled = true; control.resume(); }
        const ms = ({0: 400, 1: 50, 5: 500, 10: 900, 21: 50, 36: 200, 70: 50, 72: 400, 110: 1, 120: 1000, 121: 500})[tick] ?? 0;
        if (ms) h.freeze(ms);
        const before = h._freezeFrame, paused = +control.paused;
        phase += hz;
        if (phase >= 60) { phase -= 60; if (h.freezeEnabled) h.renderFreeze(); }
        lines.push([scenario, hz, phase0, tick, ms, mode, before, paused,
            h._freezeFrame, +control.paused, +h.freezeEnabled, completions()].join(' '));
    }
}
fs.writeFileSync(output, `${lines.length}\n${lines.join('\n')}\n`);
const sha = b => crypto.createHash('sha256').update(b).digest('hex');
fs.writeFileSync(output + '.json', JSON.stringify({rows: lines.length, scenarios: 8, source: relative,
    sourceSha256: sha(bytes), fixtureSha256: sha(fs.readFileSync(output)),
    scope: 'Original freeze/renderFreeze methods with AS3 int assignment and GameCtrl pause/resume stubs; '
        + 'declared boss disable/resume transitions, shorter/longer/disabled/zero-frame requests and both clock phases. '
        + 'Not execution in the Flash VM or full boss UI, callback or blur scheduling.'}, null, 2) + '\n');
console.log(`Exported ${lines.length} original freeze-counter and independent pause states`);
