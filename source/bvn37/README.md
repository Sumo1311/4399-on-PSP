# 死神 vs 火影 3.7 Alpha PSP 1.0.3 清晰版

从匹配的原始动画 PNG 重建 27,213 张两倍采样密度的小贴图，覆盖全部 78 个角色形态素材包及 41 个援助素材包的部分贴图。剩余贴图保留原有画面；42 名援助、76 个可选形态、2 个内部形态与 19 个场景均保留。原尺寸文字/图标按 PSP 像素对齐显示。

高密度贴图使用 LZ4 快速无损解码。人物的逻辑尺寸、位置、动作/事件/碰撞表和原有元数据身份不变，纹理缓存仍限制为 3 MiB。无双和观战模式继续排除，默认简体中文和 STDIO 同步素材读取。

保留 HWD3 完整 GE 列表提交与纹理回收同步；每段列表恢复绘制目标和舞台裁剪，避免画到两侧黑边。标题显示版本/读取方式，启动按住 L 可使用原直接提交作对照。

用户已确认 1.0.2 HWD3 的自动检查与普通游戏均无黑色矩形；1.0.3 清晰度改动完成模拟器回归，尚未新增实机清晰度/帧率验收。其他边界见 KNOWN_LIMITS.md。

## 安装

解压完整安装包，将 PSP 文件夹合并到记忆棒根目录，正确路径为 PSP/GAME/BVN37/EBOOT.PBP。更新时完整替换 BVN37 游戏目录，保留 PSP/SAVEDATA。高密度资源需要配套 1.0.3 程序。

## 从源码重建

沿用 TOOLCHAIN.json 的 PSPDEV GCC 15.2.0、CMake 3.31.10 和 SOURCE_DATE_EPOCH。以正式完整安装 ZIP 提供经哈希校验的外置资源，运行 build.py --install-zip BVN37_PSP_1.0.3_Install.zip --build-dir build --output install --require-identical。程序和全部外置文件均可核对重建身份。

## 贴图生成与检查

HD_TEXTURES.json 记录密度和各包原元数据身份。tools/native_pose_index.py 对原始导出的 poses PNG 做低密度像素匹配；tools/promote_native_textures.py 只替换匹配贴图，保留逻辑表；tools/recode_hd_lz4.py 验证像素无损后转换为 LZ4。tools/bank_texture_info.cpp 导出旧资源的逻辑尺寸与身份；tests/native_density.cpp 检查新资源解码和对象配置。输入为原始姿态导出、匹配索引及相应低密度基准资源；常规程序重建不需要重新生成素材。

BVNFGHxx 和 BVNASSTH/L 沿用原有表结构，纹理 rawSize 可表示 1×或 2×密度，文件尾保留 8 字节的原始元数据身份。BVNASSTL 使用 LZ4，读取布局仍按原援助版本 11；角色版本 15 使用 LZ4。图像密度不会改变逻辑宽高或事件编号。
