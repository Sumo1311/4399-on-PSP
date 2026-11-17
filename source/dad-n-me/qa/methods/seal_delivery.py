from pathlib import Path
import json, hashlib, shutil, zipfile, struct, datetime

R=Path(__file__).resolve().parents[1]
T=R/'work-r24'; S=R/'source-delivery/DadNMeResponseR24'; D=R/'delivery-r24'
read=lambda p:json.loads(p.read_text())
def save(p,d):p.write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n')
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

checks=['SOURCE_SCOPE_CHECK.json','ENDPOINT_COMPARISON.json','FINAL_LEGACY_ROUTE_CHECK.json',
        'NATIVE_HEALTH_CHECK.json','FINAL_CONTROLS.json','INTERPOLATION_PIXEL_CHECK.json',
        'NATIVE_INTRO_INTERRUPTION_CHECK.json','QA_PROGRESS.json']
for n in checks:assert read(T/n)['status']=='pass',n
assert read(R/'work-r23/SOURCE_DIFFERENTIAL.json')['status']=='pass'
assert read(R/'work-r23/HOST_UNIT_CHECKS.json')['status']=='pass'
rebuild=read(S/'REBUILD.json')
assert all(rebuild[k] for k in ['byte_identical_to_tested','native_sources_unchanged','cmake_unchanged','assets_unchanged'])
identity=read(S/'TESTED_IDENTITY.json')
assert all(sha(S/n)==h for n,h in identity['source_files'].items())
ring=read(T/'R24_RAM_ONLY_PACING.json')
assert ring['capture']['read_failures']==ring['capture']['overruns']==ring['capture']['discarded_records']==0
assert not ring['diagnostic']
perf=read(T/'PERFORMANCE_COMPARISON.json')
assert not D.exists(); D.mkdir(); Q=S/'qa'; Q.mkdir()
for n in checks+['RECORDED_MOTION_CHECK.json','BASELINE_ENDPOINT_OBSERVATION.json','R24_RAM_ONLY_PACING.json',
 'PERFORMANCE_COMPARISON.json','r23-EMULATOR_BENCHMARK.json','r24-EMULATOR_BENCHMARK.json',
 'r23-EMULATOR_SOURCE_PROFILE.json','r24-EMULATOR_SOURCE_PROFILE.json','benchmark-package-check.json',
 'diagnostic-package-check.json','EXPERIMENT.json']:
    shutil.copy2(T/n,Q/n)
for n in ['SOURCE_DIFFERENTIAL.json','HOST_UNIT_CHECKS.json','OWNERSHIP_BYTECODE_EVIDENCE.json']:
    shutil.copy2(R/'work-r23'/n,Q/n)
methods=Q/'methods'; methods.mkdir()
for n in ['qualify_native.py','benchmark_pair.py','run_ram_only.py','export_source.py','prepare_bench_packages.py','prepare_diag_package.py','seal_delivery.py']:
    shutil.copy2(T/n,methods/n)
for folder,n in [('work-r23','qualify_source.py'),('work-r23','read_ownership_bytecode.py'),
 ('work-r20','ram_replay_driver.mjs'),('work-r15','analyze_device_capture.py'),('work-r4','analyze_bench_pair.py'),
 ('work-r5','read_source_profile.py'),('work-r9','state_audit.cpp')]:shutil.copy2(R/folder/n,methods/n)
fixtures=Q/'fixtures'; fixtures.mkdir()
for folder,n in [('work-r7','legacy-source.swf'),('work-r8','intro-interruption-qa.swf'),
 ('work-r7','health-and-restart-5000.dnm'),('work-r4','device-benchmark-1200.dnm'),('work-r12','dense6000.dnm')]:
    shutil.copy2(R/folder/n,fixtures/n)
shutil.copytree(R/'work-r21/differential-inputs',fixtures/'control-inputs')
save(fixtures/'IDENTITY.json',{str(p.relative_to(fixtures)):dict(bytes=p.stat().st_size,sha256=sha(p))
    for p in fixtures.rglob('*') if p.is_file()})
