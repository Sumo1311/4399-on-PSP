#pragma once

#include "stage_motion.hpp"
#include "map_optical_asset.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bvn {

inline constexpr double StageViewWidth = 800;
inline constexpr double StageViewHeight = 600;
inline constexpr double StageTextureScale = 272 / StageViewHeight;

enum class StageLayer : std::uint16_t { Background, Map, Front, FrontFixed };

struct StageTile {
    float x;
    float y;
    std::uint32_t offset;
    std::uint32_t bytes;
    StageLayer layer;
    std::uint16_t width;
    std::uint16_t height;
    std::uint16_t stride;
    std::uint16_t rows;
    std::uint16_t reserved;
};

class StageAsset {
public:
    // Omit foreground pixels only when their complete optical palette will be
    // bound before drawing. Geometry and all source tiles are still validated.
    bool open(const char* path, bool paletteForeground = false);
    StageGeometry geometry() const;
    double cameraWidth() const { return cameraWidth_; }
    std::optional<Point> spawn(std::size_t player) const;
    std::span<const StageTile> tiles() const { return tiles_; }
    const void* pixels(const StageTile& tile) const { return pixels_.data() + tile.offset / 16; }
    const MapOpticalAsset& optical()const{return optical_;}
    // Call only after the renderer's complete optical palette has been bound.
    void useOpticalPalette();
    std::size_t residentTextureBytes()const{return pixels_.size()*sizeof(PixelBlock);}

private:
    struct alignas(16) PixelBlock {
        std::array<std::byte, 16> bytes;
    };

    std::array<double, 4> bounds_{};
    std::array<Point, 2> spawns_{};
    unsigned spawnFlags_ = 0;
    double cameraWidth_ = StageViewWidth;
    std::vector<Floor> floors_;
    std::vector<StageTile> tiles_;
    std::vector<PixelBlock> pixels_;
    MapOpticalAsset optical_;
};

} // namespace bvn
