#!/usr/bin/env python3
"""Build the omitted original hurt-down jump clips without rebaking accepted fighter art."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import traceback
from concurrent.futures import ThreadPoolExecutor, as_completed

from append_recovery import Bank, append_recovery
from build_actions import verified_source, detached_scripts
from fighter_actions import frame_scripts
from pack_actions import ClipPlan, clip_plan, compile_pack, detached_calls, child_requests
from pack_fighter import authored_stats, read_timeline


def build(args, row, paths, reports, scripts_by_source):
    variant, bank_name, family, *internal_frame = row.split()
    bank_name += '.BVF'
    if args.bank and args.bank != bank_name:
        return None
    variant, swf = verified_source(args.reference, 'fighters', family if internal_frame else variant)
    frame = int(internal_frame[0]) if internal_frame else variant['start_frame']
    if row.startswith('byakuya_bankai_old '): frame = 1
    scripts = scripts_by_source[str(swf)] / 'scripts'
    main = scripts / 'main_mc.as'
    cache = Path(paths[bank_name])
    tsv = (cache / 'fighter.tsv').read_text()
    symbol, = [line.split('\t',2)[2] for line in tsv.splitlines() if line.startswith('symbol\t')]
    timeline_script = scripts / Path(*symbol.split('.')).with_suffix('.as')
    authored = dict(frame_scripts(timeline_script))
    timeline = read_timeline(cache)
    plan, = clip_plan(timeline, authored, (), only=('起身',))
    output = args.out / bank_name.removesuffix('.BVF')
    reuse=False
    if output.exists():
        previous=output/'verification.json'
        if args.resume and previous.exists():
            proof=json.loads(previous.read_text())
            assert proof['swf_sha256']==hashlib.sha256(swf.read_bytes()).hexdigest()
            assert proof['timeline_script_sha256']==hashlib.sha256(timeline_script.read_bytes()).hexdigest()
            assert proof['base_sha256']==hashlib.sha256((args.package/bank_name).read_bytes()).hexdigest()
            reuse=True
        else:
            assert args.resume
            attempt=2
            while (args.out/f'{bank_name[:-4]}-attempt{attempt}').exists():attempt+=1
            output=args.out/f'{bank_name[:-4]}-attempt{attempt}'
    output.mkdir(exist_ok=reuse)
    shutil.copy2(cache / 'hits.json', output / 'hits.json')
    ids = output / 'hit-ids.txt'
    ids.write_text(''.join(name+'\n' for name in json.loads((output/'hits.json').read_text())))
    cp = os.pathsep.join(map(str,(args.classes, args.jpexs/'ffdec.jar',args.jpexs/'lib/*')))
    command = [args.java,'-Xmx512m','-cp',cp,'ExtractFighter',str(swf),str(frame),str(ids),str(output)]
    if bank_name == 'MAYURI.BVF': command += ['--actor-class','mayuri_fla.剑八全动作_2']
    launches=detached_calls([plan],authored)
    if launches:
        requests=output/'objects.tsv'
        request_rows=[f'{kind}\t{number}\t{name}\n' for kind,number,name in dict.fromkeys(key[:3] for key in launches)]
        if any(key[0]=='attacker' for key in launches):
            request_rows += [f'{kind}\t{number}\t{symbol}\t{name}\n' for kind,number,symbol,name in child_requests(scripts)]
        requests.write_text(''.join(request_rows))
        command += ['--objects',str(requests)]
    if not reuse:
        with (output / 'extract.log').open('w') as log:
            subprocess.run(command+[str(plan.start),str(plan.end-1)],check=True,stdout=log,stderr=subprocess.STDOUT,timeout=180)
    fresh = read_timeline(output)
    assert fresh.labels['起身'] == plan.start
    original_report = Path(reports[bank_name])
    old_meta = json.loads(original_report.read_text())
    base = Bank.read(args.package / bank_name)
    # Keep all existing clip IDs available to bindings in the new frame scripts.
    stubs = [ClipPlan(item['label'],item['start'],item['start']) for item in old_meta['clips']]
    stubs += [ClipPlan(f'__retained-{i}',0,0) for i in range(len(stubs),len(base.tables['clips']))]
    assert len(stubs) == len(base.tables['clips'])
    auxiliary = output / 'recovery-only.BVF'
    compile_pack(output,fresh,authored,[*stubs,plan],authored_stats(main,frame),auxiliary,detached_scripts(fresh,scripts))
    report = append_recovery(args.package/bank_name,auxiliary,args.out/'packs'/bank_name,original_report)
    report.update(bank=bank_name,swf=str(swf),swf_sha256=hashlib.sha256(swf.read_bytes()).hexdigest(),
        timeline_script=str(timeline_script),timeline_script_sha256=hashlib.sha256(timeline_script.read_bytes()).hexdigest())
    (output/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package',type=Path,required=True)
    parser.add_argument('--source-caches',type=Path,required=True)
    parser.add_argument('--reports',type=Path,required=True)
    parser.add_argument('--fresh-directory',type=Path,required=True)
    parser.add_argument('--original-sources',type=Path,required=True)
    parser.add_argument('--reference',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--jpexs',type=Path,required=True)
    parser.add_argument('--java',required=True)
    parser.add_argument('--bank')
    parser.add_argument('--jobs',type=int,default=1)
    parser.add_argument('--resume',action='store_true')
    args=parser.parse_args()
    args.reference=args.reference.resolve();args.out.mkdir(exist_ok=args.resume);(args.out/'packs').mkdir(exist_ok=args.resume);args.classes=args.out/'classes';args.classes.mkdir(exist_ok=args.resume)
    cp=os.pathsep.join(map(str,(args.jpexs/'ffdec.jar',args.jpexs/'lib/*')))
    javac=str(Path(args.java).with_name('javac'))
    subprocess.run([javac,'-encoding','UTF-8','-cp',cp,'-d',str(args.classes),
                    str(Path(__file__).with_name('FlashLayers.java')),str(Path(__file__).with_name('ExtractFighter.java'))],check=True)
    original=json.loads(args.original_sources.read_text());sources=sorted({row[3] for row in original})
    scripts={source:args.fresh_directory/f'swf-{i:02d}' for i,source in enumerate(sources)}
    paths=json.loads(args.source_caches.read_text());reports=json.loads(args.reports.read_text())
    result=[]
    errors=[]
    lines=Path(__file__).with_name('versus_fighters.txt').read_text().splitlines()
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures={pool.submit(build,args,line,paths,reports,scripts):line for line in lines}
        for future in as_completed(futures):
            try:row=future.result()
            except Exception:
                bank=futures[future].split()[1]
                path=args.out/(bank+'-failure.log');path.write_text(traceback.format_exc());errors.append(str(path))
                print(f'FAILED {bank}: {path}',flush=True)
                continue
            if row:
                result.append(row)
                print(f"Built {row['bank']}: {row['frames_added']} original recovery frames",flush=True)
    result.sort(key=lambda row:row['bank'])
    (args.out/'verification.json').write_text(json.dumps({'banks':result,'errors':errors},ensure_ascii=False,indent=2)+'\n')
    if errors:raise SystemExit(1)


if __name__=='__main__':
    main()
