#include "resource_diagnostics.hpp"
#include <cerrno>
#include "psp_stage_renderer.hpp"
#include "shadow_pixels.hpp"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <new>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>

#ifdef BVN_RESOURCE_DIAGNOSTICS
extern "C" unsigned char msx[];
#endif
extern "C" {
volatile struct RendererQA {
    unsigned frames, additive, darken, color, alpha;
    unsigned combatDraws,hardlight,lighten,screen;
    unsigned textureMisses,combatTiles;
} g_rendererQa;
volatile struct TransitionRendererQA {unsigned draws,frame,decodeFailures;}g_transitionRendererQa;
volatile struct BlurRendererQA {unsigned layers,tiles,missing,culled;} g_blurRendererQa;
volatile struct OpticalRendererQA {unsigned layers,tiles,missing,cached,normal;} g_opticalRendererQa;
volatile struct TextureCacheQA {
    unsigned budget,allocation,decode,requested,resident,locked,retryAllocation,retryDecode,evicted;
} g_textureCacheQa;
volatile struct TextureStreamQA {unsigned barriers,completedBytes;} g_textureStreamQa;
volatile struct PaintRendererQA {unsigned frames,players,roots,missing,hashLow,hashHigh,sprites,combat,ground,reactions,shadows,blackBack,backgrounds;} g_paintRendererQa;
}

namespace bvn {
namespace {

constexpr int Width = 480;
constexpr int Height = 272;
constexpr int Stride = 512;
constexpr std::size_t FrameBytes = Stride * Height * sizeof(std::uint32_t);
static_assert(3 * FrameBytes <= 2 * 1024 * 1024);
constexpr float ViewWidth = float(StageViewWidth * StageTextureScale);
constexpr float Pillar = (Width - ViewWidth) / 2;
constexpr std::size_t FighterTextureBudget = 3 * 1024 * 1024;
constexpr int VertexFormat = GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D;
alignas(64) std::uint32_t list[32768];
Point rootShift{};

struct Vertex {
    float u, v;
    std::uint32_t color;
    float x, y, z;
};

void drawTexture(const void* pixels, int stride, int rows, float x, float y, int width, int height,
                 float outputWidth, float outputHeight, bool flip = false, std::uint32_t color = 0xffffffff,
                 float cropTop = 0) {
    x += rootShift.x; y += rootShift.y;
    const float right = x + outputWidth;
    const float bottom = y + outputHeight;
    if (x >= ViewWidth || right <= 0 || y >= Height || bottom <= 0) return;
    sceGuTexImage(0, stride, rows, stride, pixels);
    auto* vertices = static_cast<Vertex*>(sceGuGetMemory(2 * sizeof(Vertex)));
    vertices[0] = {flip ? float(width) : 0, height * cropTop, color, x + Pillar, y + outputHeight * cropTop, 0};
    vertices[1] = {flip ? 0 : float(width), float(height), color, right + Pillar, bottom, 0};
    sceGuDrawArray(GU_SPRITES, VertexFormat, 2, nullptr, vertices);
}

void drawTile(const StageAsset& stage, const StageTile& tile, CameraView camera, float shiftY, unsigned shade) {
    double xOffset = 0, yOffset = 0;
    double zoom = 1;
    if (tile.layer != StageLayer::Background) {
        xOffset = -camera.origin.x;
        yOffset = -camera.origin.y;
        zoom = camera.zoom;
        if (tile.layer == StageLayer::Front) {
            xOffset -= camera.origin.x * 0.1;
            yOffset += std::max(0.0, (StageViewHeight - camera.origin.y) * 0.1);
        }
    }
    const float x = float((tile.x + xOffset * StageTextureScale) * zoom);
    const float y = float((tile.y + yOffset * StageTextureScale) * zoom) + shiftY;
    const auto color = 0xff000000u | (255 - shade) * 0x010101u;
    drawTexture(stage.pixels(tile), tile.stride, tile.rows, x, y, tile.width, tile.height,
                float(tile.width * zoom), float(tile.height * zoom), false, color);
}

void drawBox(float x, float y, float width, float height, std::uint32_t color) {
    x += rootShift.x; y += rootShift.y;
    struct ColorVertex { std::uint32_t color; float x, y, z; };
    sceGuDisable(GU_TEXTURE_2D);
    auto* vertices = static_cast<ColorVertex*>(sceGuGetMemory(2 * sizeof(ColorVertex)));
    vertices[0] = {color, x + Pillar, y, 0};
    vertices[1] = {color, x + Pillar + width, y + height, 0};
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, nullptr, vertices);
    sceGuEnable(GU_TEXTURE_2D);
}

void drawPortrait(SceneEffects effects, double zoom, const void* pixels) {
    // Position and squash keys from effect/LIBRARY/_AS链接/元件/bisha_face_mc.xml.
    const float frame = effects.portraitFrame;
    const float opening = std::clamp(frame / 3, 0.0f, 1.0f);
    const float settle = std::clamp((frame - 3) / 3, 0.0f, 1.0f);
    const float drift = std::clamp((frame - 6) / 53, 0.0f, 1.0f);
    const float fade = std::clamp((69 - frame) / 10, 0.0f, 1.0f);
    const float left = -133.95f + 113 * opening + 2.85f * settle + 16.15f * drift;
    const float right = 931.85f - 113 * opening - 2.85f * settle - 16.15f * drift;
    const float width = 254;
    const float height = 180 * (0.08253479f + 0.91746521f * opening);
    const float x = effects.portraitRight ? right - width : left;
    const float y = float(100 + 100 / zoom + 55.1f * (1 - opening));
    const auto boost = static_cast<unsigned>(255 - 205 * settle - 50 * drift);
    const auto color = (static_cast<unsigned>(255 * fade) << 24) | boost * 0x010101u;
    const auto& pose = *effects.portrait;
    sceGuTexFunc(GU_TFX_ADD, GU_TCC_RGBA);
    drawTexture(pixels, pose.stride, pose.rows, x * StageTextureScale,
                y * StageTextureScale, pose.width, pose.height, width * StageTextureScale, height * StageTextureScale,
                effects.portraitRight, color);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
}

} // namespace

