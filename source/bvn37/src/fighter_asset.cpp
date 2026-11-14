#include "resource_diagnostics.hpp"
#include <cerrno>
#include "fighter_asset.hpp"
#include "object_area_data.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <utility>
#include <lz4.h>
#include <zlib.h>

namespace bvn {
namespace {

#include "fighter_ai_hit_queries.hpp"
#include "fighter_defense_profiles.hpp"

struct Header {
    char magic[8];
    std::uint32_t frameCount, hitCount, eventCount, pixelStart;
    std::uint16_t clipCount, strikeCount;
    FighterStats stats;
};

struct LegacyHeader {
    char magic[8];
    std::uint32_t frameCount, pixelStart;
    FighterStats stats;
    float hitX, hitY;
    std::uint16_t damage, hurtFrames;
};

struct LegacyFrame {
    FighterPose pose;
    FighterBox body, hit;
    float velocityX;
    std::uint32_t changesVelocity;
};

struct LegacyStrike {
    float hitX, hitY;
    std::uint16_t damage, hurtFrames, hitType, flags;
};

static_assert(std::endian::native == std::endian::little);
static_assert(sizeof(Header) == 40 && sizeof(FighterClip) == 8 && sizeof(FighterFrame) == 48);
static_assert(sizeof(FighterHit) == 20 && sizeof(FighterEvent) == 12 && sizeof(FighterStrike) == 24);
constexpr std::size_t PreviousStrikeSize = 20;
static_assert(offsetof(FighterStrike, key) == PreviousStrikeSize);
constexpr std::size_t LegacyProjectileSize = 88;
static_assert(sizeof(ProjectileAsset) == 92 && offsetof(ProjectileAsset, ownerX) == LegacyProjectileSize);
constexpr std::size_t LegacyAttackerSize = 20;
static_assert(sizeof(AttackerAsset) == 24 && offsetof(AttackerAsset, key) == LegacyAttackerSize);
static_assert(sizeof(LegacyHeader) == 40 && sizeof(LegacyFrame) == 60 && sizeof(LegacyStrike) == 16);

constexpr std::size_t TextureBudget = 8 * 1024 * 1024;
constexpr std::size_t SmallTextureBudget = 512 * 1024;
constexpr std::size_t StreamedTextureLimit = 32 * 1024 * 1024;
constexpr std::size_t RequiredClips = static_cast<unsigned>(MoveClip::Hurt) + 1;

void loadStep(const char* step, unsigned index = 0) {
#ifdef BVN_RESOURCE_DIAGNOSTICS
    std::snprintf(resourceDiagnostics.loading.step, sizeof(resourceDiagnostics.loading.step), "%s", step);
    resourceDiagnostics.loading.index = index;
#else
    (void)step; (void)index;
#endif
}

std::size_t loadRead(void* target, std::size_t size, std::size_t count, std::FILE* stream) {
    const auto actual = std::fread(target, size, count, stream);
#ifdef BVN_RESOURCE_DIAGNOSTICS
    auto& context = resourceDiagnostics.loading;
    context.expected = size * count; context.actual = size * actual;
    if (actual != count) {
        const int error = errno;
        context.streamError = std::ferror(stream); context.eof = std::feof(stream);
        context.position = std::ftell(stream);
        errno = error;
    }
#endif
    return actual;
}

template<class Container>
bool read(std::FILE* stream, Container& items) {
    return loadRead(items.data(), sizeof(typename Container::value_type), items.size(), stream) == items.size();
}

bool textureExtent(unsigned n) {
    return n >= 4 && n <= 512 && std::has_single_bit(n);
}

bool positive(float n) {
    return std::isfinite(n) && n > 0;
}

bool valid(const FighterBox& box) {
    return std::isfinite(box.left) && std::isfinite(box.top) && std::isfinite(box.right) &&
           std::isfinite(box.bottom) && box.left <= box.right && box.top <= box.bottom;
}

bool validSpan(std::size_t start, std::size_t count, std::size_t size) {
    return start <= size && count <= size - start;
}

bool indexIn(float n, std::size_t size) {
    return n >= 0 && n < size && std::trunc(n) == n;
}

bool valid(const ProjectileAxis& axis) {
    return std::isfinite(axis.speed) && std::isfinite(axis.acceleration) && std::isfinite(axis.minimum) &&
           std::isfinite(axis.maximum) && !std::isinf(axis.onHit);
}

} // namespace

bool FighterAsset::open(const char* path, TextureResidency residency) {
#ifdef BVN_RESOURCE_DIAGNOSTICS
    diagnosticAttempt(path);
    const unsigned beforeFailure = resourceDiagnostics.failures;
    errno = 0;
    const auto load = [&]() -> bool {
#endif

    std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(path, "rb"), std::fclose);
    if (!file) return false;
    loadStep("SEEK SIZE");
    if (std::fseek(file.get(), 0, SEEK_END)) return false;
    long length = std::ftell(file.get());
#ifdef BVN_RESOURCE_DIAGNOSTICS
    resourceDiagnostics.loading.fileLength = length;
#endif
    loadStep("FILE LENGTH");
    if (length < long(sizeof(Header))) return false;
    std::rewind(file.get());
    loadStep("MAGIC");
    std::array<char, 8> magic{};
    if (!read(file.get(), magic)) return false;
    std::rewind(file.get());

    FighterAsset next;
    const bool transferFormat = std::memcmp(magic.data(),"BVNFGH16",8)==0 || std::memcmp(magic.data(),"BVNASSTX",8)==0;
    next.denseTextures_ = std::memcmp(magic.data(), "BVNFGH", 6) == 0 ||
                          std::memcmp(magic.data(), "BVNASSTH", 8) == 0 || std::memcmp(magic.data(), "BVNASSTL", 8) == 0 || transferFormat;
    std::uint64_t originalMetadata = 0;
    if (next.denseTextures_) {
        if (length < long(sizeof(Header) + sizeof(originalMetadata))) return false;
        length -= sizeof(originalMetadata);
        if (std::fseek(file.get(),length,SEEK_SET) ||
            std::fread(&originalMetadata,sizeof(originalMetadata),1,file.get()) != 1 || !originalMetadata) return false;
        if (transferFormat) {
            std::array<std::uint32_t,2> footer{};
            if(length<8 || std::fseek(file.get(),length-8,SEEK_SET) || !read(file.get(),footer) ||
               footer[1]!=0x33594c42u || !footer[0] || footer[0]>32768 ||
               length < long(8+footer[0]*12+sizeof(Header))) return false;
            length-=8+footer[0]*12;
            next.transferTextures_.resize(footer[0]);
            if(std::fseek(file.get(),length,SEEK_SET)||!read(file.get(),next.transferTextures_)||
               !std::ranges::is_sorted(next.transferTextures_,{},&TextureLayers::texture)||
               std::adjacent_find(next.transferTextures_.begin(),next.transferTextures_.end(),[](const auto& a,const auto& b){return a.texture==b.texture;})!=next.transferTextures_.end())return false;
        }
        std::rewind(file.get());
    }
    if (std::memcmp(magic.data(), "BVNASST1", 8) == 0 || std::memcmp(magic.data(), "BVNASSTH", 8) == 0 || std::memcmp(magic.data(), "BVNASSTL", 8) == 0 || std::memcmp(magic.data(),"BVNASSTX",8)==0) {
        next.assister_ = true;
        if (!next.readCurrent(file.get(), length, 11, residency, transferFormat || std::memcmp(magic.data(), "BVNASSTL", 8) == 0)) return false;
    } else {
        if ((std::memcmp(magic.data(), "BVNFGT", 6) != 0 && std::memcmp(magic.data(), "BVNFGH", 6) != 0) ||
            magic[6] < '0' || magic[6] > '1' || magic[7] < '0' || magic[7] > '9') return false;
        const unsigned version = (magic[6] - '0') * 10 + magic[7] - '0';
        if (version < 2 || version > (transferFormat?16u:15u)) return false;
        if (!(version == 2 ? next.readLegacy(file.get(), length) :
                            next.readCurrent(file.get(), length, transferFormat?15:version, residency))) return false;
    }
    for(const auto& layers:next.transferTextures_) {
        const auto modes=std::uint64_t(layers.modesLow)|(std::uint64_t(layers.modesHigh)<<32);
        const auto count=unsigned(modes&15u);if(!count)return false;
        for(unsigned p=0;p<count;++p){const auto m=(modes>>(4+4*p))&15u;
            if(m!=0&&m!=3&&m!=4&&m!=5&&m!=6&&m!=8&&m!=9&&m!=10&&m!=15)return false;}
        if(count<15 && (modes>>(4+4*count)))return false;
    }
    // Presentation-only density changes retain the qualified script/object-area
    // identity; all logical tables are copied unchanged by the raster converter.
    if (next.denseTextures_) next.metadataIdentity_ = originalMetadata;
    if(!next.transferTextures_.empty()&&next.transferTextures_.back().texture>=next.textures_.size())return false;
#ifdef BVN_RESOURCE_DIAGNOSTICS
    std::snprintf(next.diagnosticPath_.data(), next.diagnosticPath_.size(), "%s", path);
#endif
    next.initializeAiHits();
    if (next.streamedStart_) {
        next.streamed_ = std::move(file);
#ifdef __PSP__
#ifdef BVN_RESOURCE_DIAGNOSTICS
        if (!resourceDiagnostics.synchronousOnly) {
#endif
        const auto window = residency == TextureResidency::LowMemory ? 128 * 1024 : TextureStream::DefaultWindowBytes;
        std::unique_ptr<TextureStream> reader(new (std::nothrow) TextureStream(
            path, next.streamedStart_, length - next.streamedStart_, next.streamedBuffer_.size(), window));
        if (reader && reader->ready()) next.readAhead_ = std::move(reader);
#ifdef BVN_RESOURCE_DIAGNOSTICS
        }
#endif
#endif
    }
    *this = std::move(next);
    return true;
#ifdef BVN_RESOURCE_DIAGNOSTICS
    };
    const bool loaded = load();
    if (!loaded && resourceDiagnostics.failures == beforeFailure) diagnosticFailure(path, resourceDiagnostics.loading.step, errno);
    return loaded;
#endif
}

