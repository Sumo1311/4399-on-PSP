# 死神 vs 火影 3.7 Alpha PSP 1.0.5

修复宁次、茶渡、一角、大蛇丸的待机闪烁，佩恩行走和旧版鼬部分空中动作的异常透明帧。26 个动作帧从修正后的原始 SWF 导出恢复，保留原有可见帧、动作时序、碰撞、命令和事件。新增 9 张双倍密度贴图，完整包共 27,222 张双倍密度贴图。

修复默认重吾 R 大招与李洛克同类动作的落地循环：物理接地早于招式登记落地动作时，按原控制器语义保留尚未处理的接地状态，等待落地收招执行。

保留 1.0.4 的原始混合图层和黑矩形修复、3 MiB 纹理缓存及 STDIO/HWD3 完整 GE 提交。76 个可选形态、2 个内部形态、42 名援助和 19 张地图保留，默认简体中文，排除无双和观战。

## 安装和重建

解压完整安装 ZIP，完整替换记忆棒 PSP/GAME/BVN37 目录，保留 PSP/SAVEDATA。正确入口为 PSP/GAME/BVN37/EBOOT.PBP，新素材需配套本版程序。

按 TOOLCHAIN.json 使用 PSPDEV GCC 15.2.0、CMake 3.31.10、SOURCE_DATE_EPOCH=1793232000：

```
python3 build.py --install-zip BVN37_PSP_1.0.5_Install.zip --build-dir build --output install --require-identical
```

build.py 校验外置素材并重建 196 个完整安装文件。历史 HD_TEXTURES.json、BLEND_LAYERS.json 和 BLEND_VERIFICATION.json 记录前两版生成阶段，本版最终文件身份以 CONTENT_MANIFEST.json 为准。数据格式沿用 BVNFGH15/16 与 BVNASSTL/X，原混合层尾表及元数据身份保留。

## 验证与素材修复

REGRESSION_1_0_5.json 记录本版范围。全部 120 包通过 ASan/UBSan 解码；78 个英雄包的 4,043 个待机、行走、跳跃帧无异常空白。实际绘制器对全英雄动作贴图进行独立公式核验；完整形态配置的 616 组大招对照、重吾新旧版 42 次普通菜单大招连测、6 个修复英雄的普通入场与 9 组完整对战画面对照通过。以上为软件和模拟器结果，实体 PSP 尚待新版本复测。

tools/repair_held_poses.py、repair/held-visuals/repair-spec.json 和 export/ 下的对应原始 PNG 可重做纯视觉修复，只追加贴图并修复指定透明帧的画面描述；保留其他全部载荷和混合尾表。tests/landing_actions.cpp 与修复目录中的完整形态清单可重跑落地动作对照。tests/blend_layers_psp.cpp 存在 LOCOMOTION_BANKS.TXT 时改为检查全部待机/行走/跳跃贴图，否则继续原分层贴图检查。
