#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bvn {

struct UiSprite {
    char key[32];
    std::uint32_t offset;
    std::uint16_t width, height, stride, rows;
};

class UiAsset {
public:
    bool open(const char* path);
    const UiSprite* find(const char* key) const;
    const void* pixels(const UiSprite& sprite) const { return pixels_.data() + sprite.offset / 16; }

private:
    struct alignas(16) PixelBlock { std::array<std::byte, 16> bytes; };
    std::vector<UiSprite> sprites_;
    std::vector<PixelBlock> pixels_;
};

} // namespace bvn