void StageRenderer::start(bool synchronized) {
    synchronized_ = synchronized;
    drawBuffer_ = nullptr;
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, nullptr, Stride);
    sceGuDispBuffer(Width, Height, reinterpret_cast<void*>(Stride * Height * 4), Stride);
    sceGuOffset(2048 - Width / 2, 2048 - Height / 2);
    sceGuViewport(2048, 2048, Width, Height);
    sceGuScissor(0, 0, Width, Height);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, GU_FALSE);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
    if (synchronized_) guSwapBuffersBehaviour(PSP_DISPLAY_SETBUF_NEXTVSYNC);
}

void StageRenderer::shine(SceneEffects effects) {
    if(!effects.shines.empty()) {
        const auto layers=effects.shines;
        effects.shines={};
        for(const auto& layer:layers) {
            effects.shineColor=layer.color;effects.shineAlpha=layer.alpha;
            shine(effects);
        }
        return;
    }
    if (!effects.shineAlpha) return;
    const std::uint32_t rgb = effects.shineColor;
    g_rendererQa.color = rgb;
    g_rendererQa.alpha = effects.shineAlpha;
    const std::uint32_t color = (effects.shineAlpha << 24) | ((rgb & 0xff) << 16) | (rgb & 0xff00) | (rgb >> 16);
    if (rgb > 0x7fffff) {
        g_rendererQa.additive = g_rendererQa.additive + 1;
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xffffff);
        drawBox(0, 0, ViewWidth, Height, color);
    } else {
        g_rendererQa.darken = g_rendererQa.darken + 1;
        auto* vram = static_cast<std::byte*>(sceGeEdramGetAddr());
        void* original = vram + 2 * FrameBytes;
        void* current = vram + reinterpret_cast<std::uintptr_t>(drawBuffer_);
        // One 272-row copy fits after both framebuffers in the PSP's 2 MiB VRAM.
        sceGuCopyImage(GU_PSM_8888, 0, 0, Width, Height, Stride, current, 0, 0, Stride, original);
        sceGuTexSync();
        sceGuTexFlush();
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
        drawBox(0, 0, ViewWidth, Height, color);
        // min(D, (1-a)D + aS) = (1-a)D + a*min(D,S), Flash DARKEN.
        sceGuTexMode(GU_PSM_8888, 0, 0, GU_FALSE);
        sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
        sceGuTexFilter(GU_NEAREST, GU_NEAREST);
        sceGuBlendFunc(GU_MIN, GU_FIX, GU_FIX, 0xffffff, 0xffffff);
        // Split the copy into valid power-of-two texture heights, without
        // declaring a 512-row allocation that would extend past VRAM.
        const auto shifted = rootShift; rootShift = {};
        drawTexture(original, Stride, 256, -Pillar, 0, Width, 256, Width, 256);
        drawTexture(static_cast<std::byte*>(original) + Stride * 256 * 4,
                    Stride, 16, -Pillar, 256, Width, 16, Width, 16);
        rootShift = shifted;
        sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
        sceGuTexFilter(smooth_ ? GU_LINEAR : GU_NEAREST, smooth_ ? GU_LINEAR : GU_NEAREST);
    }
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
}

void StageRenderer::begin() {
    rootShift = {};
    ++frame_;
    listInFlight_ = true;
    g_rendererQa.frames = g_rendererQa.frames + 1;
    sceGuStart(GU_DIRECT, list);
    sceGuScissor(0, 0, Width, Height);
    sceGuClearColor(0xff000000);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    // Clip the centered Flash stage by pixel centers, excluding both fractional
    // pillar columns even when a translated world sprite extends past the stage.
    const int left=int(std::ceil(double(Pillar)-.5)),right=int(std::ceil(double(Pillar+ViewWidth)-.5));
    sceGuScissor(left,0,right-left,Height);
    sceGuTexMode(GU_PSM_8888, 0, 0, GU_FALSE);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
}

void StageRenderer::rootOffset(Point offset) {
    rootShift = {offset.x * StageTextureScale, offset.y * StageTextureScale};
}

void StageRenderer::clearFighterTextures() {
    fighterTextures_.clear();
    textureBytes_ = 0;
}

