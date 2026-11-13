# 死神 vs 火影 PSP 封面与背景

使用内置 image_gen 生成，完整提示词见 `generation-prompts.json`。2026-11-13 制作的配套动漫插画：一护与鸣人对峙封面，以及人物靠右、左侧留出菜单区域的夜景背景。

- `cover-master.png`：高清封面母图。
- `background-master.png`：高清背景母图。
- `xmb/ICON0.PNG`：144 × 80，嵌入 EBOOT 的游戏图标。
- `xmb/PIC1.PNG`：480 × 272，嵌入 EBOOT 的 PSP 菜单背景。

母图通过 macOS `sips -z 80 144` 和 `sips -z 272 480` 缩放得到打包图片。源码已包含最终 PNG，重建无需图像生成服务或 macOS。

本次为 1.0.0 的 `xmb1` 包装修订，游戏版本与存档格式保持一致。图片是为本移植制作的非官方插画，原角色及相关商标权利归各自所有者；不将本图片解释为原作官方提供的素材。