for label,run in [('legacy-route','response56-r24-legacy-full-diagnostic'),
 ('normal-health','response56-r24-health-333-normal'),('normal-controls','response56-r24-controls-333-normal'),
 ('interpolation','response56-r24-pair-333-normal'),('interruption','response56-r24-interrupt-diagnostic'),
 ('benchmark-r23','response55-r23-r24baseline-333-normal'),('benchmark-r24','response56-r24-333-normal'),
 ('ordinary6000','response56-r24-ramonly6000-333-normal')]:
    src=R/'source-measure/runs'/run; g=src/'MemoryStick/PSP/GAME/DadNMe'; dest=Q/label; dest.mkdir()
    for n in ['emulator-test-profile.json','ppsspp-test.ini','replay-result.json','prefix-result.json',
     'benchmark-result.json','response-controls.json','ram-replay-result.json','presentation-ring.csv',
     'presentation-ring.json','ring-qualification.json','input-events.jsonl']:
        if (src/n).is_file():shutil.copy2(src/n,dest/n)
    for n in ['ENGINE.CSV','SOURCE.BIN','PERF.BIN','PRESENT60.CSV','MID60.RGBA','EXACT60.RGBA',
     'CONTROL00.CSV','CONTROL00.TXT','BOOT_D27_03.LOG']:
        if (g/n).is_file():shutil.copy2(g/n,dest/n)
    if label=='legacy-route':
        for p in g.glob('FRAME-*.RGBA'):shutil.copy2(p,dest/p.name)
for folder,package in [('DadNMeResponseR24','response56'),('DadNMeR24Bench','response56-r24bench'),('DadNMeR24Diag','response56-r24diag')]:
    dest=D/'install/PSP/GAME'/folder; shutil.copytree(R/'package'/package,dest)
    if folder.endswith('Bench'):
        (dest/'BENCHMARK.QA').write_text('Fixed1200-frame benchmark,auto-exit\n')
        (dest/'PERF.QA').write_text('60000\n')
        shutil.copy2(R/'work-r4/device-benchmark-1200.dnm',dest/'REPLAY.DNM')
    elif folder.endswith('Diag'):(dest/'PERF.QA').write_text('36000\n')
    (dest/'SHA256SUMS').write_text(''.join(sha(p)+'  '+p.name+'\n' for p in sorted(dest.iterdir())
      if p.is_file() and p.name not in ['SHA256SUMS','build-identity.json']))
    d=read(dest/'build-identity.json'); d['source_files']=identity['source_files']
    d.update(resources={p.name:sha(p) for p in dest.iterdir() if p.is_file() and p.name!='build-identity.json'},
        status='behavior-regression-passed-physical-performance-pending',scope=identity['scope'])
    save(dest/'build-identity.json',d)
manifest={p.name:{f.name:dict(bytes=f.stat().st_size,sha256=sha(f)) for f in sorted(p.iterdir()) if f.is_file()}
          for p in (D/'install/PSP/GAME').iterdir()}