bool StageRenderer::reclaimCompletedTexture(std::size_t* bytes) noexcept {
    if (bytes) *bytes = 0;
    if (cacheMutation_) return false;
    const auto oldest = std::ranges::min_element(fighterTextures_, {}, &CachedTexture::lastFrame);
    if (oldest == fighterTextures_.end() || (listInFlight_ && oldest->lastFrame == frame_)) return false;
    const auto size = oldest->size;
    textureBytes_ -= size;
    fighterTextures_.erase(oldest);
    g_textureCacheQa.evicted = g_textureCacheQa.evicted + 1;
    if (bytes) *bytes = size;
    return true;
}

const void* StageRenderer::fighterTexture(const FighterAsset& asset, const FighterPose& pose, ShadowTint tint,
                                         EffectSurface surface) {
    struct CacheMutation {
        bool& active;
        explicit CacheMutation(bool& value) : active(value) { active = true; }
        ~CacheMutation() { active = false; }
    } mutation(cacheMutation_);
    const auto miss=[&](unsigned reason,std::size_t requested)->const void*{
#ifdef BVN_RESOURCE_DIAGNOSTICS
        diagnosticFailure(asset.diagnosticPath(), reason == 1 ? "CACHE BUDGET" : reason == 2 ? "CACHE ALLOCATION" : "TEXTURE READ", errno, pose.offset, requested);
#endif
        g_rendererQa.textureMisses=g_rendererQa.textureMisses+1;
        if(reason==1)g_textureCacheQa.budget=g_textureCacheQa.budget+1;
        else if(reason==2)g_textureCacheQa.allocation=g_textureCacheQa.allocation+1;
        else g_textureCacheQa.decode=g_textureCacheQa.decode+1;
        g_textureCacheQa.requested=requested;g_textureCacheQa.resident=textureBytes_;
        unsigned locked=0;
        for(const auto& entry:fighterTextures_)if(entry.lastFrame==frame_)locked+=entry.size;
        g_textureCacheQa.locked=locked;
        return nullptr;
    };
    if (tint == ShadowTint{}&&surface==EffectSurface::Color) if (const void* pixels = asset.pixels(pose)) return pixels;
    for (auto& entry : fighterTextures_) {
        if (entry.asset == &asset && entry.index == pose.offset && entry.tint == tint&&entry.surface==surface) {
            entry.lastFrame = frame_;
            return entry.pixels.get();
        }
    }

    const std::size_t size = FighterAsset::textureSize(pose);
    const auto evict=[&] {
        auto oldest = std::ranges::min_element(fighterTextures_, {}, &CachedTexture::lastFrame);
        if (oldest == fighterTextures_.end()) return false;
        if (oldest->lastFrame == frame_ && listInFlight_) {
            // A real frame can use more art than the bounded cache (e.g. Ino's
            // assist plus the blurred map). Finish those GE reads before
            // recycling their textures, then continue the same framebuffer
            // with its existing render state. Do not clear or swap it here.
            sceGuFinish();
            sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
            ++frame_;
            sceGuStart(GU_DIRECT, list);
            sceGuTexFlush();
            g_textureStreamQa.barriers = g_textureStreamQa.barriers + 1;
            g_textureStreamQa.completedBytes = textureBytes_;
        }
        textureBytes_ -= oldest->size;
        fighterTextures_.erase(oldest);
        g_textureCacheQa.evicted=g_textureCacheQa.evicted+1;
        return true;
    };
    while (textureBytes_ + size > FighterTextureBudget) {
        if(!evict())return miss(1,size);
    }
    // A failed cache-table growth must not re-enter and mutate this vector
    // from the process allocation handler. Evict here, then retry explicitly.
    while (fighterTextures_.size() == fighterTextures_.capacity()) {
        try {
            fighterTextures_.reserve(std::max<std::size_t>(16, fighterTextures_.capacity() * 2));
        } catch (const std::bad_alloc&) {
            g_textureCacheQa.retryAllocation = g_textureCacheQa.retryAllocation + 1;
            if (!evict()) return miss(2, size);
        }
    }
    std::unique_ptr<PixelBlock[]> pixels;
    for(;;) {
        pixels.reset(new (std::nothrow) PixelBlock[size / sizeof(PixelBlock)]);
        if(pixels)break;
        // The rest of the game shares the PSP1000 heap with this bounded cache.
        // Reclaim completed-frame entries even below the cache's nominal limit.
        g_textureCacheQa.retryAllocation=g_textureCacheQa.retryAllocation+1;
        if(!evict())return miss(2,size);
    }
    if (const auto* resident = asset.pixels(pose)) std::memcpy(pixels.get(), resident, size);
    else while(!asset.decodePixels(pose, {reinterpret_cast<std::byte*>(pixels.get()), size})) {
        // zlib also needs temporary heap space while expanding a texture.
        g_textureCacheQa.retryDecode=g_textureCacheQa.retryDecode+1;
        if(!evict())return miss(3,size);
    }
    if (!(tint == ShadowTint{})) tintShadowPixels({reinterpret_cast<std::byte*>(pixels.get()), size}, tint);
    effectSurfacePixels({reinterpret_cast<std::byte*>(pixels.get()), size},surface);
    sceKernelDcacheWritebackRange(pixels.get(), size);
    const auto* address = pixels.get();
    fighterTextures_.push_back({&asset, pose.offset, frame_, size, std::move(pixels), tint,surface});
    textureBytes_ += size;
    return address;
}

