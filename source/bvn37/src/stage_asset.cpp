#include "stage_asset.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace bvn {
namespace {

struct Header {
    char magic[8];
    std::uint32_t floorCount;
    std::uint32_t tileCount;
    std::uint32_t pixelStart;
    std::uint32_t spawnFlags;
    std::array<double, 4> bounds;
    std::array<Point, 2> spawns;
};

static_assert(std::endian::native == std::endian::little);
static_assert(sizeof(Header) == 88 && sizeof(Floor) == 24 && sizeof(StageTile) == 28);

constexpr std::size_t TextureBudget = 12 * 1024 * 1024;

bool textureExtent(unsigned size) {
    return size >= 4 && size <= 128 && std::has_single_bit(size);
}

bool valid(const StageTile& tile, std::size_t expectedOffset, std::size_t payload) {
    return std::isfinite(tile.x) && std::isfinite(tile.y) &&
           tile.layer <= StageLayer::FrontFixed && tile.reserved == 0 &&
           textureExtent(tile.stride) && textureExtent(tile.rows) &&
           tile.width > 0 && tile.width <= tile.stride && tile.height > 0 && tile.height <= tile.rows &&
           tile.offset == expectedOffset && tile.bytes == 4u * tile.stride * tile.rows &&
           tile.bytes <= payload - expectedOffset;
}

} // namespace

bool StageAsset::open(const char* path, bool paletteForeground) {
    const std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(path, "rb"), std::fclose);
    if (!file || std::fseek(file.get(), 0, SEEK_END)) return false;
    const long length = std::ftell(file.get());
    if (length < long(sizeof(Header))) return false;
    std::rewind(file.get());

    Header header{};
    if (std::fread(&header, sizeof(header), 1, file.get()) != 1 ||
        header.floorCount > 512 || header.tileCount == 0 || header.tileCount > 4096 || header.spawnFlags > 3) {
        return false;
    }
    const bool opticalMap=std::memcmp(header.magic,"BVNSTG03",sizeof(header.magic))==0;
    const bool authoredCamera = opticalMap||std::memcmp(header.magic, "BVNSTG02", sizeof(header.magic)) == 0;
    if (!authoredCamera && std::memcmp(header.magic, "BVNSTG01", sizeof(header.magic))) return false;
    const auto [left, right, bottom, playerBottom] = header.bounds;
    if (!std::all_of(header.bounds.begin(), header.bounds.end(), [](double n) { return std::isfinite(n); }) ||
        left >= right || playerBottom > bottom ||
        !std::all_of(header.spawns.begin(), header.spawns.end(), [](Point p) {
            return std::isfinite(p.x) && std::isfinite(p.y);
        })) return false;

    double cameraWidth = right - left;
    if (authoredCamera && (std::fread(&cameraWidth, sizeof(cameraWidth), 1, file.get()) != 1 ||
        !std::isfinite(cameraWidth) || cameraWidth <= 0)) return false;
    unsigned opticalChildren=0;
    if(opticalMap&&(std::fread(&opticalChildren,sizeof(opticalChildren),1,file.get())!=1||!opticalChildren||opticalChildren>12))return false;
    const std::size_t tableEnd = sizeof(Header) + (authoredCamera ? sizeof(cameraWidth) : 0) + (opticalMap?sizeof(opticalChildren):0) + header.floorCount * sizeof(Floor) +
                                 header.tileCount * sizeof(StageTile);
    if (header.pixelStart != ((tableEnd + 15) & ~std::size_t(15)) ||
        header.pixelStart >= static_cast<std::size_t>(length)) return false;
    const std::size_t payload = static_cast<std::size_t>(length) - header.pixelStart;
    if (payload > TextureBudget || payload % sizeof(PixelBlock)) return false;

    StageAsset next;
    if(opticalMap){
        auto sidecar=std::string(path);const auto dot=sidecar.find_last_of('.');if(dot==std::string::npos)return false;
        sidecar.resize(dot);sidecar+=".BVI";
        if(!next.optical_.open(sidecar.c_str(),opticalChildren))return false;
    }
    next.floors_.resize(header.floorCount);
    next.tiles_.resize(header.tileCount);
    if ((!next.floors_.empty() &&
         std::fread(next.floors_.data(), sizeof(Floor), next.floors_.size(), file.get()) != next.floors_.size()) ||
        std::fread(next.tiles_.data(), sizeof(StageTile), next.tiles_.size(), file.get()) != next.tiles_.size()) return false;
    for (const Floor& floor : next.floors_) {
        if (!std::isfinite(floor.left) || !std::isfinite(floor.right) || !std::isfinite(floor.y) ||
            floor.left > floor.right) return false;
    }
    std::size_t offset = 0;
    for (const StageTile& tile : next.tiles_) {
        if (!valid(tile, offset, payload)) return false;
        offset += tile.bytes;
    }
    if (offset != payload || std::fseek(file.get(), header.pixelStart, SEEK_SET)) return false;

    if (paletteForeground && opticalMap) {
        // Read the same retained bytes as useOpticalPalette(), without first
        // allocating the entire unused foreground and a second pixel buffer.
        // That temporary peak can fail after repeated map/fighter selections
        // on the PSP1000 heap even though the final scene fits.
        std::size_t retained = 0;
        unsigned count = 0;
        for (const auto& tile : next.tiles_) if (tile.layer != StageLayer::Front) {
            retained += tile.bytes;
            ++count;
        }
        next.pixels_.resize(retained / sizeof(PixelBlock));
        std::vector<StageTile> tiles;
        tiles.reserve(count);
        std::size_t into = 0;
        for (auto tile : next.tiles_) if (tile.layer != StageLayer::Front) {
            if (std::fseek(file.get(), header.pixelStart + tile.offset, SEEK_SET) ||
                std::fread(reinterpret_cast<std::byte*>(next.pixels_.data()) + into,
                           1, tile.bytes, file.get()) != tile.bytes) return false;
            tile.offset = into;
            into += tile.bytes;
            tiles.push_back(tile);
        }
        next.tiles_ = std::move(tiles);
    } else {
        next.pixels_.resize(payload / sizeof(PixelBlock));
        if (std::fread(next.pixels_.data(), 1, payload, file.get()) != payload) return false;
    }
    next.bounds_ = header.bounds;
    next.spawns_ = header.spawns;
    next.spawnFlags_ = header.spawnFlags;
    next.cameraWidth_ = cameraWidth;
    *this = std::move(next);
    return true;
}