save(D/'INSTALL_MANIFEST.json',manifest)
for p in [Q/'INSTALL_MANIFEST.json',D/'install/INSTALL_MANIFEST.json']:shutil.copy2(D/'INSTALL_MANIFEST.json',p)
a=perf['phase_us']['R23']; b=perf['phase_us']['R24']
report=f'''# 狂扁小朋友 Response R24 测试候选

本候选修复抓取时两处状态类型偏差，并降低绘制命令生成开销。软件回归通过；实机正常速度、输入延迟和全部偶发无法移动问题仍未验收，完整移植目标保持未完成。8 GB 卡最后写入并重新挂载校验的版本为 R20，本包尚未写卡。

## 改动及来源

原 SWF 的 f_Grab 在清空武器引用时通过 ActionGetVariable 读取变量 NULL。R20 加速代码将其中两处写成字面 null，导致第 7151 源帧的敌人 item 和武器 owner 类型与原脚本不同。R23/R24 在原执行位置解析该变量。复现路线、六组控制/战斗输入及完整路线合计 115279 源帧，全部显示树成员、引用标识、矩阵、帧号与 RNG 摘要逐字节匹配原 AVM1 解释执行。此差异已定位和修复，但没有证据证明它就是用户实机永久不能移动的根因。该检查未序列化整个非显示堆，也不是独立 Flash 播放器验证。

R24 对绘制矩阵与当前世界矩阵逐位相同的路径直接使用已提供的上一源帧矩阵，避免矩阵求逆及重复拼接；局部变换、奇异矩阵保留原路径。记录器增加两份矩阵和少量标志，没有对象池或增长缓存。旧算法的舍入噪声被源端点替代，53905 项检查覆盖随机变换、静态矩阵、奇异矩阵、局部变换和新录制重置，源端点零差异，回退结果与旧实现一致。

没有修改源 SWF、AI、物理、随机次序、伤害或逻辑步长。原作维持 30 Hz 逻辑，显示目标约 59.94 Hz。衣帽显示、角色与头像无火光、敌人短闪消失、背景边缘、正常100HP受伤和 HOME 退出路径继续保留。

## 正常速度对照

R23 与 R24 同一1200帧输入，独立冷启动，PSP型号1、333 MHz、480×272、正常1倍、无跳帧，统计源帧240至1099：

|阶段|R23平均ms|R24平均ms|减少|
|---|---:|---:|---:|
|逻辑|{a['logic']['mean']/1000:.3f}|{b['logic']['mean']/1000:.3f}|{perf['reduction_percent']['logic']:.2f}%|
|绘制命令生成|{a['capture']['mean']/1000:.3f}|{b['capture']['mean']/1000:.3f}|{perf['reduction_percent']['capture']:.2f}%|
|源线程合计段|{a['total']['mean']/1000:.3f}|{b['total']['mean']/1000:.3f}|{perf['reduction_percent']['total']:.2f}%|
|合计p95|{a['total']['p95']/1000:.3f}|{b['total']['p95']/1000:.3f}|—|

这些是模拟器虚拟 PSP 墙钟，包含等待和抢占。固定333 MHz不复现真实 PSP 的 CPU 缓存、总线和全部 GE 开销；不能据此宣布实机达标。原生帧率与输入到画面延迟仍需设备采样。

6000源帧正常速度混战测试采用已有RAM环，不连续向目标存储写性能文件。240至5899源帧中，呈现 {ring['presentation_hz']:.5f} Hz，新变换 {ring['fresh_transform_hz']:.5f} Hz，重复画面对 {ring['repeated_pairs']} 次，位置 {json.dumps(ring['held_source_ticks'])}；漏显示间隔 {ring['missed_display_intervals']}。{ring['capture']['records']} 条已观测记录无溢出、丢弃或读取失败，判定区间完整；退出前最后一条提交未必被宿主采到。流畅度判定仍为 {ring['status']}，不能将刷新率视为全部新画面。

## 回归与身份

- MIPS原生完整41019源帧，492228个CSV值和23张关键画面与基准一致，含首领、结局及重开；该项含诊断快进，只验证正确性。
- 正常100HP资源在正常1倍下5000源帧受伤、死亡、失败及重开一致；普通按键移动、攻击、暂停恢复、手动控制快照通过。
- 正常速度插值画面对照和出场中断故障夹具通过。特定出场守卫不代表所有无法移动原因已解决。
- R23基础快照、成员查找、帧事件、脚本值及存活性检查通过；R24引擎文件与R23相同，只修改记录器及版本/测试配置。
- 图像、音频及正常SWF与R20逐字节相同；完整源码独立重建与测试EBOOT逐字节相同，三个安装入口的DATA.PSP执行段一致。普通入口没有自动回放或性能标记。

普通 EBOOT SHA256：{identity['resources']['EBOOT.PBP']}。

最后可用实机性能记录仍为R7约13.1248次逻辑更新/秒。之后R20/R24的设备表现不能从模拟器外推。本包仅为本地可复核候选，未进行GitHub发布。
'''
for p in [D/'优化报告.md',D/'install/优化报告.md',S/'验证报告.md']:p.write_text(report)
notes='''狂扁小朋友 Response R24 测试候选

将 PSP 目录复制到8GB卡根目录，保留旧版和存档。
1. Dad n Me R24 Bench：固定输入采样，结束自动返回系统。
2. Dad n Me Response R24 Test：正常游玩，检查多人混战速度、受击和抓取后移动、HOME退出。
3. Dad n Me R24 Diag：复现问题1至2分钟后HOME返回；日志写入会增加额外开销。
若角色不能移动，按L+R+SELECT约一秒，松开后等两秒，再HOME返回。此组合仅保存状态。
请保留BOOT、PERF.BIN、PRESENT60.CSV和CONTROL文件；普通版性能反馈与诊断版采样分别记录。

软件回归通过，真机性能与全部偶发无法移动原因尚未验收。本包尚未写卡。
'''
(D/'install/安装与测试说明.txt').write_text(notes)
(S/'README.md').write_text('''# 狂扁小朋友 Response R24

PSP-2000/3000本地测试候选。原作流程的软件回归已通过，复杂战斗实机速度、输入延迟和全部偶发无法移动原因尚未验收。详见验证报告.md和qa。

方向键/摇杆移动，×轻攻击、□重攻击、START暂停，结局或失败界面×重开，HOME返回系统。原作30Hz逻辑，约59.94Hz插值显示目标。

构建：`python3 build.py --pspdev /path/to/pspdev --cmake /path/to/cmake --build-dir /path/to/output`。
TESTED_IDENTITY.json绑定源码、素材和测试程序，REBUILD.json记录独立重建。素材完整随附，使用GCC15.2/PSPSDK兼容工具链。qa方法中的工作区路径需调整；PPSSPP QA应用另外提供。qa/fixtures的高血量和故障SWF不属于普通安装内容。
''')
info=read(S/'CANDIDATE.json');info.update(status='behavior-regression-passed-performance-goal-open',
    physical_performance_verified=False,full_migration_acceptance=False);save(S/'CANDIDATE.json',info)