void StageRenderer::effectPose(const FighterAsset& asset,const FighterPose& layer,Point origin,double zoom,
                               int facing,unsigned rotation,EffectBlend blend) {
    g_rendererQa.combatDraws=g_rendererQa.combatDraws+1;
    sceGuTexMode(GU_PSM_8888,0,0,GU_FALSE);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuTexFilter(smooth_?GU_LINEAR:GU_NEAREST,smooth_?GU_LINEAR:GU_NEAREST);
    const double radians=rotation*3.14159265358979323846/180.;
    const double cosine=std::cos(radians),sine=std::sin(radians);
    const auto draw=[&](EffectSurface surface) {
        for(const auto& pose:asset.poseTiles(layer)) {
            const auto* pixels=fighterTexture(asset,pose,{},surface);if(!pixels)continue;
            g_rendererQa.combatTiles=g_rendererQa.combatTiles+1;
            sceGuTexImage(0,pose.stride,pose.rows,pose.stride,pixels);
            auto* vertices=static_cast<Vertex*>(sceGuGetMemory(4*sizeof(Vertex)));
            for(unsigned i=0;i<4;++i) {
                const float u=i&1?pose.width:0,v=i&2?pose.height:0;
                const double x=(pose.x+u)*facing,y=pose.y+v;
                vertices[i]={u,v,0xffffffff,float(origin.x+(cosine*x-sine*y)*zoom+Pillar+rootShift.x),
                             float(origin.y+(sine*x+cosine*y)*zoom+rootShift.y),0};
            }
            sceGuDrawArray(GU_TRIANGLE_STRIP,VertexFormat,4,nullptr,vertices);
        }
    };
    void* original=nullptr;
    if(blend==EffectBlend::Lighten) {
        auto* vram=static_cast<std::byte*>(sceGeEdramGetAddr());original=vram+2*FrameBytes;
        sceGuCopyImage(GU_PSM_8888,0,0,Width,Height,Stride,vram+reinterpret_cast<std::uintptr_t>(drawBuffer_),
                       0,0,Stride,original);
        sceGuTexSync();sceGuTexFlush();
    }
    if(blend==EffectBlend::Hardlight) {
        // H(S,D,a)=a*max(2S-1,0)+D*(1-a*abs(2S-1)).
        // Coefficients are cached per RGBA texel; both passes remain on the GE.
        g_rendererQa.hardlight=g_rendererQa.hardlight+1;
        sceGuBlendFunc(GU_ADD,GU_FIX,GU_SRC_COLOR,0,0);draw(EffectSurface::HardlightMultiply);
        sceGuBlendFunc(GU_ADD,GU_FIX,GU_FIX,0xffffff,0xffffff);draw(EffectSurface::HardlightAdd);
    } else if(blend==EffectBlend::Screen) {
        g_rendererQa.screen=g_rendererQa.screen+1;
        sceGuBlendFunc(GU_ADD,GU_FIX,GU_ONE_MINUS_SRC_COLOR,0xffffff,0);draw(EffectSurface::Screen);
    } else {
        sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,blend==EffectBlend::Add?GU_FIX:GU_ONE_MINUS_SRC_ALPHA,0,
                       blend==EffectBlend::Add?0xffffff:0);
        draw(EffectSurface::Color);
    }
    if(original) {
        g_rendererQa.lighten=g_rendererQa.lighten+1;
        // max(D,(1-a)D+aS)=(1-a)D+a*max(D,S).
        sceGuTexFunc(GU_TFX_REPLACE,GU_TCC_RGB);sceGuTexFilter(GU_NEAREST,GU_NEAREST);
        sceGuBlendFunc(GU_MAX,GU_FIX,GU_FIX,0xffffff,0xffffff);
        const auto shifted=rootShift;rootShift={};
        drawTexture(original,Stride,256,-Pillar,0,Width,256,Width,256);
        drawTexture(static_cast<std::byte*>(original)+Stride*256*4,Stride,16,-Pillar,256,Width,16,Width,16);
        rootShift=shifted;
    }
    sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuTexFilter(smooth_?GU_LINEAR:GU_NEAREST,smooth_?GU_LINEAR:GU_NEAREST);
}

