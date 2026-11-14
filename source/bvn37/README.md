# 死神 vs 火影 3.7 Alpha PSP 1.0.4

修复宇智波斑持续防御、大招及其他动作的黑色矩形，并按相同原因检查全部 120 个角色／援助素材包。19 个受影响素材包从原始 SWF 重建混合图层，覆盖其中所有动作使用的相关贴图；保留角色黑发、服装与阴影。

保留 1.0.3 的 27,213 张两倍采样密度贴图和原尺寸文字／图标对齐。76 个可选形态、2 个内部形态、42 名援助和 19 张场景均保留。默认简体中文、STDIO 同步读取；按用户要求排除无双和观战模式。

按原始顺序绘制普通、滤色、叠加、减色、乘色、反色、变亮和变暗图层。大型分层图像拆成有界贴图，保留逻辑覆盖范围，单条解码最大 2 MiB，纹理缓存仍为 3 MiB。动作事件、碰撞、数值和时序保持原样。延续 HWD3 完整 GE 列表提交与纹理回收同步；按住 L 启动可作直接提交对照。

全部 120 包通过 ASan／UBSan 解码；2,546 张分层贴图通过独立混合公式检查，9 组真实游戏逻辑场景完成 594 对 DIRECT／队列画面对照，并通过普通菜单进入的实战检查。1.0.4 尚无新增实体 PSP 验收，具体边界见 KNOWN_LIMITS.md 和 BLEND_VERIFICATION.json。

## 安装和重建

解压唯一完整安装包，正确路径为 PSP/GAME/BVN37/EBOOT.PBP。更新时完整替换 BVN37 目录，保留 PSP/SAVEDATA。新素材必须配套本版程序。

按 TOOLCHAIN.json 使用 PSPDEV GCC 15.2.0、CMake 3.31.10 和 SOURCE_DATE_EPOCH=1793232000：

```
python3 build.py --install-zip BVN37_PSP_1.0.4_Install.zip --build-dir build --output install --require-identical
```

完整 ZIP 提供经 CONTENT_MANIFEST.json 哈希校验的外置素材。程序和全部 196 个安装文件均可核对重建身份，普通重建无需重新生成动画。

## 原始素材重建

HD_TEXTURES.json 和 CLARITY_VERIFICATION.json 保留 1.0.3 的历史清晰度生成记录；本版结果见 BLEND_LAYERS.json 和 BLEND_VERIFICATION.json。tools/native_pose_index.py、promote_native_textures.py 和 recode_hd_lz4.py 保留原密度匹配管线。

tools/build_blend_bounds.py 与 repair/blend-layers/*.bounds.tsv 固定原有逻辑边界。ExtractFighter.java 在 -Dbvn.layers=true 下导出当前时间轴的原始可见图层，使用 -Dbvn.poseBounds 指定相应边界文件。tools/pack_blend_layers.py 将同一逻辑贴图的源图层无损编码并拆分超预算条目。Gin 原有两张小型普通待机贴图保持字节不变。

BVNFGH16 和 BVNASSTX 沿用角色 v15／援助 v11 表布局及 LZ4；分层条目尾表为 (u32 texture, u64 modes)，随后 count、0x33594c42 和原始 u64 元数据身份。modes 低 4 位为图层数，随后每 4 位为图层模式；每张逻辑贴图的解码载荷由等尺寸 RGBA8888 图层顺序拼接。普通旧格式继续兼容，图像密度与图层数量分别读取。tests/blend_layers_psp.cpp 使用实际 PSP 绘制器与独立浮点公式比对。