files=0; native=None
for folder,entries in manifest.items():
    g=D/'install/PSP/GAME'/folder
    for n,v in entries.items():assert (g/n).stat().st_size==v['bytes'] and sha(g/n)==v['sha256'];files+=1
    flags={p.name for p in g.glob('*.QA')}
    assert flags==({'BENCHMARK.QA','PERF.QA'} if folder.endswith('Bench') else {'PERF.QA'} if folder.endswith('Diag') else set())
    assert (g/'REPLAY.DNM').exists()==folder.endswith('Bench')
    pbp=(g/'EBOOT.PBP').read_bytes(); offsets=struct.unpack_from('<8I',pbp,8); data=pbp[offsets[6]:offsets[7]]
    if native is None:native=data
    else:assert native==data
assert files==40,files
archives={}
for name,folder,root in [('狂扁小朋友_Response_R24_设备测试.zip',D/'install',False),('狂扁小朋友_Response_R24_完整源码.zip',S,True)]:
    p=D/name; expected={str(f.relative_to(folder.parent if root else folder)):f for f in folder.rglob('*') if f.is_file() and '__pycache__' not in f.parts}
    with zipfile.ZipFile(p,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for n,f in sorted(expected.items()):z.write(f,n)
    with zipfile.ZipFile(p) as z:
        assert z.testzip() is None; assert set(z.namelist())==set(expected)
        for n,f in expected.items():assert hashlib.sha256(z.read(n)).hexdigest()==sha(f),n
    archives[name]=dict(bytes=p.stat().st_size,sha256=sha(p))
save(D/'PACKAGE_HASHES.json',archives)
sealed=dict(status='pass',scope='Candidate identity and behavior verification;performance goal open',
    runtime_files=files,source_identity_files=len(identity['source_files']),all_runtime_hashes_match=True,
    normal_has_no_replay_or_perf_flags=True,all_three_native_sections_identical=True,
    source_rebuild_byte_identical=True,archives_full_member_sha256_verified=True,
    archives=archives,card_written=False,hardware_verified=False)
save(T/'SEALED_DELIVERY.json',sealed)
pointer=dict(candidate='response56',label='R24',source=str(S),delivery=str(D),
    ordinary_eboot_sha256=identity['resources']['EBOOT.PBP'],archives=archives,
    behavior_regression='pass',performance=ring['status']+';physical unmeasured',
    card_written=False,physical_verified=False,sealed_at=datetime.datetime.now().isoformat())
save(R/'LATEST_R24_DELIVERY.json',pointer);save(T/'DELIVERY_COMPLETE.json',pointer)
print(json.dumps(sealed,ensure_ascii=False,indent=2))