std::uint32_t FighterAsset::aiHitCommands(std::uint32_t hitKey) {
    const auto entries = std::span(AiHitQueries);
    const auto found = std::ranges::lower_bound(entries, hitKey, {}, &AiHitQuery::key);
    return found != entries.end() && found->key == hitKey ? found->commands : 0;
}

void FighterAsset::initializeAiHits() {
    // Include defined but unused original hits: they still participate in getHitVOLike.
    // Identify the immutable combat data, so renamed or relocated banks behave identically.
    std::uint64_t signature = 14695981039346656037ULL;
    const auto append = [&](std::span<const std::byte> bytes) {
        for (const auto byte : bytes) signature = (signature ^ std::to_integer<unsigned>(byte)) * 1099511628211ULL;
    };
    append(std::as_bytes(std::span(&stats_, 1)));
    append(std::as_bytes(std::span(strikes_)));
    const auto profiles = std::span(FighterDefenseProfiles);
    const auto profile = std::ranges::lower_bound(profiles, signature, {}, &FighterDefenseProfile::signature);
    if (profile != profiles.end() && profile->signature == signature) defenseType_ = profile->type;
    const auto entries = std::span(AiUnusedHitQueries);
    const auto found = std::ranges::lower_bound(entries, signature, {}, &AiUnusedHits::signature);
    if (found != entries.end() && found->signature == signature) {
        aiUnusedBreaks_ = found->breaksGuard;
        aiUnusedKnockdowns_ = found->knocksDown;
    }
}