void StageRenderer::arena(const StageAsset& stage, CameraView camera, std::span<const FighterView> fighters,
                          SceneEffects effects, const UiAsset* cueArt, const FighterAsset* reactionArt,
                          const FighterAsset* combatArt,const FighterAsset* mapBlurArt,const FighterAsset* mapOpticalArt) {
    rootOffset({effects.shiftX,effects.shiftY});
    const bool blurActive=effects.blur.x>0||effects.blur.y>0;
    const auto blurIndex=std::ranges::find(MapBlurStrengths,effects.blur.x);
    const bool blurReady=blurActive&&effects.blur.y==0&&blurIndex!=MapBlurStrengths.end()&&mapBlurArt;
    if(blurActive&&!blurReady)g_blurRendererQa.missing=g_blurRendererQa.missing+1;
    const auto mapLayer=[&](StageLayer layer){
        sceGuTexMode(GU_PSM_8888,0,0,GU_FALSE);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
        sceGuTexFilter(smooth_?GU_LINEAR:GU_NEAREST,smooth_?GU_LINEAR:GU_NEAREST);
        if(layer==StageLayer::Front&&effects.optical.active){
            if(!mapOpticalArt||stage.optical().children().empty()){
                g_opticalRendererQa.missing=g_opticalRendererQa.missing+1;return;
            }
            unsigned kind=0,mask=effects.optical.mask;
            if(blurActive){
                if(!blurReady){g_opticalRendererQa.missing=g_opticalRendererQa.missing+1;return;}
                const auto index=unsigned(blurIndex-MapBlurStrengths.begin());kind=index+1;mask=effects.optical.cached[index];
                if(mask==OpticalView::Missing){g_opticalRendererQa.missing=g_opticalRendererQa.missing+1;return;}
            }
            opticalLayer(stage.optical(),*mapOpticalArt,kind,mask,camera,effects.shadeAlpha);return;
        }
        if(blurReady){
            if(std::ranges::any_of(stage.tiles(),[&](const auto& tile){return tile.layer==layer;}))
                blurLayer(*mapBlurArt,layer,unsigned(blurIndex-MapBlurStrengths.begin()),camera,effects.shadeAlpha);
        }else for(const auto& tile:stage.tiles())if(tile.layer==layer)drawTile(stage,tile,camera,0,effects.shadeAlpha);
    };
    const auto material=[&](bool smooth){
        sceGuTexMode(GU_PSM_8888,0,0,GU_FALSE);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
        sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
        sceGuTexFilter(smooth&&smooth_?GU_LINEAR:GU_NEAREST,smooth&&smooth_?GU_LINEAR:GU_NEAREST);
    };
    const auto drawCue=[&](const GroundCueView& cue){
        if(!cueArt||cue.cue==GroundCue::Count)return;
        material(true);sceGuTexMode(GU_PSM_4444,0,0,GU_FALSE);
        const bool rush=cue.cue>=GroundCue::Rush;char key[24];
        std::snprintf(key,sizeof(key),"ground-%u-%u",unsigned(cue.cue),cue.frame);
        if(const auto* sprite=cueArt->find(key)){
            const int originX=rush?112:64,originY=rush?64:(cue.cue>=GroundCue::AssistBleach?128:24);
            const float x=float((cue.origin.x-camera.origin.x-originX)*StageTextureScale*camera.zoom);
            const float y=float((cue.origin.y-camera.origin.y-originY)*StageTextureScale*camera.zoom);
            drawTexture(cueArt->pixels(*sprite),sprite->stride,sprite->rows,x,y,sprite->width,sprite->height,
                        float(sprite->width*camera.zoom),float(sprite->height*camera.zoom),cue.facing<0);
        }
        material(false);
    };
    const auto drawShadow=[&](const ShadowStamp& shadow){
        material(false);const bool flipped=shadow.facing<0;
        const auto alpha=unsigned(std::clamp(shadow.alpha*255,0.,255.));const auto tint=shadow.tint;
        const bool negative=tint.red<0||tint.green<0||tint.blue<0;
        sceGuTexFunc(negative?GU_TFX_MODULATE:GU_TFX_ADD,GU_TCC_RGBA);
        const auto color=(alpha<<24)|(negative?0x00ffffffu:unsigned(tint.red)|(unsigned(tint.green)<<8)|(unsigned(tint.blue)<<16));
        for(const auto& pose:shadow.asset->poseTiles(*shadow.pose)){
            const float x=float(((shadow.position.x-camera.origin.x)*StageTextureScale+(flipped?-pose.x-pose.width:pose.x))*camera.zoom);
            const float y=float(((shadow.position.y-camera.origin.y)*StageTextureScale+pose.y)*camera.zoom);
            if(const auto* pixels=fighterTexture(*shadow.asset,pose,negative?tint:ShadowTint{}))
                drawTexture(pixels,pose.stride,pose.rows,x,y,pose.width,pose.height,float(pose.width*camera.zoom),float(pose.height*camera.zoom),flipped,color);
        }
        material(false);
    };
    const auto drawFighter=[&](const FighterView& fighter){
        if(!fighter.visible)return;
        material(false);const bool flipped=fighter.facing<0;
        const double originX=(fighter.position.x-camera.origin.x)*StageTextureScale,originY=(fighter.position.y-camera.origin.y)*StageTextureScale;
        const auto drawPose=[&](const FighterPose& layer){
            for(const auto& pose:fighter.asset.poseTiles(layer)){
                const float x=float((originX+(flipped?-pose.x-pose.width:pose.x))*camera.zoom),y=float((originY+pose.y)*camera.zoom);
                const float width=float(pose.width*camera.zoom),height=float(pose.height*camera.zoom);
                if(x+rootShift.x>=ViewWidth||x+width+rootShift.x<=0||y+rootShift.y>=Height||y+height+rootShift.y<=0)continue;
                if(const auto* pixels=fighterTexture(fighter.asset,pose,fighter.tint))
                    drawTexture(pixels,pose.stride,pose.rows,x,y,pose.width,pose.height,width,height,flipped,(fighter.alpha<<24)|0x00ffffffu);
            }
        };
        drawPose(fighter.pose);
        if(fighter.screen){sceGuBlendFunc(GU_ADD,GU_FIX,GU_ONE_MINUS_OTHER_COLOR,0xffffff,0);drawPose(*fighter.screen);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);}
    };
    const auto drawReaction=[&](const ReactionCueView& effect){
        if(!reactionArt||effect.cue==ReactionCue::Count)return;
        material(true);const auto clip=MoveClip(9+unsigned(effect.cue));const auto& layer=reactionArt->frame(clip,effect.frame()).pose;
        const double originX=(effect.origin.x-camera.origin.x)*StageTextureScale,originY=(effect.origin.y-camera.origin.y)*StageTextureScale;
        const bool flipped=effect.facing<0;
        if(effect.cue==ReactionCue::GhostFoot)sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_FIX,0,0xffffff);
        for(const auto& pose:reactionArt->poseTiles(layer)){
            const float x=float((originX+(flipped?-pose.x-pose.width:pose.x))*camera.zoom),y=float((originY+pose.y)*camera.zoom);
            if(const auto* pixels=fighterTexture(*reactionArt,pose))drawTexture(pixels,pose.stride,pose.rows,x,y,pose.width,pose.height,float(pose.width*camera.zoom),float(pose.height*camera.zoom),flipped);
        }
        material(true);
    };
    const auto drawCombat=[&](const CombatEffectView& effect,bool background){
        if(!combatArt)return;
        const auto& pose=combatArt->frame(MoveClip(effect.art->clip),effect.frame()).pose;
        if(background){effectPose(*combatArt,pose,{0,0},1,effect.facing,effect.rotation,effect.effect->blend);return;}
        const double correction=effect.effect->randomRotation&&!effect.rotation?CombatArtOffsetX[effect.art->offsetsStart+effect.frame()]*(effect.offsetFacing-1):0;
        effectPose(*combatArt,pose,{((effect.origin.x-camera.origin.x)*StageTextureScale+correction)*camera.zoom,(effect.origin.y-camera.origin.y)*StageTextureScale*camera.zoom},camera.zoom,effect.facing,effect.rotation,effect.effect->blend);
    };
    const auto blackBack=[&]{
        material(false);drawBox(0,0,ViewWidth,Height,0xff000000|effects.backdropColor);
        if(effects.portrait&&effects.portraitFrame<69)if(const auto* pixels=fighterTexture(*effects.portraitAsset,*effects.portrait))drawPortrait(effects,camera.zoom,pixels);
    };
    const auto front=[&]{if(!effects.hideMap){mapLayer(StageLayer::FrontFixed);mapLayer(StageLayer::Front);}};
    if(effects.paint){
        using P=SourcePaint;
        std::uint64_t visited=14695981039346656037ull;
        const auto visit=[&](P::Key key){for(unsigned word:{unsigned(key),unsigned(key>>32)})for(unsigned byte=0;byte<4;++byte)visited=(visited^((word>>(byte*8))&255))*1099511628211ull;};
        const auto missing=[&]{g_paintRendererQa.missing=g_paintRendererQa.missing+1;};
        g_paintRendererQa.frames=g_paintRendererQa.frames+1;g_paintRendererQa.players=effects.paint->players().size();g_paintRendererQa.roots=effects.paint->stage().size();
        g_paintRendererQa.sprites=0;g_paintRendererQa.combat=0;g_paintRendererQa.ground=0;g_paintRendererQa.reactions=0;g_paintRendererQa.shadows=0;g_paintRendererQa.blackBack=0;g_paintRendererQa.backgrounds=0;
        const auto player=[&](P::Key key){
            visit(key);
            const auto id=P::id(key);
            switch(P::kind(key)){
            case P::Kind::Sprite:{auto f=std::ranges::find(fighters,key,&FighterView::displayKey);if(f!=fighters.end()){drawFighter(*f);g_paintRendererQa.sprites=g_paintRendererQa.sprites+1;}else missing();break;}
            case P::Kind::Ground:if(id<effects.ground.size()&&effects.ground[id].cue!=GroundCue::Count){drawCue(effects.ground[id]);g_paintRendererQa.ground=g_paintRendererQa.ground+1;}else missing();break;
            case P::Kind::Reaction:if(id<effects.reactions.size()&&effects.reactions[id].cue!=ReactionCue::Count){drawReaction(effects.reactions[id]);g_paintRendererQa.reactions=g_paintRendererQa.reactions+1;}else missing();break;
            case P::Kind::Shadow:{auto f=std::ranges::find_if(effects.shadows,[&](const auto& v){return unsigned(v.serial)==id;});if(f!=effects.shadows.end()){drawShadow(*f);g_paintRendererQa.shadows=g_paintRendererQa.shadows+1;}else missing();break;}
            case P::Kind::Combat:{auto f=std::ranges::find(effects.combat,id,&CombatEffectView::paintId);if(f!=effects.combat.end()){drawCombat(*f,false);g_paintRendererQa.combat=g_paintRendererQa.combat+1;}else missing();break;}
            default:missing();break;
            }
        };
        for(auto key:effects.paint->stage()){
            visit(key);
            switch(P::kind(key)){
            case P::Kind::Base:
                if(P::id(key)==0){if(!effects.hideMap)mapLayer(StageLayer::Background);}
                else if(P::id(key)==1){if(!effects.hideMap)mapLayer(StageLayer::Map);for(auto child:effects.paint->players())player(child);front();}
                break;
            case P::Kind::BlackBack:blackBack();g_paintRendererQa.blackBack=g_paintRendererQa.blackBack+1;break;
            case P::Kind::Combat:{auto f=std::ranges::find(effects.backgrounds,P::id(key),&CombatEffectView::paintId);if(f!=effects.backgrounds.end()){drawCombat(*f,true);g_paintRendererQa.backgrounds=g_paintRendererQa.backgrounds+1;}else missing();break;}
            default:missing();break;
            }
        }
        g_paintRendererQa.hashLow=unsigned(visited);g_paintRendererQa.hashHigh=unsigned(visited>>32);
    }else{
        for(const auto& view:effects.backgrounds)drawCombat(view,true);
        if(effects.hideMap){
            if(effects.fillBackdrop||effects.backdropColor)blackBack();
            else if(effects.portrait&&effects.portraitFrame<69)if(const auto* pixels=fighterTexture(*effects.portraitAsset,*effects.portrait))drawPortrait(effects,camera.zoom,pixels);
        }else{mapLayer(StageLayer::Background);mapLayer(StageLayer::Map);for(const auto& cue:effects.ground)if(cue.cue<GroundCue::Rush)drawCue(cue);}
        for(const auto& shadow:effects.shadows)drawShadow(shadow);
        for(const auto& fighter:fighters)drawFighter(fighter);
        for(const auto& cue:effects.ground)if(cue.cue>=GroundCue::Rush)drawCue(cue);
        for(const auto& effect:effects.reactions)drawReaction(effect);
        for(const auto& effect:effects.combat)drawCombat(effect,false);
        front();
    }
    shine(effects);
}

