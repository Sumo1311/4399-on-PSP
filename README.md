# PSP上的4399

## 简单介绍

这是一个用于学习与技术研究的 Flash 游戏 PSP 移植项目，支持单人游玩，以及两台 PSP 通过 WLAN 无线联机。

## 安装方式

1. 下载并解压安装包，将其中的 `PSP` 文件夹合并复制到记忆棒根目录。
2. 确认路径为 `PSP/GAME/游戏目录/EBOOT.PBP`，在支持自制程序的 PSP 上，从“游戏 → Memory Stick”启动。
3. 联机时，两台 PSP 安装相同版本并开启 WLAN，一台创建房间，另一台加入。

使用 PPSSPP 时，打开游戏目录中的 `EBOOT.PBP`。

## 协议与声明

本项目为爱好者开展的非官方移植，以非商业的学习、技术研究与交流为目的。感谢原游戏厂商、开发者与素材创作者，原作的素材、名称、商标等权利均归相应权利人所有。

原创移植实现、发布工具和文档采用 [MIT 协议](LICENSE)，不覆盖原游戏素材；依赖库保留各自协议。学习用途说明不构成原作素材的授权，也不代表原厂商或平台认可本项目。

本仓库通过 Release 提供安装包；《死神 vs 火影 3.7 Alpha》1.0.0 同时提供可独立重建的源码与核验资料，其源码许可按包内 GPL-3.0 文本及各文件声明执行。根目录 MIT 协议不覆盖另行许可的源码和原作素材。详见[协议范围](LICENSE_SCOPE.md)、[第三方声明](THIRD_PARTY_NOTICES.md)及[依赖库重新链接说明](RELINKING.md)。如权利人对相关内容有异议，可通过 [Issues](https://github.com/Sumo1311/4399-on-PSP/issues) 联系核实处理。

## 游戏下载

现有发行包括下列四款游戏的 **2026.11.13** 正式合集（采用 2026-11-11 性能优化构建），以及独立发布的《死神 vs 火影 3.7 Alpha》1.0.0。程序与配套资源按各发行版本身份保留。

[四款游戏合集](https://github.com/Sumo1311/4399-on-PSP/releases/download/v2026.11.13/Flash-to-PSP-Collection-2026.11.13.zip) · [本次正式发行](https://github.com/Sumo1311/4399-on-PSP/releases/tag/v2026.11.13) · [SHA-256 校验和](https://github.com/Sumo1311/4399-on-PSP/releases/download/v2026.11.13/SHA256SUMS)

| 游戏 | 正式安装包 |
| --- | --- |
| 森林冰火人：森林神殿 | [构建 11 优化版](https://github.com/Sumo1311/4399-on-PSP/releases/download/v2026.11.13/ForestTemple_PSP-build11.zip) |
| 勇者之路：精灵物语 | [1.0.1 优化版](https://github.com/Sumo1311/4399-on-PSP/releases/download/v2026.11.13/ElfStory_PSP-1.0.1.zip) |
| 双人旋转赛车 · Flipside | [1.0.3 优化版](https://github.com/Sumo1311/4399-on-PSP/releases/download/v2026.11.13/Flipside_PSP-1.0.3.zip) |
| 火柴人羽毛球 2 | [1.0.18 优化版](https://github.com/Sumo1311/4399-on-PSP/releases/download/v2026.11.13/Badminton2_PSP-1.0.18.zip) |
| 死神 vs 火影 3.7 Alpha（5DPLAY） | [1.0.0 独立正式包](https://github.com/Sumo1311/4399-on-PSP/releases/tag/bvn37-v1.0.0) |

四款完整 EBOOT.PBP 均已从保留源码重建并逐字节核对。实际 PSP 帧率和两台真机 WLAN 仍需分别验证，正式版本身份不代替设备测试。

[构建与重新链接材料](https://github.com/Sumo1311/4399-on-PSP/releases/download/v2026.11.13/Relink-Materials-2026.11.13.zip)包含本次程序对应的对象文件、链接参数、修改源码和线程库对应源码。

[发行版本页面](https://github.com/Sumo1311/4399-on-PSP/releases)

死神 vs 火影 1.0.0：76 个可选形态、2 个内部形态、42 名援助、11 种无双杂兵、19 张场景，默认简体中文，不添加观战模式。模拟器资源、操作、存档及双实例 WLAN 检查已通过；实体 PSP 和完整原版细节仍有待验证项，详见发行页的核验报告与已知边界。