bool FighterAsset::readCurrent(std::FILE* stream, std::size_t length, unsigned version, TextureResidency residency, bool forceLz4) {
    loadStep("HEADER");
    Header header{};
    if (loadRead(&header, sizeof(header), 1, stream) != 1 ||
        !header.frameCount || header.frameCount > 32768 || header.hitCount > 32768 ||
        header.eventCount > 32768 || header.clipCount < (assister_ ? 1 : RequiredClips) || header.clipCount > 512 ||
        header.strikeCount > 4096) return false;

    const bool hasProjectiles = version >= 5;
    const bool hasAttackers = version >= 6;
    const bool hasGuard = version >= 4;
    const bool hasRanges = version >= 8;
    const bool extended = version >= 9;
    const bool compressed = version >= 10;
    const bool namedAttackers = version >= 11;
    const bool hasHurtEscape = version >= 14;
    lz4Textures_ = forceLz4 || version == 13 || version == 15;
    loadStep("COUNTS");
    std::uint16_t projectileCount = 0, attackerCount = 0;
    std::uint32_t textureCount = 0, tileCount = 0;
    if (hasProjectiles && (loadRead(&projectileCount, sizeof(projectileCount), 1, stream) != 1 || projectileCount > 512)) return false;
    if (hasAttackers && (loadRead(&attackerCount, sizeof(attackerCount), 1, stream) != 1 || attackerCount > 512)) return false;
    if (compressed && (loadRead(&textureCount, sizeof(textureCount), 1, stream) != 1 ||
                       loadRead(&tileCount, sizeof(tileCount), 1, stream) != 1 || tileCount > 32768 ||
                       !textureCount || textureCount > header.frameCount + tileCount)) return false;
    if (hasHurtEscape && loadRead(&hurtEscape_, sizeof(hurtEscape_), 1, stream) != 1) return false;
    const std::size_t bindings = version >= 12 ? defaults_.size() :
        compressed ? unsigned(FighterCommand::CatchAttack) : extended ? 18 :
        version >= 7 ? 13 : hasProjectiles ? 10 : hasGuard ? 7 : 4;
    const std::size_t ranges = hasRanges ? bindings : 0;
    const std::size_t strikeSize = extended ? sizeof(FighterStrike) : hasGuard ? PreviousStrikeSize : sizeof(LegacyStrike);
    const std::size_t projectileSize = hasAttackers ? sizeof(ProjectileAsset) : LegacyProjectileSize;
    const std::size_t attackerSize = namedAttackers ? sizeof(AttackerAsset) : LegacyAttackerSize;
    const std::size_t headerSize = sizeof(header) + sizeof(std::uint16_t) * (unsigned(hasProjectiles) + unsigned(hasAttackers))
        + (compressed ? sizeof(textureCount) + sizeof(tileCount) : 0) + (hasHurtEscape ? sizeof(hurtEscape_) : 0);
    const std::size_t tableEnd = headerSize + bindings * sizeof(MoveClip) +
        ranges * sizeof(FighterBox) + header.clipCount * sizeof(FighterClip) +
        header.frameCount * sizeof(FighterFrame) + header.hitCount * sizeof(FighterHit) +
        header.strikeCount * strikeSize + header.eventCount * sizeof(FighterEvent) + projectileCount * projectileSize +
        attackerCount * attackerSize + textureCount * sizeof(PackedTexture) + tileCount * sizeof(FighterPose);
    loadStep("TABLE LAYOUT");
    if (header.pixelStart != ((tableEnd + 15) & ~std::size_t(15)) || header.pixelStart >= length) return false;

    loadStep("ALLOC TABLES");
    stats_ = header.stats;
    clips_.resize(header.clipCount);
    frames_.resize(header.frameCount);
    hits_.resize(header.hitCount);
    strikes_.resize(header.strikeCount);
    events_.resize(header.eventCount);
    projectiles_.resize(projectileCount);
    attackers_.resize(attackerCount);
    textures_.resize(textureCount);
    tiles_.resize(tileCount);
    defaults_.fill(MoveClip::Missing);
    loadStep("BINDINGS");
    if (loadRead(defaults_.data(), sizeof(MoveClip), bindings, stream) != bindings) return false;
    loadStep("AI RANGES");
    if (ranges && loadRead(aiRanges_.data(), sizeof(FighterBox), ranges, stream) != ranges) return false;
    loadStep("CLIPS"); if (!read(stream, clips_)) return false;
    loadStep("FRAMES"); if (!read(stream, frames_)) return false;
    loadStep("HITS"); if (!read(stream, hits_)) return false;
    loadStep("STRIKES");
    if (extended) {
        if (!read(stream, strikes_) || std::ranges::any_of(strikes_, [](const auto& strike) { return !strike.key; })) return false;
    } else if (hasGuard) {
        for (auto& strike : strikes_) {
            if (loadRead(&strike, strikeSize, 1, stream) != 1 || strike.flags > 7 || strike.slowTimeMs) return false;
        }
    } else {
        for (FighterStrike& strike : strikes_) {
            LegacyStrike old{};
            if (loadRead(&old, sizeof(old), 1, stream) != 1) return false;
            strike = {old.hitX, old.hitY, old.damage, old.hurtFrames, old.hitType, old.flags, 0, 0, 0};
        }
    }
    loadStep("EVENTS");
    if (!read(stream, events_)) return false;
    loadStep("PROJECTILES");
    for (ProjectileAsset& projectile : projectiles_) {
        if (loadRead(&projectile, projectileSize, 1, stream) != 1) return false;
    }
    loadStep("ATTACKERS");
    for (AttackerAsset& attacker : attackers_) {
        if (loadRead(&attacker, attackerSize, 1, stream) != 1 ||
            (namedAttackers ? !attacker.key : attacker.targetAxes > 3)) return false;
    }
    loadStep("TEXTURE TABLE");
    if(!read(stream,textures_)||!read(stream,tiles_)||std::fseek(stream,0,SEEK_SET))return false;
    loadStep("METADATA HASH");
    metadataIdentity_=14695981039346656037ull;
    std::array<unsigned char,256> chunk{};std::size_t remaining=header.pixelStart;
    while(remaining){
        const auto count=std::min(remaining,chunk.size());if(loadRead(chunk.data(),1,count,stream)!=count)return false;
        for(std::size_t i=0;i<count;++i){metadataIdentity_^=chunk[i];metadataIdentity_*=1099511628211ull;}
        remaining-=count;
    }

#ifdef BVN_RESOURCE_DIAGNOSTICS
    resourceDiagnostics.loading.metadata = metadataIdentity_;
#endif
    return readPixels(stream,length,header.pixelStart,residency);
}

