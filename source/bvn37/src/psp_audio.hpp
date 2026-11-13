#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <psptypes.h>
#include "source_effect_sounds.hpp"

namespace bvn {

enum class Sound : unsigned {
    Round, One, Two, Three, Four, FinalRound, Fight, TimeOver, Draw, KO, OverHit, Select, Confirm,
    AssistBleach, AssistNaruto, UlquiorraDash, Rush, BossWarning, GhostStep, CounterReplace, CounterCharge, CounterBurst,
    Step1, Step2, Step3, GhostFoot, KOBisha, SourceFirst, Count=SourceFirst+SourceEffectSoundNames.size()
};

class PspAudio {
public:
    ~PspAudio();
    bool open(const char* path);
    void music(const char* name);
    void volumes(unsigned soundPercent,unsigned musicPercent);
    void play(Sound sound);
    void sourceEffect(std::string_view name);
    void footsteps(unsigned count, bool musou = false);
    void ghostFootsteps(unsigned count, bool musou = false);
    unsigned requests(Sound sound) const { return observedSounds_[unsigned(sound)]; }
    void dashSound(unsigned side, bool start);

private:
    static constexpr unsigned BlockFrames = 512;
    static constexpr unsigned SampleRate = 22050;
    struct Clip { char name[32]; std::uint32_t offset, samples; };
    struct Voice { int effect = -1; unsigned sample = 0; };
    static int entry(SceSize, void*);
    int run();
    bool load(const char* path);
    const Clip* find(const char* name) const;
    void mixMusic(const Clip* track, std::array<int, BlockFrames * 2>& mixed,unsigned volume);

    std::FILE* stream_ = nullptr;
    std::array<char, 16 * 1024> fileBuffer_{};
    std::vector<Clip> clips_;
    std::array<std::vector<std::int16_t>, static_cast<unsigned>(Sound::Count)> effects_;
    std::array<Voice, 4> voices_, dashVoices_;
    alignas(64) std::array<std::array<std::int16_t, BlockFrames * 4>, 2> output_{};
    std::array<std::int16_t, BlockFrames * 2> musicBuffer_{};
    unsigned musicSample_ = 0;
    const Clip* requestedTrack_ = nullptr;
    std::array<unsigned, static_cast<unsigned>(Sound::Count)> requestedSounds_{};
    std::array<unsigned, static_cast<unsigned>(Sound::Count)> observedSounds_{};
    std::array<int, 2> requestedDashes_{};
    int lock_ = -1, thread_ = -1, channel_ = -1;
    bool playing_ = false;
    unsigned soundVolume_=70,musicVolume_=70;
    std::uint64_t lastSoundTime_ = 0;
    std::uint32_t cosmeticRandom_ = 0x6d2b79f5U;
};

} // namespace bvn
