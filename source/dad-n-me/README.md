# 狂扁小朋友 PSP 1.0.3 正式版

用户指定最新完整设备测试版 R24 作为正式版，加入 XMB 中文名称、封面和背景。游戏 DATA.PSP／DATA.PSAR 及全部运行资源与 R24 保持逐字节一致。R27／R28 为其他尚未交付的开发试验，未纳入本版。

## 重建

需要 pspdev-v20260701（GCC 15.2.0／PSPSDK）及 CMake 3.31.10、Python 3。源码、固定运行素材和依赖均已包含。

```sh
python3 build.py --pspdev /path/to/pspdev --cmake /path/to/cmake
```

输出 dist/PSP/GAME/DadNMe。SOURCE_DATE_EPOCH=1794355200、LC_ALL=C。先使用原 R24 参数重建字节相同的基准程序，再使用 tools/repack_xmb.py 嵌入本目录 artwork，核对正式 EBOOT 及资源。candidate_build.py 和 CANDIDATE.json 保留基准构建入口。

## 证据与范围

TESTED_IDENTITY.json、qa 及 provenance 保留 R24 的源文件/素材身份、原有行为回归和性能记录。正式包的启动与封装检查另见 VERIFICATION.json。尚未达到已验证的全程实机60帧，复杂战斗仍可能顿帧、迟滞；部分无法移动原因的实机证明未完成。正式版身份不扩大既有验证范围。

原图和内置 imagegen 完整提示词在 artwork/ARTWORK.json。原作及依赖的许可、署名分别保留于 packaging、vendor、reference、pipeline。安装包不含个人存档或自动回放开关，游戏不保存中途关卡进度。
