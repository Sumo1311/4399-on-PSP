# Third-party notices / 第三方声明

This is an unofficial PSP port collection. All original game credits and third-party notices remain applicable. The author's MIT grant is limited as described in `LICENSE_SCOPE.md`.

本项目用于非商业的学习、技术研究与交流。感谢原游戏厂商、开发者及素材创作者；原作名称、商标、画面、音频等权利归各自权利人所有。学习用途说明不替代原作素材的使用或分发许可，也不表示原厂商、作者或平台对本项目的授权与认可。

## Original games and media

| Game | Original attribution | Content excluded from MIT |
| --- | --- | --- |
| 勇者之路：精灵物语 / 冒险王之精灵物语 | Original Flash production and asset creators credited in the game's 制作者 screen | Original artwork, animation, music, sound, text, characters, maps, campaign and derived asset data |
| Fireboy and Watergirl: Forest Temple | Oslo Albet; original Armor Games presentation and credits retained | Original characters, artwork, audio, level layouts, terrain geometry and derived asset data |
| Flipside | Nitrome; Jon Annal, artwork; Chris Burt-Brown, programming; Dave Cowen, music; original MTV Arcade presentation | Original artwork, audio, animation, track layouts, geometry and derived asset data |
| Stick Figure Badminton 2 | Effing Games and the original game's credited contributors | Original artwork, animation, sound, font renderings, character designs and extracted collision data |
| Bleach vs Naruto 3.7 Alpha / 死神 vs 火影 | [5DPLAY Game Studio](https://github.com/5DPLAY-Game-Studio/BleachVsNaruto), pinned baseline `3.7.0.0.10292026_alpha`; corresponding source and notices are attached to the 1.0.0 Release | Source-derived code under the archive's GPL-3.0/per-file notices, original characters, names, artwork, animation, sound, maps and derived data |

Original publication references include the [Nitrome Flipside page](https://www.nitrome.com/games/flipside/) and [Effing Games' Kongregate page](https://www.kongregate.com/en/games/effinggames/stick-figure-badminton-2). These are attribution references, not evidence of an MIT license for the games.

Nitrome's [EULA](https://www.nitrome.com/eula/), including its provisions for fan-made work and its retained rights, continues to apply to Nitrome materials. This release does not convert those terms into an open-content license. No separate original-asset redistribution grant is recorded in this repository. The original rights holders retain their rights in the other games as well.

Menu artwork added for this collection includes generated fan illustrations. Existing character, brand and other third-party rights are retained. Original XMB icons/backgrounds already present in delivered packages are preserved.

## Runtime libraries

| Component | Use | License / accompanying notice |
| --- | --- | --- |
| PSPSDK | Published PSP programs | BSD-3-Clause, `licenses/PSPSDK-BSD-3-Clause.txt` |
| newlib | C/C++ runtime support | Multiple permissive notices, reproduced in `licenses/COPYING.NEWLIB` |
| GCC runtime libraries / libstdc++ | Compiler runtime and C++ support | GPL-3.0 with GCC Runtime Library Exception 3.1, both texts in `licenses/` |
| pthread-embedded, PSP port | Thread support selected by the PSP GCC toolchain | LGPL-2.1-or-later; retained Vita-specific MIT notice; see accompanying license files and `RELINKING.md` |
| zlib | Elf Story, Flipside, Badminton 2 and Bleach vs Naruto compressed data | zlib license in `licenses/zlib-README.txt` |
| LZ4 | Flipside 1.0.3 and Bleach vs Naruto media | BSD-2-Clause, `licenses/LZ4-BSD-2-Clause.txt` |
| Box2D 2.0.1 | Flipside physics | zlib-style license, `licenses/Box2D-2.0.1-zlib.txt` |

The Flipside Box2D implementation is an altered version of Erin Catto's original. Its proxy capacity, maximum polygon vertices, finite-value checks, allocation alignment and combined normal/friction impulse order were adapted for the PSP/source-game port. It must not be represented as an unmodified upstream Box2D distribution.

pthread-embedded corresponding source is the unmodified PSP fork at commit `97fe4ce006b420894f2bcaeb530d1f1f53111fc2`, included in the Release's relinking archive. The original game object files and a relinking tool are supplied with it. PSPSDK build provenance is recorded in that archive. The GCC runtime exception remains attached to the eligible compiler runtime portions; it is not used to relicense pthread-embedded.

Build-only reference tools such as JPEXS, Ruffle and PPSSPP are not bundled as runtime components of these installation packages.
