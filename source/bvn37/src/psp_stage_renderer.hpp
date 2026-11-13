#pragma once

#include "fight_camera.hpp"
#include "fight_effects.hpp"
#include "fighter_asset.hpp"
#include "stage_asset.hpp"
#include "ui_asset.hpp"
#include "transition_asset.hpp"
#include "effect_pixels.hpp"

#include <memory>

namespace bvn {

struct FighterView {
    const FighterAsset& asset;
    const FighterPose& pose;
    Point position;
    int facing;
    const FighterPose* screen = nullptr;
    unsigned alpha = 255;
    ShadowTint tint{};
    SourcePaint::Key displayKey=SourcePaint::None;
    bool visible=true;
};

// World coordinates remain in Flash's 800×600 view space.
class StageRenderer {
public:
    void start(bool synchronized = false);
    void quality(unsigned value){smooth_=value==3;}
    void begin();
    // Whole GameStage translation, including its UI and shine children, in Flash units.
    void rootOffset(Point offset);
    void arena(const StageAsset& stage, CameraView camera, std::span<const FighterView> fighters, SceneEffects effects,
               const UiAsset* cueArt = nullptr, const FighterAsset* reactionArt = nullptr,
               const FighterAsset* combatArt = nullptr,const FighterAsset* mapBlurArt = nullptr,
               const FighterAsset* mapOpticalArt = nullptr);
    // Origin is in PSP view pixels, before the pillar offset; pose offsets retain the asset view scale.
    void effectPose(const FighterAsset& asset,const FighterPose& layer,Point origin,double zoom,
                    int facing,unsigned rotation,EffectBlend blend);
    void beginUi();
    bool transition(TransitionAsset& asset,unsigned frame);
    void image(const UiAsset& ui, const char* key, float x, float y, float width, float height, bool flip = false);
    void imageTint(const UiAsset& ui,const char* key,float x,float y,float width,float height,
                   bool flip,unsigned alpha,unsigned rgbOffset);
    void imageFillBottom(const UiAsset& ui, const char* key, float x, float y, float width, float height,
                         bool flip, float fill);
    void box(float x, float y, float width, float height, std::uint32_t color);
#ifdef BVN_RESOURCE_DIAGNOSTICS
    // Diagnostic ASCII in native pixels; uses the linked PSPSDK font.
    void diagnosticText(const char* text, float x, float y);
#endif
    void shine(SceneEffects effects);
    void end();
    void clearFighterTextures();
    // Reclaim one LRU texture without allocating. Textures referenced by an
    // unfinished GE list and a cache mutation in progress are never reclaimed.
    bool reclaimCompletedTexture(std::size_t* bytes = nullptr) noexcept;
    void draw(const StageAsset& stage, CameraView camera, std::span<const FighterView> fighters = {}, SceneEffects effects = {});

private:
    void blurLayer(const FighterAsset& asset,StageLayer layer,unsigned strengthIndex,CameraView camera,unsigned shade);
    void opticalLayer(const MapOpticalAsset& info,const FighterAsset& art,unsigned kind,unsigned mask,CameraView camera,unsigned shade);
    struct alignas(16) PixelBlock { std::byte bytes[16]; };
    struct CachedTexture {
        const FighterAsset* asset;
        std::uint32_t index;
        std::uint64_t lastFrame;
        std::size_t size;
        std::unique_ptr<PixelBlock[]> pixels;
        ShadowTint tint{};
        EffectSurface surface=EffectSurface::Color;
    };
    const void* fighterTexture(const FighterAsset& asset, const FighterPose& pose, ShadowTint tint = {},
                              EffectSurface surface=EffectSurface::Color);

    bool synchronized_ = false;
    void* drawBuffer_ = nullptr;
    bool smooth_=false;
    bool cacheMutation_ = false, listInFlight_ = false;
    std::vector<CachedTexture> fighterTextures_;
    std::size_t textureBytes_ = 0;
    std::uint64_t frame_ = 0;
};

} // namespace bvn
