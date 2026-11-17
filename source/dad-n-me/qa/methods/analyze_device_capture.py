"""Read candidate performance/control logs against a delivery manifest; never change captures."""
from pathlib import Path
import argparse,csv,hashlib,json,math,re,struct,sys

ROOT=Path(__file__).resolve().parents[1]
PERF_FIELDS='tick rendered phase interval_us work_us logic_us draw_us finish_us io_us decode_us evict_us sync_us vblank_us filter_us alloc_us decoded_bytes misses evictions heap_free audio_mix_us audio_blocks audio_io_us audio_max_us audio_lock_us write_us lag_us present_offset_us'.split()
PRESENT_FIELDS='presentation source_tick subframe fresh moving_samples vcount time_us logic_us capture_us'.split()
CONTROL_FIELDS='us tick raw buttons phase key_mask blocked_buttons blocked_analog lx ly x y health hero_frame game_pause body_frame loaded controllers'.split()
BENCH_REPLAY='1b3dbfdc471c3bb97ba66f223ae35962e7f77c56dcaecde5416b8cb941c1c977'
class CaptureError(ValueError):pass

def require(ok,message):
 if not ok:raise CaptureError(message)
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def stats(values,budget=None):
 v=sorted(values);n=len(v)
 if not n:return None
 out=dict(samples=n,mean=sum(v)/n,p95=v[min(n-1,int(n*.95))],p99=v[min(n-1,int(n*.99))],maximum=v[-1])
 if budget is not None:out['over_budget']=sum(x>budget for x in v);out['budget_us']=budget
 return out

def read_perf(path):
 raw=Path(path).read_bytes();require(len(raw)>=16,'PERF header missing')
 magic,size,version,clock=struct.unpack_from('<4I',raw);require((magic,size,version)==(0x31465250,108,2),'Unsupported PERF format')
 count,trailing=divmod(len(raw)-16,size);require(count>0,'No complete PERF records')
 rows=[dict(zip(PERF_FIELDS,r)) for r in struct.iter_unpack('<27I',raw[16:16+count*size])]
 for a,b in zip(rows,rows[1:]):require(b['rendered']==a['rendered']+1 and b['tick']>=a['tick'],'PERF sequence discontinuity')
 return rows,dict(records=count,start_cpu_mhz=clock,trailing_bytes_preserved=trailing,sha256=hashlib.sha256(raw).hexdigest(),trailing_sha256=hashlib.sha256(raw[-trailing:]).hexdigest() if trailing else None)

def read_present(path):
 raw=Path(path).read_bytes();lines=raw.decode('utf-8').splitlines();require(bool(lines),'PRESENT header missing')
 require(next(csv.reader([lines[0]]))==PRESENT_FIELDS,'Unsupported PRESENT schema')
 rows=[];tail=None
 for index,line in enumerate(lines[1:],1):
  if index==len(lines)-1 and not raw.endswith(b'\n'):
   tail=line;continue
  values=next(csv.reader([line]));valid=len(values)==len(PRESENT_FIELDS) and all(x.isascii() and x.isdigit() for x in values)
  if not valid:
   raise CaptureError('Corrupt nonterminal PRESENT row')
  row=dict(zip(PRESENT_FIELDS,map(int,values)));require(row['subframe'] in (0,1) and row['fresh'] in (0,1),'Invalid presentation flags')
  if rows:
   last=rows[-1];require(row['presentation']==last['presentation']+1 and row['source_tick']>=last['source_tick'],'PRESENT sequence discontinuity')
   require(row['time_us']>last['time_us'],'PRESENT clock did not advance monotonically')
  rows.append(row)
 require(len(rows)>1,'Too few complete PRESENT rows')
 return rows,dict(records=len(rows),partial_terminal_row_preserved=tail,sha256=hashlib.sha256(raw).hexdigest())

