# 死神 vs 火影 3.7 Alpha PSP 1.0.0 hwd1 源码

对应 [bvn37-v1.0.0 安装包](https://github.com/Sumo1311/4399-on-PSP/releases/download/bvn37-v1.0.0/BVN37_PSP_1.0.0_Install.zip)。全部原生 C/C++ 源码、生成的编译输入、测试、构建与提取工具、许可证及封面／背景均保存在本目录。原作基准与固定提交见 `reference/manifest.json`，工具链版本见 `TOOLCHAIN.json`。

Release 附件仅提供游戏安装包；构建直接从安装包读取并逐项校验 199 个游戏素材／数据文件，再从本目录源码编译游戏程序，生成全部 200 个安装文件。

## 重建

需要 PSPDEV（PSPSDK、zlib、LZ4）、Python 3、CMake。参考工具链为 PSP GCC 15.2.0、CMake 3.31.10。

```sh
export PSPDEV=/path/to/pspdev
export PATH="$PSPDEV/bin:$PATH"
python3 build.py --install-zip /path/to/BVN37_PSP_1.0.0_Install.zip --require-identical
```

输出为 `build/install/PSP/GAME/BVN37/`。可通过 `--build-dir`、`--output` 指定其他目录，通过 `CMAKE`、`BUILD_JOBS` 环境变量指定 CMake 和并行数。其他兼容编译器可省略 `--require-identical`；游戏数据始终逐项验证。

`artwork/` 包含高清封面与背景、144×80 图标、480×272 菜单背景及完整生成提示词，CMake 自动嵌入最终 PNG，重建无需在线生图。本次 hwd1 修订恢复旧版迪达拉待机与起跳的原始图形，并加入实机资源失败定位。XMB 标题为 `BVN37 HWD1 PSP Test`，用于识别这次构建；当前正式 Release 按用户要求提供此构建。观战模式继续排除。实体 PSP、实体双机 WLAN 和完整原版细节的边界见 `KNOWN_LIMITS.md`。

源码遵循 `LICENSE.txt` 的 GPL-3.0 文本及各文件、第三方声明；原游戏素材与原角色权利归各自所有者，仓库根目录 MIT 不重新授权这些内容。完整原作工程分别见 `reference/manifest.json` 指定的上游仓库与固定提交。

修订内容、诊断使用方法和银行修复依据见 [HWD1.md](HWD1.md)。实体 PSP 的读取失败仍待复测，PPSSPP 测试不能代替实机结果。
