# 战争进化史2 PSP 正式版 C10

本目录是用户于2026-11-14确认发布的C10原生源码。完整安装包与运行资源见本仓库 `age-of-war-2-c10` Release。

正式包游戏程序、XMB图像和运行素材与实机已使用的C10一致，只把PARAM.SFO中的TITLE改为“战争进化史2”，并整理发布说明与清单。正式EBOOT的DATA.PSP SHA-256为 `e387d128026099eb48f6de9a5b7f4f039c14ed8d8e595c043477477e3051382e`。

自动内存选择发生在libc及全局构造之前，按最大连续空闲内存预留1MiB。资源缓存按堆容量选择档位；受限内存预载实际引用的43种音效，相同左右声道只保留一份驻留并还原原样本。保留指令缓存边界修复、原生清晰字体及音效限幅。规则仍基于1.0.3；另一路1.0.4炮塔玩法改动未合入本包。

## 构建

使用PSPDEV GCC15.2.0和CMake，安装工具链依赖lz4、zlib及PSPSDK库。构建参数：

```sh
export SOURCE_DATE_EPOCH=1794355200
export LC_ALL=C
psp-cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
cmake --build build -j2
python3 tools/retitle_pbp.py build/EBOOT.PBP build/EBOOT.formal.PBP
```

CMake保留C10测试时的原始XMB标题以重建相同程序；最后一步只更新标题，形成正式PBP。数据目录从正式安装包的 `PSP/GAME/AgeWar2/data` 复制，不包含个人存档。将正式PBP命名为 `EBOOT.PBP` 并与该数据目录放在同一安装目录。

`identity.json` 记录当前源码及构建参数；`provenance` 内是明确标注的历史来源清单。`VERIFICATION.json` 区分用户实机反馈、原C10模拟器检查和正式安装目录检查。用户反馈当前未发现问题并确认发布；没有新增全机型、全场景或实机帧间隔测量结论。

新平台适配代码与原作派生数据、画面、音乐及字体的权利范围分别说明于 `LICENSE_SCOPE.md` 和 `THIRD_PARTY_NOTICES.md`。本目录不为原作素材授予新的许可。