def summarize_window(rows):
 if len(rows)<2:return None
 elapsed=rows[-1]['time_us']-rows[0]['time_us'];require(elapsed>0,'Nonpositive window duration')
 fresh=[];last_tick=None;nonadvancing=0
 for row in rows:
  if row['subframe']==0 and row['fresh']:
   if row['source_tick']==last_tick:nonadvancing+=1
   elif row['logic_us']>0:fresh.append(row)
   last_tick=row['source_tick']
 intervals=[b['time_us']-a['time_us'] for a,b in zip(rows,rows[1:])]
 vcount=[(b['vcount']-a['vcount'])&0xffffffff for a,b in zip(rows,rows[1:])]
 return dict(source_tick_span=[rows[0]['source_tick'],rows[-1]['source_tick']],time_us_span=[rows[0]['time_us'],rows[-1]['time_us']],duration_seconds=elapsed/1e6,
  observed_source_hz=(rows[-1]['source_tick']-rows[0]['source_tick'])*1e6/elapsed,presentation_hz=(len(rows)-1)*1e6/elapsed,
  midpoint_slots=sum(r['subframe']==0 for r in rows),held_pairs=sum(r['subframe']==0 and not r['fresh'] for r in rows),nonadvancing_fresh_pairs=nonadvancing,
  unique_advancing_logic_samples=len(fresh),logic_wall_us=stats([r['logic_us'] for r in fresh],1000000/30),capture_wall_us=stats([r['capture_us'] for r in fresh]),
  presentation_interval_us=stats(intervals,25025),unlogged_refresh_gaps=sum(1<d<0x80000000 for d in vcount),vcount_reset_or_nonprogress_intervals=sum(d==0 or d>=0x80000000 for d in vcount))

def joined_gameplay(perf,present):
 by_index={r['rendered']:r for r in perf};out=[]
 for row in present:
  p=by_index.get(row['presentation'])
  if not p:continue
  require(p['tick']==row['source_tick'],'PERF/PRESENT source-tick mismatch')
  if row['subframe']==0:require(p['logic_us']==row['logic_us'],'PERF/PRESENT logic-duration mismatch')
  if p['phase']==3:out.append(dict(row,perf=p))
 return out

def split_segments(rows):
 result=[];current=[]
 for row in rows:
  if current and row['presentation']!=current[-1]['presentation']+1:result.append(current);current=[]
  current.append(row)
 if current:result.append(current)
 return result

def controls(folder):
 result=[]
 for path in sorted(Path(folder).glob('CONTROL[0-9][0-9].CSV')):
  raw=path.read_bytes();lines=raw.decode('utf-8').splitlines();require(bool(lines) and next(csv.reader([lines[0]]))==CONTROL_FIELDS,'Unsupported control schema')
  rows=[]
  for line in lines[1:]:
   values=next(csv.reader([line]));require(len(values)==len(CONTROL_FIELDS),'Incomplete control row')
   row=dict(zip(CONTROL_FIELDS,values));row={k:float(v) if k in ['x','y','health'] else int(v) for k,v in row.items()};rows.append(row)
  require(all(b['us']>=a['us'] and b['tick']>=a['tick'] for a,b in zip(rows,rows[1:])),'Control sequence discontinuity')
  text=path.with_suffix('.TXT');reason=None
  if text.exists():
   m=re.search(r'snapshot reason=(\d+)',text.read_text(errors='replace'));reason=int(m.group(1)) if m else None
  still=[]
  for a,b in zip(rows,rows[1:]):
   analog_intent=b['lx']<80 or b['lx']>176 or b['ly']<80 or b['ly']>176
   if b['us']>a['us'] and (b['raw']&0xf0 or analog_intent) and b['health']>0 and (b['x'],b['y'])==(a['x'],a['y']):still.append(dict(first_us=a['us'],last_us=b['us'],tick=b['tick'],raw=b['raw'],buttons=b['buttons'],keys=b['key_mask'],lx=b['lx'],ly=b['ly'],blocked_buttons=b['blocked_buttons'],blocked_analog=b['blocked_analog'],game_pause=b['game_pause'],hero_frame=b['hero_frame'],controllers=b['controllers']))
  result.append(dict(file=path.name,sha256=hashlib.sha256(raw).hexdigest(),records=len(rows),reason=reason,time_us_span=[rows[0]['us'],rows[-1]['us']] if rows else None,directional_input_without_position_change=still,interpretation='Sparse intervals are leads,not proof of a stuck character; combat,collision,stun,loading and native pause require context.'))
 return result

