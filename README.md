# PSP 上的 4399

Flash 游戏的非官方 PSP 移植项目，支持单人游玩，部分游戏支持两台 PSP 通过 WLAN 联机。

## 游戏下载

每款游戏单独发布一条 Release，每条只保留该游戏的完整正式安装包。

| 游戏 | 发行页 | 完整安装包 |
| --- | --- | --- |
| 森林冰火人：森林神殿 | [发行页](https://github.com/Sumo1311/4399-on-PSP/releases/tag/forest-temple-build11) | [构建 11 优化版](https://github.com/Sumo1311/4399-on-PSP/releases/download/forest-temple-build11/ForestTemple_PSP-build11.zip) |
| 勇者之路：精灵物语 | [发行页](https://github.com/Sumo1311/4399-on-PSP/releases/tag/elf-story-v1.0.1) | [1.0.1 优化版](https://github.com/Sumo1311/4399-on-PSP/releases/download/elf-story-v1.0.1/ElfStory_PSP-1.0.1.zip) |
| 双人旋转赛车 · Flipside | [发行页](https://github.com/Sumo1311/4399-on-PSP/releases/tag/flipside-v1.0.3) | [1.0.3 优化版](https://github.com/Sumo1311/4399-on-PSP/releases/download/flipside-v1.0.3/Flipside_PSP-1.0.3.zip) |
| 火柴人羽毛球 2 | [发行页](https://github.com/Sumo1311/4399-on-PSP/releases/tag/badminton2-v1.0.18) | [1.0.18 优化版](https://github.com/Sumo1311/4399-on-PSP/releases/download/badminton2-v1.0.18/Badminton2_PSP-1.0.18.zip) |
| 死神 vs 火影 3.7 Alpha（5DPLAY） | [发行页](https://github.com/Sumo1311/4399-on-PSP/releases/tag/bvn37-v1.0.5) | [1.0.5 角色闪烁与大招修复正式版](https://github.com/Sumo1311/4399-on-PSP/releases/download/bvn37-v1.0.5/BVN37_PSP_1.0.5_Install.zip) |
| 战争进化史2 | [发行页](https://github.com/Sumo1311/4399-on-PSP/releases/tag/age-of-war-2-c10) | [C10 正式版](https://github.com/Sumo1311/4399-on-PSP/releases/download/age-of-war-2-c10/AgeOfWar2_PSP_C10_Official.zip) |

## 安装

1. 解压安装包，将 `PSP` 文件夹合并复制到记忆棒根目录。
2. 确认路径为 `PSP/GAME/游戏目录/EBOOT.PBP`，从“游戏 → Memory Stick”启动。PSP 系统需支持运行自制程序。
3. 更新前备份个人存档。联机时，两台 PSP 使用相同版本并开启 WLAN。

PPSSPP 可直接打开游戏目录内的 `EBOOT.PBP`。

四款优化版采用 2026-11-11 的定版构建。《死神 vs 火影》1.0.5 修复宁次等角色的异常透明动作帧、重吾及李洛克的大招落地循环；保留清晰度及原混合图层修复。默认简体中文，保留 76 个可选形态、42 名援助和 19 张场景，排除无双与观战。已完成源码重建、素材与模拟器回归，新增实体 PSP 验收范围见源码说明。

![死神 vs 火影 PSP 封面](source/bvn37/artwork/cover-master.png)

## 源码与构建资料

- [死神 vs 火影原生源码、封面背景与重建说明](source/bvn37)
- [战争进化史2 C10 原生源码及构建说明](source/age-of-war-2-c10)
- [四款优化版的构建与重新链接资料](materials)
- [当前独立发行清单与 SHA-256](manifests/releases.json)
- [历史版本与构建清单](manifests)

## 协议与声明

本项目以非商业的学习、技术研究与交流为目的。原游戏素材、名称、角色与商标权利归各自权利人所有。

原创移植实现、工具和文档按 [MIT 协议](LICENSE) 提供；死神 vs 火影源码按其目录内 GPL-3.0 文本及各文件声明执行。原作素材及第三方依赖保留各自权利和许可证。详见 [协议范围](LICENSE_SCOPE.md)、[第三方声明](THIRD_PARTY_NOTICES.md)及 [重新链接说明](RELINKING.md)。如需核实相关内容，可通过 [Issues](https://github.com/Sumo1311/4399-on-PSP/issues) 联系。