bool FighterAsset::hasObjectAreas()const{
    const auto found=std::ranges::lower_bound(ObjectAreaBanks,metadataIdentity_,{},&ObjectAreaBank::signature);
    return found!=ObjectAreaBanks.end()&&found->signature==metadataIdentity_&&found->count==attackers_.size();
}
std::optional<OpticalRect> FighterAsset::objectArea(unsigned index)const{
    const auto found=std::ranges::lower_bound(ObjectAreaBanks,metadataIdentity_,{},&ObjectAreaBank::signature);
    if(found==ObjectAreaBanks.end()||found->signature!=metadataIdentity_||index>=found->count)return {};
    return ObjectAreas[found->first+index];
}

bool FighterAsset::readLegacy(std::FILE* stream, std::size_t length) {
    LegacyHeader header{};
    if (loadRead(&header, sizeof(header), 1, stream) != 1 || !header.frameCount || header.frameCount > 8192) return false;
    const std::size_t tableEnd = sizeof(header) + RequiredClips * sizeof(FighterClip) + header.frameCount * sizeof(LegacyFrame);
    if (header.pixelStart != ((tableEnd + 15) & ~std::size_t(15)) || header.pixelStart >= length) return false;

    stats_ = header.stats;
    defaults_.fill(MoveClip::Missing);
    defaults_[static_cast<unsigned>(FighterCommand::Attack)] = MoveClip::GroundAttack;
    clips_.resize(RequiredClips);
    std::vector<LegacyFrame> legacy(header.frameCount);
    if (!read(stream, clips_) || !read(stream, legacy)) return false;
    clips_[static_cast<unsigned>(MoveClip::Land)].repeat = FinishClip;
    clips_[static_cast<unsigned>(MoveClip::GroundAttack)].repeat = FinishClip;
    strikes_.push_back({header.hitX, header.hitY, header.damage, header.hurtFrames, 0, 0, 0, 0, 0});
    frames_.reserve(legacy.size());
    for (const LegacyFrame& old : legacy) {
        if (!valid(old.hit) || !std::isfinite(old.velocityX) || old.changesVelocity > 1) return false;
        const bool hit = !old.hit.empty();
        frames_.push_back({old.pose, old.body, static_cast<std::uint32_t>(hits_.size()),
                           static_cast<std::uint32_t>(events_.size()), std::uint16_t(hit),
                           std::uint16_t(old.changesVelocity)});
        if (hit) hits_.push_back({old.hit, 0, 0});
        if (old.changesVelocity) events_.push_back({FighterOp::Velocity, {}, FinishClip, old.velocityX, 0});
    }
    return readPixels(stream, length, header.pixelStart);
}

bool FighterAsset::readPixels(std::FILE* stream, std::size_t length, std::size_t start, TextureResidency residency) {
    loadStep("PAYLOAD SIZE");
    const std::size_t payload = length - start;
    if (payload > StreamedTextureLimit * (transferTextures_.empty()?1:2)) return false;
    if (!validate(payload)) return false;
    const auto residentLimit = residency == TextureResidency::LowMemory && !textures_.empty() ?
        SmallTextureBudget : TextureBudget;
    if (payload > residentLimit) {
        if (textures_.empty()) return false;
        std::size_t largest = 0;
        loadStep("PACKED BOUND");
        for (const auto& texture : textures_) {
            const std::size_t bound = lz4Textures_ ? LZ4_compressBound(texture.rawSize) : compressBound(texture.rawSize);
            if (texture.packedSize > bound) return false;
            largest = std::max(largest, std::size_t(texture.packedSize));
        }
        streamedStart_ = start;
        streamedBuffer_.resize(largest);
        return true;
    }
    loadStep("PIXEL PAYLOAD");
    if (std::fseek(stream, static_cast<long>(start), SEEK_SET)) return false;
    if (!textures_.empty()) {
        compressed_.resize(payload);
        return read(stream, compressed_);
    }
    if (payload % sizeof(PixelBlock)) return false;
    pixels_.resize(payload / sizeof(PixelBlock));
    return read(stream, pixels_);
}