def verify_identity(folder,environment,manifest_path=None):
 folder=Path(folder).resolve();require(environment in ['physical-PSP','emulator'],'Explicit environment required')
 markers=[str(p/'emulator-test-profile.json') for p in [folder,*folder.parents] if (p/'emulator-test-profile.json').exists()]
 require(not(environment=='physical-PSP' and markers),'Emulator run cannot be labelled physical PSP')
 manifest_path=Path(manifest_path or ROOT/'delivery-r15/INSTALL_MANIFEST.json').resolve()
 manifest=json.loads(manifest_path.read_text());actual=sha(folder/'EBOOT.PBP');names=[n for n,files in manifest.items() if files['EBOOT.PBP']['sha256']==actual];require(len(names)==1,'Executable does not uniquely match the selected delivery manifest')
 name=names[0]
 for file in ['source.swf','art.pak','audio.pak']:
  require(sha(folder/file)==manifest[name][file]['sha256'],f'Resource identity mismatch: {file}')
 delivered_kind='benchmark' if name.endswith('Bench') else 'manual' if name.endswith('Diag') else 'ordinary'
 kind=delivered_kind;flags=sorted(f.name for f in folder.glob('*.QA'))
 if (folder/'BENCHMARK.QA').exists():
  require(delivered_kind=='benchmark' or environment=='emulator','Unexpected benchmark flags in a physical live-input variant')
  kind='benchmark'
 if kind=='benchmark':
  require((folder/'BENCHMARK.QA').exists(),'Benchmark marker missing')
  require(sha(folder/'REPLAY.DNM')==BENCH_REPLAY,'Benchmark input mismatch')
 else:require(not(folder/'REPLAY.DNM').exists(),'Unexpected input replay in a live-input capture')
 return dict(variant=name,manifest_path=str(manifest_path),manifest_sha256=sha(manifest_path),delivered_kind=delivered_kind,kind=kind,qa_role_override=kind!=delivered_kind,instrumentation_flags=flags,eboot_sha256=actual,resources_verified=True,environment=environment,environment_provenance='Explicit caller declaration; emulator ancestry markers rejected for physical classification',emulator_markers=markers)