StageGeometry StageAsset::geometry() const {
    const auto [left, right, bottom, playerBottom] = bounds_;
    return {left, right, bottom, playerBottom, floors_};
}

void StageAsset::useOpticalPalette(){
    if(optical_.children().empty())return;
    std::size_t retained=0;unsigned count=0;
    for(const auto& tile:tiles_)if(tile.layer!=StageLayer::Front){retained+=tile.bytes;++count;}
    if(count==tiles_.size())return;
    // The palette supplies every normal and blurred foreground state. Keeping
    // the old full foreground resident would consume the PSP1000 drawing heap.
    std::vector<PixelBlock> pixels(retained/sizeof(PixelBlock));
    std::vector<StageTile> tiles;tiles.reserve(count);std::size_t offset=0;
    for(auto tile:tiles_)if(tile.layer!=StageLayer::Front){
        std::memcpy(reinterpret_cast<std::byte*>(pixels.data())+offset,this->pixels(tile),tile.bytes);
        tile.offset=offset;offset+=tile.bytes;tiles.push_back(tile);
    }
    pixels_=std::move(pixels);tiles_=std::move(tiles);
}

std::optional<Point> StageAsset::spawn(std::size_t player) const {
    if (player >= spawns_.size() || !(spawnFlags_ & (1u << player))) return std::nullopt;
    return spawns_[player];
}

} // namespace bvn
