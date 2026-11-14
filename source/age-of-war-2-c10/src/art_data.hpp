#pragma once
#include <cstdint>
namespace aow {
struct ArtTile {uint16_t page,x,y,w,h; float ox,oy;};
struct ArtTexture {uint16_t width,height;uint32_t offset,compressed;float uvScale=1.f;};
struct ArtFrame {uint32_t first;uint16_t count;};
struct ArtAsset {const char* key;uint32_t first;uint16_t count;float rasterScale;};
struct Glyph {uint32_t code,frame;float advance;};
inline constexpr int ART_TEXTURE_COUNT=7904;
extern const ArtTexture artTextures[];
extern const ArtTile artTiles[];
extern const ArtFrame artFrames[];
extern const ArtAsset artAssets[];
extern const Glyph glyphs[];
inline constexpr int ART_ASSET_COUNT=196, GLYPH_COUNT=720;
inline constexpr int COMPACT_GLYPH_COUNT=111, LARGE_GLYPH_COUNT=138;
extern const Glyph compactGlyphs[];
extern const Glyph largeGlyphs[];
}