def analyze(folder,environment,window_seconds=5,manifest_path=None):
 folder=Path(folder).resolve();identity=verify_identity(folder,environment,manifest_path);require(window_seconds>0,'Window seconds must be positive')
 out=dict(status='analyzed',identity=identity,physical_performance_acceptance='not established by automatic analysis',input_to_photon_latency='not measured',control_snapshots=controls(folder),limits=[
  'SceKernel wall timers include waits and preemption; CPU/GE/audio stages overlap and are not summed into a total.',
  'Root phase3 can contain introduction,pause or loading; manual windows are diagnostic candidates,not verified combat intervals.',
  'A fresh scene without source-tick progress is flagged separately; it is not treated as a new logic sample.',
  'Partial terminal data is retained. Only complete aligned records are used; source files are never rewritten.',
  'Refresh gaps can include unlogged loading-card presentations.60Hz presentation alone does not prove30Hz gameplay.',
  'Heap free is allocator-arena free,not total free system memory. No direct input-to-photon measurement exists.'])
 if not(folder/'PERF.BIN').exists():
  require(identity['kind']=='ordinary','Performance capture missing');out['performance_status']='not_recorded_in_ordinary_build';return out
 perf,meta=read_perf(folder/'PERF.BIN');present,pm=read_present(folder/'PRESENT60.CSV');joined=joined_gameplay(perf,present)
 perf_indices={p['rendered'] for p in perf}
 out.update(perf=meta,presentation=pm,performance_status='partial-prefix' if meta['trailing_bytes_preserved'] or pm['partial_terminal_row_preserved'] else 'complete-record-prefix',aligned_gameplay_presentations=len(joined),presentation_records_without_complete_PERF=sum(r['presentation'] not in perf_indices for r in present))
 for snapshot in out['control_snapshots']:
  span=snapshot['time_us_span'];snapshot['overlaps_presentation_log']=bool(span and span[1]>=present[0]['time_us'] and span[0]<=present[-1]['time_us'])
 segments=split_segments(joined);out['gameplay_root_segments']=[summarize_window(s) for s in segments if len(s)>1]
 game_perf=[r['perf'] for r in joined];out['per_present_wall_us']={field:stats([r[field] for r in game_perf]) for field in ['work_us','draw_us','finish_us','sync_us','vblank_us','io_us','decode_us','evict_us','audio_mix_us','audio_io_us','audio_lock_us','write_us']}
 out['resource_totals']=dict(texture_misses=sum(r['misses'] for r in game_perf),evictions=sum(r['evictions'] for r in game_perf),decoded_bytes=sum(r['decoded_bytes'] for r in game_perf),allocator_arena_free_min_bytes=min((r['heap_free'] for r in game_perf),default=None))
 windows=[];span=window_seconds*1e6
 for segment in segments:
  bins={}
  for row in segment:bins.setdefault(int((row['time_us']-segment[0]['time_us'])//span),[]).append(row)
  for rows in bins.values():
   if len(rows)>1:
    w=summarize_window(rows);w['window_complete_enough']=w['duration_seconds']>=window_seconds*.8;windows.append(w)
 out['manual_window_seconds']=window_seconds;out['gameplay_windows']=windows
 out['candidate_windows_by_logic_p95']=sorted([w for w in windows if w['window_complete_enough'] and w['logic_wall_us']],key=lambda w:w['logic_wall_us']['p95'],reverse=True)[:10]
 if identity['kind']=='benchmark':
  boot=(folder/'BOOT_D27_03.LOG').read_text(errors='replace');complete='benchmark-complete' in boot and perf[-1]['tick']>=1200 and not meta['trailing_bytes_preserved'];out['benchmark_complete']=complete
  out['matched_benchmark_windows']={f'{a}:{b}':summarize_window([r for r in joined if a<=r['source_tick']<b]) for a,b in [(240,1100),(400,1100)]}
  if(folder/'SOURCE.BIN').exists():
   import importlib.util
   spec=importlib.util.spec_from_file_location('source_profile_reader',ROOT/'work-r5/read_source_profile.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);out['source_thread_profile']=m.analyze(folder/'SOURCE.BIN')
  else:out['source_thread_profile']=None
 else:out['source_thread_profile']=None
 return out

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);p.add_argument('--environment',required=True,choices=['physical-PSP','emulator']);p.add_argument('--manifest',type=Path,default=ROOT/'delivery-r15/INSTALL_MANIFEST.json');p.add_argument('--window-seconds',type=float,default=5);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 require(not a.output.resolve().is_relative_to(a.folder.resolve()),'Write reports outside the original capture folder')
 result=analyze(a.folder,a.environment,a.window_seconds,a.manifest);a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
 print(json.dumps(dict(status=result['status'],identity=result['identity'],performance_status=result.get('performance_status'),report=str(a.output)),ensure_ascii=False))
if __name__=='__main__':
 try:main()
 except(CaptureError,FileNotFoundError) as e:raise SystemExit(str(e))