bool FighterAsset::decodePixels(const FighterPose& pose, std::span<std::byte> output) const {
    if (pose.offset >= textures_.size() || output.size() != textures_[pose.offset].rawSize) return false;
    const auto& texture = textures_[pose.offset];
    const std::byte* input = nullptr;
    if (streamed_) {
#ifdef __PSP__
        if (readAhead_) input = readAhead_->get(texture.offset, texture.packedSize);
#endif
        if (!input) {
            if (std::fseek(streamed_.get(), static_cast<long>(streamedStart_ + texture.offset), SEEK_SET) ||
                loadRead(streamedBuffer_.data(), 1, texture.packedSize, streamed_.get()) != texture.packedSize) return false;
            input = streamedBuffer_.data();
        }
    } else {
        input = compressed_.data() + texture.offset;
    }
    if (lz4Textures_) {
        return LZ4_decompress_safe(reinterpret_cast<const char*>(input), reinterpret_cast<char*>(output.data()),
                                   int(texture.packedSize), int(output.size())) == int(output.size());
    }
    uLongf bytes = static_cast<uLongf>(output.size());
    return uncompress(reinterpret_cast<Bytef*>(output.data()), &bytes,
                      reinterpret_cast<const Bytef*>(input), texture.packedSize) == Z_OK &&
           bytes == output.size();
}

std::optional<unsigned> FighterAsset::nextForm(MoveClip id, unsigned current) const {
    if (!has(id)) return {};
    for (unsigned i = 0; i < clip(id).count; ++i) {
        for (const auto& event : events(frame(id, i))) {
            if (event.op == FighterOp::SwitchForm || event.op == FighterOp::SwapForm) {
                return event.target == FinishClip ? current + 1 : event.target;
            }
        }
    }
    return {};
}

