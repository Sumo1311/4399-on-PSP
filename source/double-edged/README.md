# 双刃战士 PSP 1.0.0 正式版（单机）

以 B155 单机修复版为定版程序。用户于2026-11-17认可当前性能并要求制作XMB封面背景后正式发布。原作逻辑仍为30Hz；现有模拟器重战斗记录有长步，正式版身份不构成全程60帧或新增全Boss真机验收结论。

封面图标、背景及完整内置imagegen提示词在 artwork。只变更 PARAM.SFO 的 TITLE／APP_VER 和 ICON0／PIC1，DATA.PSP、DATA.PSAR和运行资源逐字节保持B155。OFFICIAL_IDENTITY.json绑定正式资源；provenance保存旧候选的历史证据，不把B155 WLAN构建/复建当作玩法验收。此正式安装只含单机。

## 重建

需要 PSPDEV GCC15.2.0／PSPSDK 与 CMake3.31.10。完整 vendor 和运行素材已包含。

```sh
python3 build.py --pspdev /path/to/pspdev --cmake /path/to/cmake
```

输出 dist/PSP/GAME/DoubleEdged/EBOOT.PBP。构建固定SOURCE_DATE_EPOCH=1794355200、LC_ALL=C；先使用原参数重建字节相同B155，再由 tools/repack_xmb.py嵌入本目录 artwork，核对正式EBOOT身份。candidate_build.py和CANDIDATE.json是保留的历史构建入口；正式入口build.py只构建单机。个人SAVE不包含在安装或源码中，更新时按INSTALL说明迁移。

原作和第三方许可分别保留于packaging/LICENSE_SCOPE.md、packaging/licenses及vendor目录。
