#include "ui_asset.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <memory>

namespace bvn {
namespace {

struct Header {
    char magic[8];
    std::uint32_t count, pixelStart;
};

static_assert(sizeof(UiSprite) == 44 && sizeof(Header) == 16);
constexpr unsigned SpriteLimit = 1024;
constexpr std::size_t PixelLimit = 6 * 1024 * 1024;

} // namespace

bool UiAsset::open(const char* path) {
    const std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(path, "rb"), std::fclose);
    if (!file || std::fseek(file.get(), 0, SEEK_END)) return false;
    const long length = std::ftell(file.get());
    std::rewind(file.get());
    Header header{};
    if (std::fread(&header, sizeof(header), 1, file.get()) != 1 ||
        std::memcmp(header.magic, "BVNUI001", sizeof(header.magic)) || !header.count || header.count > SpriteLimit ||
        header.pixelStart < sizeof(Header) + sizeof(UiSprite) * header.count || header.pixelStart % 16 ||
        length <= long(header.pixelStart)) return false;
    const std::size_t bytes = length - header.pixelStart;
    if (bytes > PixelLimit || bytes % 16) return false;

    UiAsset next;
    next.sprites_.resize(header.count);
    if (std::fread(next.sprites_.data(), sizeof(UiSprite), header.count, file.get()) != header.count) return false;
    const char* previous = "";
    for (const UiSprite& sprite : next.sprites_) {
        if (!std::memchr(sprite.key, 0, sizeof(sprite.key)) || std::strcmp(previous, sprite.key) >= 0 ||
            !sprite.width || !sprite.height || sprite.width > sprite.stride || sprite.height > sprite.rows ||
            sprite.stride < 8 || sprite.stride > 512 || sprite.rows > 512 ||
            !std::has_single_bit(sprite.stride) || !std::has_single_bit(sprite.rows) || sprite.offset % 16 ||
            sprite.offset > bytes || 2U * sprite.stride * sprite.rows > bytes - sprite.offset) return false;
        previous = sprite.key;
    }
    next.pixels_.resize(bytes / 16);
    if (std::fseek(file.get(), header.pixelStart, SEEK_SET) ||
        std::fread(next.pixels_.data(), 1, bytes, file.get()) != bytes) return false;
    *this = std::move(next);
    return true;
}

const UiSprite* UiAsset::find(const char* key) const {
    const auto sprite = std::lower_bound(sprites_.begin(), sprites_.end(), key, [](const UiSprite& entry, const char* name) {
        return std::strcmp(entry.key, name) < 0;
    });
    return sprite != sprites_.end() && std::strcmp(sprite->key, key) == 0 ? &*sprite : nullptr;
}

} // namespace bvn