bool FighterAsset::validate(std::size_t payload) const {
    loadStep("CHECK STATS");
    if (hurtEscape_ != MoveClip::Missing && !has(hurtEscape_)) return false;
    if (!positive(stats_.speed) || !positive(stats_.jumpPower) || !positive(stats_.weight)) return false;
    if (!std::ranges::all_of(aiRanges_, [](const FighterBox& box) { return valid(box); })) return false;
    if (assister_ && (attackers_.empty() || attackers_.front().clip != MoveClip::Idle ||
                      attackers_.front().initial != 1)) return false;

    std::size_t first = 0;
    for (std::size_t i = 0; i < clips_.size(); ++i) {
        loadStep("CHECK CLIPS", i);
        const FighterClip& clip = clips_[i];
        if (clip.first != first || (!clip.count && (assister_ || i < RequiredClips)) ||
            (clip.count && (!clip.timelineStart || (clip.repeat != FinishClip && clip.repeat >= clip.count)))) return false;
        first += clip.count;
    }
    if (first != frames_.size()) return false;
    loadStep("CHECK COMMANDS");
    for (MoveClip binding : defaults_) {
        if (binding != MoveClip::Missing && (assister_ || !has(binding))) return false;
    }
    if (has(command(FighterCommand::Guard)) != has(command(FighterCommand::GuardRecover))) return false;

    loadStep("CHECK TEXTURES");
    if (!textures_.empty()) {
        std::size_t position = 0;
        for (const auto& texture : textures_) {
            if (texture.offset != position || !texture.packedSize ||
                !validSpan(position, texture.packedSize, payload) || texture.rawSize > 60u * 512 * 512) return false;
            position += texture.packedSize;
        }
        if (position != payload) return false;
    }
    const auto validPose = [&](const FighterPose& pose) {
        loadStep("CHECK POSE");
        if (!std::isfinite(pose.x) || !std::isfinite(pose.y) ||
            !textureExtent(pose.stride) || !textureExtent(pose.rows) || !pose.width || !pose.height ||
            pose.width > pose.stride || pose.height > pose.rows) return false;
        const auto planes=textureLayers(pose);
        return textures_.empty() ? pose.offset % sizeof(PixelBlock) == 0 &&
            validSpan(pose.offset, textureSize(pose), payload) :
            pose.offset < textures_.size() &&
            (textures_[pose.offset].rawSize == planes*textureSize(pose) ||
             (denseTextures_ && pose.stride <= 256 && pose.rows <= 256 &&
              textures_[pose.offset].rawSize == planes*4 * textureSize(pose)));
    };
    if (!std::ranges::all_of(tiles_, validPose)) return false;
    for (const FighterFrame& frame : frames_) {
        loadStep("CHECK FRAME", unsigned(&frame - frames_.data()));
        const FighterPose& pose = frame.pose;
        if (!valid(frame.body) ||
            !validSpan(frame.hitStart, frame.hitCount, hits_.size()) ||
            !validSpan(frame.eventStart, frame.eventCount, events_.size())) return false;
        if (pose.tiled()) {
            if (pose.x != 0 || pose.y != 0 || pose.height || pose.rows || !pose.width ||
                !validSpan(pose.offset, pose.width, tiles_.size())) return false;
        } else if (!validPose(pose)) {
            return false;
        }
        loadStep("CHECK FRAME OPS", unsigned(&frame - frames_.data()));
        const auto script = events(frame);
        for (std::size_t i = 0; i < script.size(); ++i) {
            if (isFighterBranch(script[i].op) && script[i].target >= script.size() - i) return false;
            if (script[i].op == FighterOp::AttackerPushOut || script[i].op == FighterOp::IfDisplayOverlap) {
                const auto probe = script[i].op == FighterOp::AttackerPushOut ? script[i].target : script[i].x;
                if (!indexIn(probe, FinishClip) || std::ranges::none_of(hits(frame), [&](const auto& hit) {
                    return hit.probe() && hit.strikeIndex == probe;
                })) return false;
            }
        }
    }
    loadStep("CHECK HITS");
    for (const FighterHit& hit : hits_) {
        if (!valid(hit.box) || hit.box.empty() || hit.kind > 2 ||
            (hit.strike() && hit.strikeIndex >= strikes_.size()) ||
            (hit.display() && (assister_ || hit.strikeIndex != FinishClip))) return false;
    }
    loadStep("CHECK STRIKES");
    for (const FighterStrike& strike : strikes_) {
        if (!std::isfinite(strike.hitX) || !std::isfinite(strike.hitY) ||
            (strike.hitType > 11 && strike.hitType != 0xffff) || strike.flags > 63) return false;
    }
    for (const FighterEvent& event : events_) {
        loadStep("CHECK EVENT", unsigned(&event - events_.data()));
        if (event.op >= FighterOp::Count || !std::isfinite(event.x) || !std::isfinite(event.y) ||
            (event.op == FighterOp::Allow && (event.command >= FighterCommand::Count ||
                                             (event.target != FinishClip && !has(MoveClip(event.target))))) ||
            (event.op == FighterOp::Fire && event.target >= projectiles_.size()) ||
            (event.op == FighterOp::Summon && event.target >= attackers_.size()) ||
            (event.op == FighterOp::Shine && !indexIn(event.x, 0x1000000))) return false;
        const unsigned option = static_cast<unsigned>(event.command);
        switch (event.op) {
        case FighterOp::Allow:
            if (event.y == 1) {
                if ((event.command != FighterCommand::Super && event.command != FighterCommand::SuperUp &&
                     event.command != FighterCommand::SuperDown && event.command != FighterCommand::SuperAir) ||
                    !indexIn(event.x, FinishClip) || event.target == FinishClip) return false;
            } else if (event.x || event.y) return false;
            break;
        case FighterOp::Velocity:
            if (option > 2) return false;
            break;
        case FighterOp::Damping:
            if (option > 1) return false;
            break;
        case FighterOp::Cutin:
            if (option < 1 || option > unsigned(CutinKind::GhostStep) || !indexIn(event.x, 2) ||
                (option == unsigned(CutinKind::GhostStep) && (event.x || event.target != FinishClip)) || (event.target != FinishClip &&
                (!has(MoveClip(event.target)) || frame(MoveClip(event.target), 0).pose.tiled()))) return false;
            break;
        case FighterOp::Landing:
        case FighterOp::Goto:
        case FighterOp::Probe:
            if (!has(MoveClip(event.target))) return false;
            if (event.op == FighterOp::Landing && option > 1) return false;
            if (event.op == FighterOp::Probe && !indexIn(event.x, FinishClip)) return false;
            break;
        case FighterOp::EndCutin:
            if(option>3||event.target!=FinishClip||event.x||event.y)return false;
            break;
        case FighterOp::HurtAction:
            if (option > 1 || event.x || event.y ||
                (event.target == FinishClip ? option != 0 : !has(MoveClip(event.target)))) return false;
            break;
        case FighterOp::Recover:
            if (option || event.x || event.y ||
                (event.target != FinishClip && !has(MoveClip(event.target)))) return false;
            break;
        case FighterOp::BranchHit:
            if (!has(MoveClip(event.target)) || !indexIn(event.x, strikes_.size()) ||
                !indexIn(event.y, clips_.size()) || !has(MoveClip(static_cast<unsigned>(event.y)))) return false;
            break;
        case FighterOp::QiOnHit:
            if (event.target >= strikes_.size()) return false;
            break;
        case FighterOp::MoveToTarget:
            if (option > 7) return false;
            break;
        case FighterOp::TargetAnchor:
            if (option || event.target == FinishClip) return false;
            break;
        case FighterOp::MoveTarget:
            if (option > 1 || event.x < 0 || event.y < 0 ||
                (!option && (event.x || event.y)) || (event.target == FinishClip && option)) return false;
            break;
        case FighterOp::IfTarget:
            if (option > 3 || event.x || event.y) return false;
            break;
        case FighterOp::Gravity:
            if (option > 1 || event.target != FinishClip || !indexIn(event.x, 2) || event.y) return false;
            break;
        case FighterOp::IfDisplayOverlap:
            if (option || event.y) return false;
            break;
        case FighterOp::TargetOffset:
            if (option || event.target != FinishClip) return false;
            break;
        case FighterOp::TargetVisible:
            if (option || !indexIn(event.x, 2)) return false;
            break;
        case FighterOp::UlquiorraDashSound:
            if (option || event.target != FinishClip || !indexIn(event.x, 2) || event.y) return false;
            break;
        case FighterOp::AttackerPushOut:
            if (option || event.target == FinishClip || event.x || event.y) return false;
            break;
        case FighterOp::AttackerProbe:
            if (option || !indexIn(event.x, FinishClip)) return false;
            break;
        case FighterOp::AttackerGoto:
            if (option > 2 || (option == 2 ? !indexIn(event.x, strikes_.size()) : event.x != 0) || event.y) return false;
            break;
        case FighterOp::AttackerRange:
            if (option > 1 || event.x > event.y) return false;
            break;
        case FighterOp::AttackerCutin:
            if (!assister_ || option || !indexIn(event.x, 3)) return false;
            break;
        case FighterOp::FighterBuff:
            if (assister_ || option > 2 || event.target != FinishClip || !indexIn(event.y, 3601) || event.y == 0 ||
                !indexIn(option ? event.x : -event.x, 1001)) return false;
            break;
        case FighterOp::OwnerBuff:
            if (!assister_ || option > 2 || event.target != FinishClip ||
                !indexIn(event.x, 1001) || !indexIn(event.y, 3601) || event.y == 0) return false;
            break;
        case FighterOp::OwnerHeal:
            if (!assister_ || option > 1 || event.x <= 0 || event.x > 1000 || event.y) return false;
            break;
        case FighterOp::AssistWind:
        case FighterOp::AttackerRemoveAtSide:
            if (option || event.target != FinishClip || event.x || event.y) return false;
            break;
        case FighterOp::AssistSwap:
            if (!assister_ || option > 1 || event.target != FinishClip || event.x || event.y) return false;
            break;
        case FighterOp::AttackerHurtBind:
            if (!assister_ || option || event.target >= attackers_.size() || event.y ||
                !indexIn(event.x, clip(attackers_[event.target].clip).count)) return false;
            break;
        case FighterOp::AttackerHoming:
            if (option || event.target == FinishClip || event.x <= 0 || event.y <= 0) return false;
            break;
        case FighterOp::AttackerRandomGoto:
            if (option || event.target == FinishClip || !indexIn(event.x, FinishClip) || !indexIn(event.y, FinishClip)) return false;
            break;
        case FighterOp::AttackerRandomMove:
            if (option || event.target == FinishClip || event.x < 0) return false;
            break;
        case FighterOp::TargetHealthPower:
            if (!assister_ || option || event.target >= strikes_.size() || !indexIn(event.x, 10001) || event.y) return false;
            break;
        case FighterOp::AttackerCapture:
            if (option || event.target >= strikes_.size() || event.x || event.y) return false;
            break;
        case FighterOp::AttackerMiss:
        case FighterOp::AttackerRelease:
            if (option || event.target != FinishClip || event.x || event.y) return false;
            break;
        case FighterOp::AttackerQiCap:
            if (option > 1 || event.target != FinishClip || event.x || event.y) return false;
            break;
        case FighterOp::HealPercent:
            if (option || event.target != FinishClip || !indexIn(event.x, 10001) || event.x == 0 || event.y) return false;
            break;
        case FighterOp::ControlAttacker: {
            if (option > 2 || event.target >= attackers_.size() || event.y) return false;
            const auto target = attackers_[event.target].clip;
            if (!has(target) || (option == 1 ? event.x != 0 : !indexIn(event.x, clip(target).count)) ||
                (option == 2 && frame(target, unsigned(event.x)).eventCount)) return false;
            break;
        }
        case FighterOp::IfFlight:
            if (option || event.x || event.y) return false;
            break;
        case FighterOp::FlightMode:
            if (option || event.target != FinishClip || !indexIn(event.x, 2) || event.y) return false;
            break;
        case FighterOp::FlightPose:
        case FighterOp::ScreenPose:
        case FighterOp::BankaiPose:
        case FighterOp::WaterPose:
        case FighterOp::TwinLionPose:
        case FighterOp::FacingPose:
            if (option || event.y || !has(MoveClip(event.target)) ||
                !indexIn(event.x, clip(MoveClip(event.target)).count)) return false;
            break;
        case FighterOp::TargetVelocity:
        case FighterOp::TargetDamping:
            if (option || event.target != FinishClip) return false;
            break;
        case FighterOp::TimelineValue:
            if (option >= FirstTimelineCounter + TimelineCounterCount || event.target != FinishClip) return false;
            if (option < FirstTimelineCounter) {
                if ((option == 3 && !indexIn(event.x, 2)) ||
                    (event.y != 0 && (option != 0 || event.y != 1))) return false;
            } else if (!indexIn(event.y, 2) || (event.y ? std::abs(event.x) != 1 : !indexIn(event.x, FinishClip))) {
                return false;
            }
            break;
        case FighterOp::TimelineMove:
            if (!indexIn(float(option) - FirstTimelineCounter, TimelineCounterCount) ||
                event.y || event.target != FinishClip) return false;
            break;
        case FighterOp::Kamui:
        case FighterOp::AttackerRecoverOwner:
            if (option || event.x || event.y || event.target != FinishClip) return false;
            break;
        case FighterOp::IfWaterInput:
            if (option || event.x || event.y) return false;
            break;
        case FighterOp::WaterWalk:
            if (option || !indexIn(event.x, 2) || event.y || event.target != FinishClip) return false;
            break;
        case FighterOp::HinataState:
            if (option > 2 || event.x || event.y ||
                (option == 2 ? event.target >= strikes_.size() : event.target != FinishClip)) return false;
            break;
        case FighterOp::FollowAttackerY:
            if (option || event.y || event.target >= attackers_.size()) return false;
            break;
        case FighterOp::AttackerWaterEnergy:
            if (option || event.x <= 0 || !has(MoveClip(event.target)) ||
                !indexIn(event.y, clips_.size()) || !has(MoveClip(unsigned(event.y)))) return false;
            break;
        case FighterOp::IfTimelineValue:
            if (option > 2 || (event.y &&
                (option > 1 || !indexIn(event.y - FirstTimelineCounter, TimelineCounterCount) ||
                 !indexIn(event.x, FinishClip)))) return false;
            break;
        case FighterOp::TimelineQi:
            if (option || event.target != FinishClip || event.x || event.y) return false;
            break;
        case FighterOp::TransferQi:
            if (option || event.target != FinishClip || event.x <= 0 || event.x > 300 || event.y) return false;
            break;
        case FighterOp::IfEnergy:
            if (option > 3 || event.x < 0 || event.x > 100 || !indexIn(event.y, option == 1 ? 2 : 1)) return false;
            break;
        case FighterOp::IfShunko:
            if (option || event.x || event.y) return false;
            break;
        case FighterOp::RememberFacing:
            if (option || event.target != FinishClip || event.x || event.y) return false;
            break;
        case FighterOp::StrikeReaction:
            if (option > 6 || event.target >= strikes_.size() || event.y ||
                ((option == 1 || option == 3) && !indexIn(event.x, 2)) ||
                (option == 2 && event.x < 0) || ((option == 4 || option == 6) && !indexIn(event.x, 65536)) ||
                (option == 5 && !indexIn(event.x, 12))) return false;
            break;
        case FighterOp::AirAttackLock:
        case FighterOp::GroundCancel:
            if (option || event.target != FinishClip || event.x || event.y) return false;
            break;
        case FighterOp::CounterPosition:
            if (option > 1 || event.target != FinishClip || !indexIn(event.x, 2) || event.y) return false;
            break;
        case FighterOp::WalkCancel:
            if (!option || option > 3 || event.target != FinishClip || !indexIn(event.x, 2) || event.y) return false;
            break;
        case FighterOp::ComboLimit:
            if (option >= 3 || !has(MoveClip(event.target)) || event.x || event.y) return false;
            break;
        case FighterOp::Hittable:
            if (option > 1 || event.target != FinishClip || !indexIn(event.x, 2) || event.y) return false;
            break;
        case FighterOp::MapBackdrop:
            if (option || event.target != FinishClip || !indexIn(event.x, 2) || !indexIn(event.y, 0x1000000)) return false;
            break;
        case FighterOp::AttackerStopTracking:
            if (option || event.target != FinishClip || event.x || event.y) return false;
            break;
        case FighterOp::SwapForm:
            if (option || (event.target != FinishClip && event.target > 7) || event.x || event.y) return false;
            break;
        case FighterOp::RememberAttacker:
            if (option >= 8 || event.target >= attackers_.size() || event.x || event.y) return false;
            break;
        case FighterOp::ControlRemembered:
            if (option >= 8 || event.target >= attackers_.size() || event.y || !has(attackers_[event.target].clip) ||
                !indexIn(event.x, clip(attackers_[event.target].clip).count)) return false;
            break;
        case FighterOp::AttackerWall:
            if (event.target == FinishClip ? (option || event.x || event.y) :
                (!event.target || event.x >= 0 || event.y <= 0)) return false;
            break;
        case FighterOp::AttackerAlignHitY:
            if (option > 1 || event.target >= strikes_.size() || !indexIn(event.x, strikes_.size()) || event.y) return false;
            break;
        case FighterOp::AttackerAnchor:
            if (option || event.target != FinishClip) return false;
            break;
        case FighterOp::AttackerFollow:
            if (option || event.target != FinishClip || !indexIn(event.x, 2) || event.y) return false;
            break;
        case FighterOp::AttackerStopTargetAction:
            if (option || event.target != FinishClip || !indexIn(event.x, 2) || event.y) return false;
            break;
        case FighterOp::AttackerShowTarget:
        case FighterOp::AttackerAllowTargetGuard:
            if (option || event.target != FinishClip || event.x || event.y) return false;
            break;
        case FighterOp::AttackerBehind:
            if (option > 1 || event.target != FinishClip || event.x || event.y) return false;
            break;
        case FighterOp::IfHit:
            if (option > 1 || !indexIn(event.x, strikes_.size())) return false;
            break;
        case FighterOp::IfRange:
            if (option > 1 || event.x > event.y) return false;
            break;
        case FighterOp::IfHurt:
            if (option > 15) return false;
            break;
        case FighterOp::StageCue:
            if (option > 3 || event.target != FinishClip || !indexIn(event.x, option == 3 ? 2 : 1) || event.y) return false;
            break;
        case FighterOp::Shadow: {
            const auto blue = std::int16_t(event.target);
            if (option || event.x < -255 || event.x > 255 || event.y < -255 || event.y > 255 ||
                event.x != int(event.x) || event.y != int(event.y) || blue < -255 || blue > 255) return false;
            break;
        }
        case FighterOp::EndShadow:
        case FighterOp::WalkSound:
            if (option || event.target != FinishClip || event.x || event.y) return false;
            break;
        case FighterOp::IfAttacker:
            if (option || !indexIn(event.x, 65536) || !indexIn(event.y, 65536) || (!event.x && !event.y)) return false;
            break;
        case FighterOp::Slow:
            if (!indexIn(event.x, FinishClip)) return false;
            break;
        case FighterOp::JumpCount:
            if (!indexIn(event.x, 16)) return false;
            break;
        case FighterOp::IfHealth:
        case FighterOp::LoseHealth:
        case FighterOp::LoseEnergy:
            if (option || event.x < 0) return false;
            break;
        case FighterOp::GainEnergy:
            if (option || event.target != FinishClip || event.x < 0 || event.x > 100 || event.y) return false;
            break;
        default: break;
        }
    }
    for (const ProjectileAsset& projectile : projectiles_) {
        if (!has(projectile.clip) || projectile.strikeIndex >= strikes_.size() ||
            !std::isfinite(projectile.spawnX) || !std::isfinite(projectile.spawnY) || !std::isfinite(projectile.ownerX) ||
            !valid(projectile.bounds) || projectile.bounds.empty() ||
            !valid(projectile.horizontal) || !valid(projectile.vertical) || !projectile.health ||
            projectile.life < -1 || projectile.hits < -1 || projectile.hits == 0) return false;
        const auto count = clip(projectile.clip).count;
        if (projectile.initial >= count || projectile.loopTo >= count) return false;
        for (auto phase : {projectile.loopAt, projectile.impact, projectile.timeout, projectile.removeAt, projectile.disableHitAt}) {
            if (phase != FinishClip && phase >= count) return false;
        }
    }
    for (const AttackerAsset& attacker : attackers_) {
        if (!has(attacker.clip) || attacker.initial >= clip(attacker.clip).count || attacker.gravity > 1 || attacker.targetAxes > 15 ||
            !std::isfinite(attacker.spawnX) || !std::isfinite(attacker.spawnY) || !std::isfinite(attacker.ownerX)) return false;
        const auto count = clip(attacker.clip).count;
        for (unsigned phase = 0; phase < count; ++phase) {
            for (const auto& event : events(frame(attacker.clip, phase))) {
                if (((event.op == FighterOp::AttackerLanding && event.target != FinishClip) || event.op == FighterOp::AttackerProbe ||
                     event.op == FighterOp::AttackerGoto) &&
                    event.target >= count) return false;
            }
        }
    }
    return true;
}

} // namespace bvn
