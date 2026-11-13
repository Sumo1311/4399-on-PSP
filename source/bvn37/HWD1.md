# 1.0.0 hwd1 修订

本构建已按用户要求替换 bvn37-v1.0.0 的正式安装包；保留 HWD1 实机测试标记及诊断能力。构建、安装文件和哈希由 CONTENT_MANIFEST.json 固定，build.py 会启用相同编译选项。

旧版迪达拉的 Idle 和 JumpStart 使用原始 sprite 180。该动画同帧同深度的有效图形放置后，有指向动画自身的异常重复放置，旧导出器清空该层，导致每段 12 个相位中的 6 个全透明。FlashLayers 只在同帧同深度已有真实有效放置时忽略异常 self-placement，保留图形、变换和时间信息，原始固定版本 SWF 不改动。

本次只替换父帧 1 和 45 对应的 24 个视觉姿态。原先显示正常的 12 个姿态像素及定位完全相同；攻击、碰撞、事件、命令与片段时序保持一致；所有旧贴图记录及压缩字节保留。新元数据签名 e1d2ef18e98e8e69 绑定到旧签名 7466edc65decc5a3 的同一份 ObjectAreas 几何。

源导出图形、时间轴及字节对比证据在 repair/deidara_old/。重建完整安装包使用 README 的 build.py 命令；独立重生成修复银行使用：

```sh
python3 tools/repair_legacy_held_visuals.py /path/to/xmb1/DEIDARAO.BVF \
  repair/deidara_old /path/to/output/DEIDARAO.BVF \
  --object-area-header src/object_area_data.hpp
```

工具仅接受固定哈希的 xmb1 旧银行，保留原文件，并验证战斗数据不变。默认安装包重建直接验证 Release ZIP 的新银行，不要求保留旧银行。

正常启动保留原有预读取方式，标题显示 HWD1: ORIGINAL IO。启动时按住 R，或在 EBOOT.PBP 同目录放置空文件 BVN_SYNC.IO，可切换为 HWD1: STDIO ONLY。后者是定位对照，不代表已确定实机错误原因。

资源失败弹窗增加文件名及错误码，最多记录 16 次失败到 BVN_DIAG.LOG，包含文件状态和失败时内存；每次启动覆盖日志，成功的普通战斗帧不写日志。退出后先保存日志再重新启动。

PPSSPP PSP-1000、默认时钟、原始分辨率、不跳帧：最后迁移的 12 个形态搭配 15 个新增援助，12 组正常入场及 3 组同步读取检查通过；缺失英雄故障样例准确报告 URYUUG.BVF、errno 2。实体 PSP 的读取故障仍待复测，完整边界见 KNOWN_LIMITS.md。
