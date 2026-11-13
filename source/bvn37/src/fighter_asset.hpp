#pragma once
#include "map_optical.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#ifdef __PSP__
#include "texture_stream.hpp"
#endif

namespace bvn {

enum class MoveClip : std::uint16_t {
    Idle, Walk, JumpStart, JumpRise, JumpApex, Fall, Land, GroundAttack, Hurt,
    HurtFly, HurtFall, HurtBounce, HurtDown, HurtRecover, Count, Missing = 0xffff
};

enum class FighterCommand : std::uint8_t {
    Attack, SkillDown, SkillUp, AirAttack, Guard, GuardRecover, Dash, Special, SpecialDown, SpecialUp,
    Intro, Victory, Defeat, AirSpecial, Super, SuperUp, SuperDown, Jump, Transform, TransformUp, TransformDown,
    SuperAir, CatchAttack, CatchSpecial, Count
};
enum class FighterOp : std::uint8_t {
    Velocity, Damping, Allow, End, Gravity, Steel, Qi, AirMove, Shake, Shine, DashStart, DashStop, Fire, Summon,
    Cutin, EndCutin, Landing, Goto, BranchHit, QiOnHit, Probe, MoveOnce, MoveToTarget, SelfLaunch, Remove,
    SwitchForm, IfHit, IfRange, IfHurt, Skip, Cross, FaceTarget, Slow, ShakeLoop, HurtAction, Recover,
    IfAttacker, StageCue, JumpCount, AttackerLanding, AttackerPause, IfHealth, LoseHealth, LoseEnergy,
    TargetAnchor, MoveTarget, IfTarget, TargetVisible, AttackerProbe, AttackerGoto, AttackerRange,
    AttackerCutin, OwnerBuff, OwnerHeal, TargetHealthPower, AttackerCapture, AttackerMiss, AttackerRelease,
    HealPercent, ControlAttacker, AttackerQiCap, IfFlight, FlightMode, FlightPose, TargetVelocity,
    RememberAttacker, ControlRemembered, AttackerWall, AttackerAlignHitY, AttackerAnchor, AttackerFollow,
    AttackerShowTarget, AttackerBehind, TargetDamping, TimelineValue, IfTimelineValue, TimelineQi,
    IfEnergy, StrikeReaction, AirAttackLock, GroundCancel, CounterPosition, WalkCancel, ScreenPose, BankaiPose,
    ComboLimit, Hittable, AttackerStopTracking, SwapForm, MapBackdrop, AttackerPushOut, UlquiorraDashSound, GainEnergy,
    FighterBuff, IfDisplayOverlap, TargetOffset, AttackerStopTargetAction, AttackerAllowTargetGuard, TimelineMove, Kamui,
    IfWaterInput, WaterWalk, HinataState, FollowAttackerY, AttackerRecoverOwner, AttackerWaterEnergy,
    WaterPose, TwinLionPose, IfShunko, FacingPose, RememberFacing, TransferQi,
    AssistWind, AssistSwap, AttackerHurtBind, AttackerHoming, AttackerRemoveAtSide,
    AttackerRandomGoto, AttackerRandomMove, Shadow, EndShadow, WalkSound, Count
};
constexpr bool isFighterBranch(FighterOp op) {
    return (op >= FighterOp::IfHit && op <= FighterOp::Skip) ||
        op == FighterOp::IfAttacker || op == FighterOp::IfHealth || op == FighterOp::IfTarget ||
        op == FighterOp::IfFlight || op == FighterOp::IfTimelineValue || op == FighterOp::IfEnergy ||
        op == FighterOp::IfDisplayOverlap || op == FighterOp::IfWaterInput || op == FighterOp::IfShunko;
}
enum class CutinKind : std::uint8_t { None, Super, Ultimate, Transformation, GhostStep };

inline constexpr std::uint16_t FinishClip = 0xffff;
inline constexpr unsigned FirstTimelineCounter = 4;
inline constexpr unsigned TimelineCounterCount = 11;

struct FighterStats {
    float speed, jumpPower, weight;
};

struct AiHitTraits { bool breaksGuard = false, knocksDown = false; };

struct FighterClip {
    std::uint16_t first, count, repeat, timelineStart;
};

struct FighterPose {
    float x, y;
    std::uint32_t offset;
    std::uint16_t width, height, stride, rows;

    // A zero stride replaces the texture index and width with a span in the tile table.
    bool tiled() const { return stride == 0; }
};

struct FighterBox {
    float left, top, right, bottom;

    bool empty() const { return left >= right || top >= bottom; }
};

struct FighterFrame {
    FighterPose pose;
    FighterBox body;
    std::uint32_t hitStart, eventStart;
    std::uint16_t hitCount, eventCount;
};

struct FighterStrike {
    float hitX, hitY;
    std::uint16_t damage, hurtFrames;
    std::uint16_t hitType, flags;
    std::uint16_t hurtTimeMs, slowTimeMs;
    std::uint32_t key;

    bool launches() const { return flags & 1; }
    bool targetGravity() const {return !(flags&64);}
    bool breaksGuard() const { return flags & 2; }
    bool checksFacing() const { return flags & 8; }
    bool bisha() const { return flags & 16; }
    bool skill() const { return flags & 32; }
};

struct FighterHit {
    FighterBox box;
    std::uint16_t strikeIndex, kind;

