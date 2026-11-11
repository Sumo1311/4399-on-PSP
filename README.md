# Flash to PSP · 成品发行库

为支持自制程序的 PSP 提供 Flash 游戏的原生移植安装包。

**[下载最新安装包](https://github.com/Sumo1311/Flash-to-PSP-Releases/releases/latest)**

| 游戏 | 本次版本 | 安装目录 | 下载 |
| --- | --- | --- | --- |
| 勇者之路：精灵物语 | 1.0.1 | `ElfStory_PSP` | [独立安装包](https://github.com/Sumo1311/Flash-to-PSP-Releases/releases/download/v2026.11.11/ElfStory_PSP-1.0.1.zip) |
| 森林冰火人：森林神殿 | 交付构建 11 | `ForestTemple_PSP` | [独立安装包](https://github.com/Sumo1311/Flash-to-PSP-Releases/releases/download/v2026.11.11/ForestTemple_PSP-build11.zip) |
| 双人旋转赛车 · Flipside | 1.0.3 | `Flipside_PSP` | [独立安装包](https://github.com/Sumo1311/Flash-to-PSP-Releases/releases/download/v2026.11.11/Flipside_PSP-1.0.3.zip) |
| 火柴人羽毛球 2 | 1.0.18 | `Badminton2_PSP` | [独立安装包](https://github.com/Sumo1311/Flash-to-PSP-Releases/releases/download/v2026.11.11/Badminton2_PSP-1.0.18.zip) |

也可以下载 [四款合集](https://github.com/Sumo1311/Flash-to-PSP-Releases/releases/download/v2026.11.11/Flash-to-PSP-Collection-2026.11.11.zip)。全部安装包均包含 XMB 封面和背景图。

森林神殿保留已交付构建的原始版本信息 `APP_VER=00.82`；“构建 11”是交付编号，没有将其改为新的语义版本。飞鸟骑士和两个《死神 vs 火影》项目不在本次发行范围内。

## 安装

1. 解压所选安装包，将里面的 `PSP` 文件夹合并复制到记忆棒根目录。
2. 确认程序路径为 `PSP/GAME/游戏目录/EBOOT.PBP`。
3. 在 PSP 的“游戏 → Memory Stick”中启动。使用 PPSSPP 时打开相应的 `EBOOT.PBP`。

升级时一起替换同一游戏目录内的程序和数据文件。每款游戏目录中的 `INSTALL.txt` 提供操作说明。单台 PSP 使用单人模式；WLAN 双人模式需要两台 PSP 安装相同版本并开启 WLAN。

这批发行包的程序和游戏数据与交付包比对一致，补充或保留了封面、背景图。发布校验覆盖 ZIP 完整性、安装层级、PBP 结构、版本信息与文件哈希。本次发布没有重新进行游戏通关或 PSP 真机性能验收，模拟器记录也不代替两台真机 WLAN 验证。

## 协议与素材

[MIT](LICENSE) 适用于自行编写的移植实现、发布工具和文档，具体范围见 [LICENSE_SCOPE.md](LICENSE_SCOPE.md)。原 Flash 的画面、声音、角色、名称、关卡及其他第三方内容保留原权利人的权利，**不随移植代码改为 MIT**。依赖库沿用各自协议，完整声明随安装包提供，见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

本仓库用于发行安装包、清单和校验工具，本次未上传各游戏的完整工程源码。MIT 许可不等于原游戏素材的开放许可，也不代表原作者认可或授权本移植项目。

SDK 使用的 pthread-embedded 的 LGPL 源码及可重新链接的对象文件，随同一 Release 的 `Relink-Materials-2026.11.11.zip` 提供。[重新链接说明](RELINKING.md) 说明如何替换线程库和生成新的程序。

## 校验下载

Release 提供 `SHA256SUMS` 和 `Release_Manifest.json`。可以在下载目录执行 `shasum -a 256 -c SHA256SUMS`；Windows 可使用 PowerShell 的 `Get-FileHash -Algorithm SHA256`。

仓库中的校验工具还能检查安装层级、PBP 和封面尺寸。

```sh
python3 tools/verify_release.py /path/to/ElfStory_PSP-1.0.1.zip
```

包内容及每个文件的 SHA-256 见 [发行清单](manifests/release-2026.11.11.json)。二进制安装包通过 Releases 分发。