void StageRenderer::blurLayer(const FighterAsset& asset,StageLayer layer,unsigned index,CameraView camera,unsigned shade) {
    const auto clip=MoveClip(9+index*4+unsigned(layer));
    if(!asset.has(clip)){g_blurRendererQa.missing=g_blurRendererQa.missing+1;return;}
    g_blurRendererQa.layers=g_blurRendererQa.layers+1;
    double xOffset=0,yOffset=0,zoom=1;
    if(layer!=StageLayer::Background) {
        xOffset=-camera.origin.x;yOffset=-camera.origin.y;zoom=camera.zoom;
        if(layer==StageLayer::Front){xOffset-=camera.origin.x*.1;yOffset+=std::max(0.,(StageViewHeight-camera.origin.y)*.1);}
    }
    sceGuTexMode(GU_PSM_8888,0,0,GU_FALSE);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    // The original cached map Bitmap keeps its default smoothing=false.
    sceGuTexFilter(GU_NEAREST,GU_NEAREST);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    const auto color=0xff000000u|(255-shade)*0x010101u;
    for(const auto& pose:asset.poseTiles(asset.frame(clip,0).pose)) {
        const float x=float((pose.x+xOffset*StageTextureScale)*zoom),y=float((pose.y+yOffset*StageTextureScale)*zoom);
        const float width=float(pose.width*zoom),height=float(pose.height*zoom);
        // Cull before decoding: full map snapshots can span several screens.
        if(x+rootShift.x>=ViewWidth||x+rootShift.x+width<=0||y+rootShift.y>=Height||y+rootShift.y+height<=0) {
            g_blurRendererQa.culled=g_blurRendererQa.culled+1;continue;
        }
        if(const auto* pixels=fighterTexture(asset,pose)) {
            g_blurRendererQa.tiles=g_blurRendererQa.tiles+1;
            drawTexture(pixels,pose.stride,pose.rows,x,y,pose.width,pose.height,width,height,false,color);
        } else g_blurRendererQa.missing=g_blurRendererQa.missing+1;
    }
}

