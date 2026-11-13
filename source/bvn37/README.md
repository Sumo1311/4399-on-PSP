# 死神 vs 火影 3.7 Alpha PSP HWD2 补丁源码

本分支对应 [HWD2 针对性补丁](https://github.com/Sumo1311/4399-on-PSP/releases/download/bvn37-v1.0.0/BVN37_PSP_HWD2_TargetedPatch.zip)，需要已安装 hwd1 完整游戏。主分支继续保留正式 hwd1 的对应源码。

HWD2 恢复市丸银待机动画中被导出为全透明的 9 个姿态，保留原先正常的 3 个姿态和全部战斗数据。新增标题菜单按 R 切换原预读取/同步 stdio 的入口，并细分读取失败步骤、短读信息和失败文件的复读 CRC 校验。实体 PSP-2000 的 NARUTO1.BVF / ERR=0 故障尚未确认解决，仍需实机复测。

使用 PSPDEV、CMake、Python 3 重建：

```sh
export PSPDEV=/path/to/pspdev
export PATH="$PSPDEV/bin:$PATH"
python3 build.py --install-zip /path/to/BVN37_PSP_1.0.0_Install.zip \
  --patch-zip /path/to/BVN37_PSP_HWD2_TargetedPatch.zip --require-identical
```

工具将补丁素材覆盖到完整包的数据来源上，再逐项校验 199 个数据文件，从本分支源码编译 EBOOT，生成全部 200 个安装文件。若输入已经是完整 HWD2 包，可省略 --patch-zip。参考工具链为 PSP GCC 15.2.0、CMake 3.31.10，其他兼容工具链可省略 --require-identical。

HWD2 说明见 [HWD2.md](HWD2.md)，源图及修复证据在 repair/gin_idle/。许可见 LICENSE.txt 及各文件声明；仍排除观战模式，其他原有验证边界见 KNOWN_LIMITS.md。
