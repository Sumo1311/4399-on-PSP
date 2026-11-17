"""Run a candidate in a fresh PPSSPP memory stick, without changing normal profiles."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import time
import uuid
import struct

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('--package', type=Path, default=ROOT/'work/DadNMe_Prototype')
    parser.add_argument('--app', type=Path, default=ROOT/'work/tools/PPSSPP_QA.app')
    parser.add_argument('--model', type=int, choices=[0,1], default=1)
    parser.add_argument('--map', type=Path, default=ROOT/'work/psp/dad_n_me.map')
    parser.add_argument('--visual-only', action='store_true')
    parser.add_argument('--dump-audio', action='store_true')
    parser.add_argument('--capture-loading', action='store_true', help='Diagnostic native loading-card pixel capture')
    parser.add_argument('--driver',type=Path,help='Explicit live-input QA driver')
    parser.add_argument('--replay-to-live',type=int,default=0,help='QA-only detach the deterministic pad prefix at this source tick')
    parser.add_argument('--replay',type=Path)
    parser.add_argument('--benchmark-exit',action='store_true',help='QA-only return to system after presenting the fixed replay')
    parser.add_argument('--source-stall',type=int,nargs=2,metavar=('TICK','MICROSECONDS'),help='Explicit QA-only delayed source publication to exercise frame retention')
    parser.add_argument('--trace-frames',type=int,default=0)
    parser.add_argument('--fixture',type=Path)
    parser.add_argument('--scenario',choices=['campaign','gameover'],default='campaign')
    parser.add_argument('--safe-memory',action='store_true',help='Diagnostic only: disable JIT fast-memory accesses; not FPS acceptance')
    parser.add_argument('--backend', type=int, choices=[0,1,3], default=0)
    parser.add_argument("--perf-frames",type=int,default=0)
    parser.add_argument("--stop-at",type=int,default=0)
    parser.add_argument("--capture-ticks",default="")
    parser.add_argument("--capture-pair-tick",type=int,default=0)
    parser.add_argument("--verbose-logs",action="store_true")
    parser.add_argument("--wall-sample-ms",type=float,default=0)
    parser.add_argument("--wall-snapshot-every",type=int,default=32)
    parser.add_argument("--presentation-ring",action="store_true",help="Read committed RAM presentation/loading records; no target disk logging")
    parser.add_argument('--diagnostic-fast-until',type=int,default=0,help='Diagnostic only: hold SDL fast-forward until a source tick; never FPS evidence')
    parser.add_argument('--response-metrics',action='store_true')
    args = parser.parse_args()
    if args.benchmark_exit and not args.replay:parser.error('Benchmark exit requires an explicit replay')
    if args.source_stall and (not args.replay or args.source_stall[0]<1 or not 1<=args.source_stall[1]<=1000000):parser.error('Source stall requires replay and a positive delay up to1second')
    if args.diagnostic_fast_until and (args.diagnostic_fast_until<9 or not args.replay or not args.wall_sample_ms or not (args.trace_frames or args.perf_frames)):
        parser.error('Diagnostic fast-forward requires input replay, wall sampling, TRACE/PERF, and a tick >=9')
    if args.presentation_ring and not args.wall_sample_ms:parser.error('Ring capture requires the native read-only sampler')
    if args.wall_snapshot_every<1:parser.error('Snapshot period must be positive')
    identity=json.loads((args.package/'build-identity.json').read_text())
    if hashlib.sha256(args.map.read_bytes()).hexdigest()!=identity['linker_map_sha256']:
        parser.error('The selected package and linker map belong to different builds.')
    for name,expected in identity['resources'].items():
        if hashlib.sha256((args.package/name).read_bytes()).hexdigest()!=expected:
            parser.error(f'Package identity mismatch: {name}')
    run = args.run.resolve()
    if not run.is_relative_to(ROOT) or run.exists():
        parser.error('Use a new output directory inside this project.')
    ancestor=run.parent
    while not ancestor.exists():ancestor=ancestor.parent
    free_disk=shutil.disk_usage(ancestor).free
    if free_disk<2*1024**3:
        parser.error('At least 2 GiB free disk space is required before cloning and recording a QA run.')
    run.mkdir(parents=True)
    (run/'disk-space-before.json').write_text(json.dumps(dict(free_bytes=free_disk,minimum_bytes=2*1024**3),indent=2)+'\n')
    app = run/'Dad n Me QA.app'
    stick = run/'MemoryStick'
    install = stick/'PSP/GAME/DadNMe'
    tmp = run/'tmp'
    tmp.mkdir()
    subprocess.run(['ditto', args.app, app], check=True)
    identifier = uuid.uuid4().hex[:12]
    subprocess.run(['plutil', '-replace', 'CFBundleIdentifier', '-string', f'org.ppsspp.dnm.qa.{identifier}', app/'Contents/Info.plist'], check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', app], check=True, capture_output=True)
    binary = app/'Contents/MacOS/PPSSPPSDL'
    env = dict(os.environ, PSP_QA_ACTIVE="1", TMPDIR=str(tmp), CLANG_MODULE_CACHE_PATH=str(tmp/'clang'))
    isolation = run/'isolation.dylib'
    isolation_source=ROOT/'tools/emulator_isolation_fast.m' if args.diagnostic_fast_until else ROOT/'tools/emulator_isolation.m'
    sdl_headers=['-F'+str(app/'Contents/Frameworks')] if args.diagnostic_fast_until else []
    subprocess.run(['clang', '-O2', '-dynamiclib', '-fobjc-arc', '-framework', 'Foundation']+sdl_headers+[isolation_source, '-o', isolation], check=True, env=env)
    window = run/'window-id'
    subprocess.run(['clang', '-O2', '-fobjc-arc', '-framework', 'Foundation', '-framework', 'CoreGraphics', ROOT/'tools/window_id.m', '-o', window], check=True, env=env)
    env.update(DYLD_INSERT_LIBRARIES=str(isolation), DNM_PPSSPP_INSTANCE=f'/DNM-{identifier}', DNM_PPSSPP_STICK=str(stick))
    shutil.copytree(args.package, install)
    if args.response_metrics:(install/'RESPONSE.QA').write_text('Opt-in RAM input/CPU telemetry; not a retail flag\n')
    if args.replay_to_live:(install/'REPLAY_TO_LIVE.QA').write_text(str(args.replay_to_live)+'\n')
    if args.capture_loading:(install/'LOAD_CAPTURE.QA').write_text('Explicit native loading-card capture; diagnostic timing\n')
    if args.verbose_logs:(install/"LOG.QA").write_text("explicit diagnostic logging\n")
    if args.capture_ticks:(install/"FRAMES.QA").write_text(args.capture_ticks.replace(',', ' ')+"\n")
    if args.benchmark_exit:(install/'BENCHMARK.QA').write_text('Explicit paired device benchmark\n')
    if args.source_stall:(install/'SOURCE_STALL.QA').write_text(' '.join(map(str,args.source_stall))+'\n')
    if args.capture_pair_tick:(install/"CAPTURE60.QA").write_text(str(args.capture_pair_tick)+"\n")
    if args.perf_frames:(install/"PERF.QA").write_text(str(args.perf_frames)+"\n")
    fixture=None
    if args.fixture:
        shutil.copyfile(args.fixture,install/'source.swf')
        fixture=dict(path=str(args.fixture.resolve()),sha256=hashlib.sha256(args.fixture.read_bytes()).hexdigest(),scope='instrumented QA fixture; not release data')
    replay=None
    if args.trace_frames:(install/'TRACE.DNM').write_bytes(struct.pack('<I',args.trace_frames))
    if args.replay:
        data=args.replay.read_bytes()
        magic,version,seed,count,frames=struct.unpack_from('<4s4I',data)
        if magic!=b'DNMR' or version!=1 or len(data)!=20+count*8:parser.error('Invalid input replay')
        shutil.copyfile(args.replay,install/'REPLAY.DNM')
        replay=dict(seed=seed,frames=frames,segments=count,sha256=hashlib.sha256(data).hexdigest())
    with socket.socket(socket.AF_INET6) as s:
        s.bind(('::1',0)); port=s.getsockname()[1]
    config=run/'ppsspp-test.ini'
    config.write_text(f'''[General]
ReportingHost =
CheckForNewVersion = False
DumpAudio = {'True' if args.dump_audio else 'False'}
RemoteDebuggerOnStartup = True
RemoteDebuggerLocal = True
RemoteISOPort = {port}
RemoteShareOnStartup = False
PauseOnLostFocus = False
[Sound]
EnableSound = True
[Control]
ShowTouchControls = False
[Graphics]
GraphicsBackend = {args.backend}
InternalResolution = 1
FrameSkip = 0
AutoFrameSkip = False
SkipBufferEffects = False
SkipGPUReadbackMode = 0
FrameRate = {0 if args.diagnostic_fast_until else 60}
FrameRate2 = -1
AltSpeedPercent = {0 if args.diagnostic_fast_until else 100}
AltSpeedPercent2 = -1
AnalogAltSpeedPercent = 100
[SystemParam]
PSPModel = {args.model}
[CPU]
CPUSpeed = 333
FastMemoryAccess = {"False" if args.safe_memory else "True"}
[Network]
EnableWlan = False
EnableAdhocServer = False
DontDownloadInfraJson = True
''')
    (run/'emulator-test-profile.json').write_text(json.dumps(dict(
        cpu_mhz=333,psp_model=args.model,internal_resolution=1,frame_skip=0,
        auto_frame_skip=False,skip_buffer_effects=False,skip_gpu_readbacks=False,
        diagnostic_fast_forward=bool(args.diagnostic_fast_until),
        normal_speed=not bool(args.diagnostic_fast_until),
        scope='Fixed PSP clock and normal-speed settings do not reproduce physical PSP CPU/GE timing; hardware qualification remains separate.'
    ),indent=2)+'\n')
    text=args.map.read_text()
    address=int(re.search(r'^\s*(0x[0-9a-f]+)\s+g_dnm\s*$',text,re.M).group(1),16)
    if args.wall_sample_ms:
        presentation_probe=re.search(r'^\s*(0x[0-9a-f]+)\s+g_dnm_60\s*$',text,re.M)
        if presentation_probe:env['PSP_WALL_LINK_ADDRESS']=presentation_probe.group(1)
        symbols=subprocess.check_output(['nm',str(binary)],text=True,stderr=subprocess.DEVNULL)
        base_symbol=int(re.search(r'^([0-9a-fA-F]+)\s+S\s+__ZN6Memory4baseE$',symbols,re.M).group(1),16)
        env.update(PSP_WALL_SNAPSHOT=str(run/"native-probe.bin"),PSP_WALL_OUTPUT=str(run/'wall-frames.csv'),PSP_WALL_STOP=str(run/'wall.stop'),
                   PSP_WALL_SUMMARY=str(run/'wall-sampler.json'),PSP_WALL_PROBE_ADDRESS=hex(address),
                   PSP_WALL_PROBE_OFFSET=str(identity['wall_probe_offset']),
                   PSP_WALL_PERIOD_US=str(int(args.wall_sample_ms*1000)),
                   PSP_WALL_SNAPSHOT_EVERY=str(args.wall_snapshot_every),
                   PSP_WALL_BASE_SYMBOL_VMADDR=hex(base_symbol))
    if args.response_metrics:
        sym=re.search(r'^\s*(0x[0-9a-f]+)\s+g_dnm_response_ring\s*$',text,re.M)
        if not sym or not args.wall_sample_ms:parser.error('Response telemetry requires instrumented map and native sampler')
        env.update(PSP_RESPONSE_RING_ADDRESS=sym.group(1),PSP_RESPONSE_OUTPUT=str(run/'response.csv'),PSP_RESPONSE_SUMMARY=str(run/'response-reader.json'))
    if args.presentation_ring:
        symbols={name:re.search(r'^\s*(0x[0-9a-f]+)\s+'+name+r'\s*$',text,re.M) for name in ['g_dnm_present_ring','g_dnm_loading_address']}
        if not all(symbols.values()):parser.error('This package has no committed presentation ring')
        env.update(PSP_PRESENT_RING_ADDRESS=symbols['g_dnm_present_ring'].group(1),PSP_LOADING_ADDRESS=symbols['g_dnm_loading_address'].group(1),
                   PSP_PRESENT_RING_OUTPUT=str(run/'presentation-ring.csv'),PSP_PRESENT_LOAD_OUTPUT=str(run/'presentation-loading.csv'),
                   PSP_PRESENT_RING_SUMMARY=str(run/'presentation-ring.json'))
    if args.diagnostic_fast_until:
        env.update(PSP_DIAG_FAST_UNTIL=str(args.diagnostic_fast_until),PSP_DIAG_FAST_LOG=str(run/'diagnostic-speed.jsonl'))
    with (run/'emulator.log').open('wb') as log:
        process=subprocess.Popen([binary,'-j','--windowed','--loglevel=3',f'--appendconfig={config}',install],cwd=install,env=env,stdout=log,stderr=subprocess.STDOUT)
    try:
        for _ in range(150):
            if process.poll() is not None: raise RuntimeError(f'PPSSPP exited {process.returncode}')
            try:
                with socket.create_connection(('::1',port),timeout=.2):break
            except OSError:time.sleep(.1)
        else:raise RuntimeError('PPSSPP debugger did not start')
        presentation_symbol=re.search(r'^\s*(0x[0-9a-f]+)\s+g_dnm_60\s*$',text,re.M)
        session=dict(diagnosticSafeMemory=args.safe_memory,presentationAddress=int(presentation_symbol.group(1),16) if presentation_symbol else None,port=port,pid=process.pid,address=address,window=str(window),install=str(install),package={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in args.package.iterdir() if p.is_file()},emulator_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),replay=replay,model=args.model,traceFrames=args.trace_frames,fixture=fixture,scenario=args.scenario,stopAt=args.stop_at)
        session["nativeProbeSnapshot"]=str(run/"native-probe.bin") if args.wall_sample_ms else None
        session["nativeSamplerSha256"]=hashlib.sha256((ROOT/"tools/native_wall_sampler.inc").read_bytes()).hexdigest() if args.wall_sample_ms else None
        session["nativeProbeReaderSha256"]=hashlib.sha256((ROOT/"tools/native_probe.mjs").read_bytes()).hexdigest() if args.wall_sample_ms else None
        session["observer"]= "native read-only snapshot" if args.wall_sample_ms else "debugger memory.read"
        session['presentationRing']=args.presentation_ring
        session['benchmarkExit']=args.benchmark_exit
        session['sourceStall']=args.source_stall
        session['diagnosticFastUntil']=args.diagnostic_fast_until
        session['timingUse']='diagnostic-only-not-FPS' if args.diagnostic_fast_until else 'normal-clock'
        session['wallSnapshotEvery']=args.wall_snapshot_every
        session['ringReaderSha256']=hashlib.sha256((ROOT/'tools/presentation_ring_reader.inc').read_bytes()).hexdigest() if args.presentation_ring else None
        (run/'session.json').write_text(json.dumps(session,indent=2)+'\n')
        if args.visual_only:
            started=time.monotonic()
            for second in (5,50,65):
                while time.monotonic()-started<second:
                    if process.poll() is not None:raise RuntimeError(f'PPSSPP exited during visual run: {process.returncode}')
                    time.sleep(.5)
                wid=subprocess.check_output([window,str(process.pid)],text=True,timeout=3).strip()
                subprocess.run(['/usr/sbin/screencapture','-x','-o','-l',wid,run/f'visual-{second}.png'],check=True,timeout=5)
                print(f'Captured visual-{second}',flush=True)
            return
        driver=subprocess.Popen(['node',args.driver or ROOT/'tools/ppsspp_driver.mjs',run],env=env)
        started=time.monotonic()
        try:
            while driver.poll() is None:
                if process.poll() is not None:raise RuntimeError(f'PPSSPP exited during test: {process.returncode}')
                limit=(replay['frames']/30*1.7+300) if replay else 300
                if time.monotonic()-started>limit:raise TimeoutError(f'Driver exceeded {limit:.0f} seconds')
                time.sleep(.5)
            if driver.returncode:raise RuntimeError(f'Driver failed: {driver.returncode}')
        finally:
            if driver.poll() is None:driver.terminate();driver.wait(timeout=5)
    finally:
        if args.wall_sample_ms:
            (run/"wall.stop").write_text("runner completed\n")
            for _ in range(100):
                if (run/"wall-sampler.json").exists():break
                time.sleep(.02)
        (run/'process-status.json').write_text(json.dumps({'returncode_before_cleanup':process.poll()})+'\n')
        if process.poll() is None:
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()

if __name__=='__main__':main()