void StageRenderer::opticalLayer(const MapOpticalAsset& info,const FighterAsset& art,unsigned kind,unsigned mask,CameraView camera,unsigned shade){
    g_opticalRendererQa.layers=g_opticalRendererQa.layers+1;
    if(kind)g_opticalRendererQa.cached=g_opticalRendererQa.cached+1;
    else g_opticalRendererQa.normal=g_opticalRendererQa.normal+1;
    double offsetX=-camera.origin.x;offsetX-=camera.origin.x*.1;
    const double offsetY=-camera.origin.y+std::max(0.,(StageViewHeight-camera.origin.y)*.1);
    sceGuTexMode(GU_PSM_8888,0,0,GU_FALSE);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    const bool smooth=!kind&&smooth_;sceGuTexFilter(smooth?GU_LINEAR:GU_NEAREST,smooth?GU_LINEAR:GU_NEAREST);
    sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    const auto color=0xff000000u|(255-shade)*0x010101u;
    for(const auto& tile:info.tiles(kind)){
        const auto index=info.texture(tile,mask);if(index==OpticalView::Missing)continue;
        const auto& pose=art.frame(MoveClip(9),index).pose;
        const float x=float((tile.x+offsetX*StageTextureScale)*camera.zoom),y=float((tile.y+offsetY*StageTextureScale)*camera.zoom);
        const float width=float(pose.width*camera.zoom),height=float(pose.height*camera.zoom);
        if(x+rootShift.x>=ViewWidth||x+rootShift.x+width<=0||y+rootShift.y>=Height||y+rootShift.y+height<=0)continue;
        if(const auto* pixels=fighterTexture(art,pose)){
            g_opticalRendererQa.tiles=g_opticalRendererQa.tiles+1;
            drawTexture(pixels,pose.stride,pose.rows,x,y,pose.width,pose.height,width,height,false,color);
        }else g_opticalRendererQa.missing=g_opticalRendererQa.missing+1;
    }
}