    bool strike() const { return kind == 0; }
    bool probe() const { return kind == 1; }
    bool display() const { return kind == 2; }
};

struct FighterEvent {
    FighterOp op;
    FighterCommand command;
    std::uint16_t target;
    float x, y;
};

struct ProjectileAxis {
    float speed, acceleration, minimum, maximum, onHit;
};

struct ProjectileAsset {
    float spawnX, spawnY;
    FighterBox bounds;
    ProjectileAxis horizontal, vertical;
    MoveClip clip;
    std::uint16_t strikeIndex, initial, loopAt, loopTo, impact, timeout, removeAt, disableHitAt, health;
    std::int16_t life, hits;
    float ownerX;
};

struct AttackerAsset {
    float spawnX, spawnY, ownerX;
    MoveClip clip;
    std::uint16_t initial, gravity, targetAxes;
    std::uint32_t key;
};

class FighterAsset {
public:
    enum class TextureResidency { Standard, LowMemory };

    bool open(const char* path, TextureResidency residency = TextureResidency::Standard);
    bool assister() const { return assister_; }
    const FighterStats& stats() const { return stats_; }
    unsigned defenseType() const { return defenseType_; }
    MoveClip command(FighterCommand id) const { return defaults_[static_cast<std::size_t>(id)]; }
    const FighterBox& aiRange(FighterCommand id) const { return aiRanges_[static_cast<std::size_t>(id)]; }
    MoveClip hurtEscape() const { return hurtEscape_; }
    bool has(MoveClip id) const {
        const auto index = static_cast<std::size_t>(id);
        return index < clips_.size() && clips_[index].count;
    }
    const FighterClip& clip(MoveClip id) const { return clips_[static_cast<std::size_t>(id)]; }
    std::size_t clipCount() const { return clips_.size(); }
    std::optional<unsigned> nextForm(MoveClip id, unsigned current) const;
    const FighterFrame& frame(MoveClip id, unsigned phase) const { return frames_[clip(id).first + phase]; }
    std::span<const FighterHit> hits(const FighterFrame& frame) const {
        return std::span(hits_).subspan(frame.hitStart, frame.hitCount);
    }
    std::span<const FighterEvent> events(const FighterFrame& frame) const {
        return std::span(events_).subspan(frame.eventStart, frame.eventCount);
    }
    const FighterStrike& strike(unsigned index) const { return strikes_[index]; }
    std::span<const FighterStrike> strikes() const { return strikes_; }
    static std::uint32_t aiHitCommands(std::uint32_t hitKey);
    AiHitTraits unusedAiHits(FighterCommand command) const {
        const auto bit = 1U << unsigned(command);
        return {bool(aiUnusedBreaks_ & bit), bool(aiUnusedKnockdowns_ & bit)};
    }
    const ProjectileAsset& projectile(unsigned index) const { return projectiles_[index]; }
    const AttackerAsset& attacker(unsigned index) const { return attackers_[index]; }
    unsigned attackerCount()const{return attackers_.size();}
    std::uint64_t metadataIdentity()const{return metadataIdentity_;}
    bool hasObjectAreas()const;
    std::optional<OpticalRect> objectArea(unsigned index)const;
    std::span<const FighterPose> poseTiles(const FighterPose& pose) const {
        return pose.tiled() ? std::span(tiles_).subspan(pose.offset, pose.width) : std::span(&pose, 1);
    }
    static std::size_t textureSize(const FighterPose& pose) { return 4u * pose.stride * pose.rows; }
    const void* pixels(const FighterPose& pose) const {
        return textures_.empty() ? pixels_.data() + pose.offset / 16 : nullptr;
    }
    bool decodePixels(const FighterPose& pose, std::span<std::byte> output) const;

private:
    struct alignas(16) PixelBlock { std::array<std::byte, 16> bytes; };
    struct PackedTexture { std::uint32_t offset, packedSize, rawSize; };
    bool readLegacy(std::FILE* stream, std::size_t length);
    bool readCurrent(std::FILE* stream, std::size_t length, unsigned version, TextureResidency residency);
    bool readPixels(std::FILE* stream, std::size_t length, std::size_t start,
                    TextureResidency residency = TextureResidency::Standard);
    bool validate(std::size_t payload) const;
    void initializeAiHits();
    std::uint64_t metadataIdentity_=0;

    bool assister_ = false;
    FighterStats stats_{};
    unsigned defenseType_ = 0;
    std::uint32_t aiUnusedBreaks_ = 0, aiUnusedKnockdowns_ = 0;
    std::array<MoveClip, static_cast<std::size_t>(FighterCommand::Count)> defaults_{};
    std::array<FighterBox, static_cast<std::size_t>(FighterCommand::Count)> aiRanges_{};
    MoveClip hurtEscape_ = MoveClip::Missing;
    std::vector<FighterClip> clips_;
    std::vector<FighterFrame> frames_;
    std::vector<FighterHit> hits_;
    std::vector<FighterStrike> strikes_;
    std::vector<FighterEvent> events_;
    std::vector<ProjectileAsset> projectiles_;
    std::vector<AttackerAsset> attackers_;
    std::vector<PixelBlock> pixels_;
    std::vector<PackedTexture> textures_;
    std::vector<FighterPose> tiles_;
    std::vector<std::byte> compressed_;
    bool lz4Textures_ = false;
    std::unique_ptr<std::FILE, decltype(&std::fclose)> streamed_{nullptr, std::fclose};
    std::size_t streamedStart_ = 0;
    mutable std::vector<std::byte> streamedBuffer_;
#ifdef __PSP__
    std::unique_ptr<TextureStream> readAhead_;
#endif
};

} // namespace bvn
