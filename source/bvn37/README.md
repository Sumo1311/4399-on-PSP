# 死神 vs 火影 3.7 Alpha PSP 1.0.0 hwd2 整合正式版

对应 [完整游戏安装包](https://github.com/Sumo1311/4399-on-PSP/releases/download/bvn37-v1.0.0/BVN37_PSP_1.0.0_Install.zip)。本版已整合 HWD2 补丁，并默认使用用户在实体 PSP-2000 验证通过的同步素材读取方式，启动即为 STDIO ONLY。封面、背景、简体中文和原有模式保留，观战入口继续排除。

市丸银待机动画中 9 个透明姿态已从原始图形恢复，原先正常的 3 个姿态和所有攻击、碰撞、命令、事件及时序保持一致；保留旧版迪达拉的待机/起跳修复。用户已反馈补丁后游戏正常、此前发现的问题均已修复，并授权合并正式发布。

## 重建

需要 PSPDEV、Python 3、CMake；参考工具链为 PSP GCC 15.2.0、CMake 3.31.10。

```sh
export PSPDEV=/path/to/pspdev
export PATH="$PSPDEV/bin:$PATH"
python3 build.py --install-zip /path/to/BVN37_PSP_1.0.0_Install.zip --require-identical
```

从完整包逐项校验并读取 200 个数据/配置文件，从本目录源码编译 EBOOT，共生成 201 个安装文件。新增的空文件 BVN_SYNC.IO 固定已验证的默认同步读取方式。其他兼容工具链可省略 --require-identical，数据文件始终逐项验证。build.py 仍支持通过 --patch-zip 读取历史补丁作重建输入。

游戏程序 DATA.PSP 与用户验证通过的 HWD2 补丁完全一致；正式包装恢复常规 PSP 菜单标题，并在完整目录内加入默认读取标记。游戏标题菜单仍可按 R 手动切换读取方式，重启后恢复同步默认值。

修复细节见 HWD2.md，源图与字节对比证据在 repair/gin_idle/，用户实机反馈范围见 USER_ACCEPTANCE.json，其他原有验证边界见 KNOWN_LIMITS.md。

源码遵循 LICENSE.txt 的 GPL-3.0 文本及各文件、第三方声明；游戏素材与角色权利归各自所有者。原作固定基准与工具链见 reference/manifest.json 和 TOOLCHAIN.json。