void StageRenderer::beginUi() {
    sceGuTexMode(GU_PSM_4444, 0, 0, GU_FALSE);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
}

bool StageRenderer::transition(TransitionAsset& asset,unsigned frame){
    const auto* pixels=asset.pixels(frame);
    if(!pixels){g_transitionRendererQa.decodeFailures=g_transitionRendererQa.decodeFailures+1;return false;}
    alignas(16) static const auto palette=[](){std::array<unsigned,256> a{};for(unsigned i=0;i<256;++i)a[i]=i<<24;return a;}();
    sceKernelDcacheWritebackRange(pixels,asset.stride()*asset.rows());
    sceKernelDcacheWritebackRange(palette.data(),sizeof(palette));
    sceGuTexMode(GU_PSM_T8,0,0,GU_FALSE);sceGuClutMode(GU_PSM_8888,0,255,0);sceGuClutLoad(32,palette.data());
    sceGuTexFlush();sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    const auto saved=rootShift;rootShift={};
    drawTexture(pixels,asset.stride(),asset.rows(),0,0,asset.width(),asset.height(),ViewWidth,Height);
    rootShift=saved;sceGuTexMode(GU_PSM_4444,0,0,GU_FALSE);
    g_transitionRendererQa.draws=g_transitionRendererQa.draws+1;g_transitionRendererQa.frame=frame;return true;
}

void StageRenderer::image(const UiAsset& ui, const char* key, float x, float y, float width, float height, bool flip) {
    if (const auto* sprite = ui.find(key)) {
        drawTexture(ui.pixels(*sprite), sprite->stride, sprite->rows,
                    x * StageTextureScale, y * StageTextureScale, sprite->width, sprite->height,
                    width > 0 ? width * StageTextureScale : sprite->width,
                    height > 0 ? height * StageTextureScale : sprite->height, flip);
    }
}

void StageRenderer::imageTint(const UiAsset& ui,const char* key,float x,float y,float width,float height,
                              bool flip,unsigned alpha,unsigned rgbOffset){
    if(width<=0||height<=0||!alpha)return;
    if(const auto* sprite=ui.find(key)){
        sceGuTexFunc(GU_TFX_ADD,GU_TCC_RGBA);
        const auto color=(std::min(alpha,255u)<<24)|std::min(rgbOffset,255u)*0x010101u;
        drawTexture(ui.pixels(*sprite),sprite->stride,sprite->rows,x*StageTextureScale,y*StageTextureScale,
                    sprite->width,sprite->height,width*StageTextureScale,height*StageTextureScale,flip,color);
        sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    }
}

void StageRenderer::imageFillBottom(const UiAsset& ui, const char* key, float x, float y, float width, float height,
                                    bool flip, float fill) {
    if (fill <= 0) return;
    if (const auto* sprite = ui.find(key)) {
        drawTexture(ui.pixels(*sprite), sprite->stride, sprite->rows, x * StageTextureScale, y * StageTextureScale,
                    sprite->width, sprite->height, width * StageTextureScale, height * StageTextureScale,
                    flip, 0xffffffff, 1 - std::min(fill, 1.f));
    }
}

void StageRenderer::box(float x, float y, float width, float height, std::uint32_t color) {
    if (width <= 0 || height <= 0) return;
    drawBox(x * StageTextureScale, y * StageTextureScale, width * StageTextureScale, height * StageTextureScale, color);
}


#ifdef BVN_RESOURCE_DIAGNOSTICS
void StageRenderer::diagnosticText(const char* text, float x, float y) {
    alignas(64) static std::array<std::uint16_t, 128 * 128> atlas{};
    static bool ready = false;
    if (!ready) {
        for (unsigned c = 0; c < 256; ++c) for (unsigned row = 0; row < 8; ++row)
            for (unsigned col = 0; col < 8; ++col)
                atlas[((c / 16) * 8 + row) * 128 + (c % 16) * 8 + col] =
                    msx[c * 8 + row] & (0x80 >> col) ? 0xffff : 0;
        sceKernelDcacheWritebackRange(atlas.data(), sizeof(atlas));
        ready = true;
    }
    sceGuTexMode(GU_PSM_4444, 0, 0, GU_FALSE);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexImage(0, 128, 128, 128, atlas.data());
    sceGuTexFlush();
    x += Pillar;
    for (const unsigned char* c = reinterpret_cast<const unsigned char*>(text); *c; ++c, x += 8) {
        const float u = (*c % 16) * 8, v = (*c / 16) * 8;
        auto* vertices = static_cast<Vertex*>(sceGuGetMemory(2 * sizeof(Vertex)));
        vertices[0] = {u, v, 0xffffffff, x, y, 0};
        vertices[1] = {u + 8, v + 8, 0xffffffff, x + 8, y + 8, 0};
        sceGuDrawArray(GU_SPRITES, VertexFormat, 2, nullptr, vertices);
    }
}
#endif

void StageRenderer::end() {
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    listInFlight_ = false;
    drawBuffer_ = sceGuSwapBuffers();
    if (synchronized_) sceDisplayWaitVblankStart();
}

void StageRenderer::draw(const StageAsset& stage, CameraView camera, std::span<const FighterView> fighters, SceneEffects effects) {
    begin();
    arena(stage, camera, fighters, effects);
    end();
}

} // namespace bvn
