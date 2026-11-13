#include "psp_audio.hpp"

#include <algorithm>
#include <cstring>
#include <span>
#include <utility>

#include <pspaudio.h>
#include <pspkernel.h>

namespace bvn {
namespace {

constexpr unsigned ClipLimit = 256;
constexpr const char* EffectKeys[]{"fx-round", "fx-one", "fx-two", "fx-three", "fx-four", "fx-finalround",
    "fx-fight", "fx-timeup", "fx-drawgame", "fx-ko", "fx-over-hit", "fx-select", "fx-confirm",
    "fx-assist-bleach", "fx-assist-naruto", "fx-ulquiorra-dash", "fx-rush", "fx-boss-warning", "fx-ghost-step",
    "fx-counter-replace", "fx-counter-charge", "fx-counter-burst", "fx-step1", "fx-step2", "fx-step3", "fx-ghost-foot", "fx-ko-bisha"};
static_assert(std::size(EffectKeys) == static_cast<unsigned>(Sound::SourceFirst));

} // namespace

PspAudio::~PspAudio() {
    if (thread_ >= 0) {
        sceKernelWaitSema(lock_, 1, nullptr);
        playing_ = false;
        sceKernelSignalSema(lock_, 1);
        sceKernelWaitThreadEnd(thread_, nullptr);
        sceKernelDeleteThread(thread_);
    }
    if (lock_ >= 0) sceKernelDeleteSema(lock_);
    if (channel_ >= 0) sceAudioChRelease(channel_);
    if (stream_) std::fclose(stream_);
}

const PspAudio::Clip* PspAudio::find(const char* name) const {
    const auto clip = std::lower_bound(clips_.begin(), clips_.end(), name, [](const Clip& item, const char* key) {
        return std::strcmp(item.name, key) < 0;
    });
    return clip != clips_.end() && std::strcmp(clip->name, name) == 0 ? &*clip : nullptr;
}

bool PspAudio::load(const char* path) {
    static_assert(sizeof(Clip) == 40);
    struct Header { char magic[8]; unsigned count, dataStart; } header{};
    stream_ = std::fopen(path, "rb");
    if (!stream_) return false;
    std::setvbuf(stream_, fileBuffer_.data(), _IOFBF, fileBuffer_.size());
    if (std::fseek(stream_, 0, SEEK_END)) return false;
    const long bytes = std::ftell(stream_);
    std::rewind(stream_);
    if (bytes < 0 || bytes > 128 * 1024 * 1024 || std::fread(&header, sizeof(header), 1, stream_) != 1 ||
        std::memcmp(header.magic, "BVNAUD01", 8) || !header.count || header.count > ClipLimit ||
        header.dataStart != sizeof(header) + header.count * sizeof(Clip) || header.dataStart >= unsigned(bytes)) return false;
    clips_.resize(header.count);
    if (std::fread(clips_.data(), sizeof(Clip), clips_.size(), stream_) != clips_.size()) return false;
    const char* previous = "";
    for (const Clip& clip : clips_) {
        if (!std::memchr(clip.name, 0, sizeof(clip.name)) || std::strcmp(previous, clip.name) >= 0 ||
            clip.offset < header.dataStart || clip.offset % 4 || clip.offset >= unsigned(bytes) ||
            !clip.samples || clip.samples % 2 || clip.samples > (unsigned(bytes) - clip.offset) / 2) return false;
        previous = clip.name;
    }
    for (unsigned i = 0; i < effects_.size(); ++i) {
        const auto* clip = find(i<std::size(EffectKeys)?EffectKeys[i]:SourceEffectSoundNames[i-std::size(EffectKeys)].data());
        if (!clip && i >= static_cast<unsigned>(Sound::UlquiorraDash) && i < static_cast<unsigned>(Sound::Step1)) continue;
        if (!clip || clip->samples > SampleRate * 2 * 8 || std::fseek(stream_, clip->offset, SEEK_SET)) return false;
        effects_[i].resize(clip->samples);
        if (std::fread(effects_[i].data(), sizeof(std::int16_t), clip->samples, stream_) != clip->samples) return false;
    }
    return true;
}

bool PspAudio::open(const char* path) {
    if (!load(path)) return false;
    channel_ = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, BlockFrames * 2, PSP_AUDIO_FORMAT_STEREO);
    if (channel_ < 0) return false;
    lock_ = sceKernelCreateSema("BVN audio command", 0, 1, 1, nullptr);
    if (lock_ < 0) return false;
    thread_ = sceKernelCreateThread("BVN audio mixer", entry, 0x21, 24 * 1024, PSP_THREAD_ATTR_USER, nullptr);
    if (thread_ < 0) return false;
    playing_ = true;
    PspAudio* self = this;
    if (sceKernelStartThread(thread_, sizeof(self), &self) < 0) {
        sceKernelDeleteThread(thread_);
        thread_ = -1;
        playing_ = false;
        return false;
    }
    return true;
}

void PspAudio::music(const char* name) {
    if (thread_ < 0) return;
    sceKernelWaitSema(lock_, 1, nullptr);
    requestedTrack_ = find(name);
    sceKernelSignalSema(lock_, 1);
}

void PspAudio::volumes(unsigned soundPercent,unsigned musicPercent) {
    if(lock_>=0)sceKernelWaitSema(lock_,1,nullptr);
    soundVolume_=std::min(soundPercent,100U);musicVolume_=std::min(musicPercent,100U);
    if(lock_>=0)sceKernelSignalSema(lock_,1);
}

void PspAudio::play(Sound sound) {
    if (thread_ < 0) return;
    lastSoundTime_ = sceKernelGetSystemTimeWide();
    ++observedSounds_[unsigned(sound)];
    sceKernelWaitSema(lock_, 1, nullptr);
    auto& requested = requestedSounds_[static_cast<unsigned>(sound)];
    requested = std::min(requested + 1, unsigned(voices_.size()));
    sceKernelSignalSema(lock_, 1);
}

void PspAudio::sourceEffect(std::string_view name) {
    const auto entry=std::ranges::find(SourceEffectSoundNames,name);
    if(entry!=SourceEffectSoundNames.end())play(Sound(unsigned(Sound::SourceFirst)+unsigned(entry-SourceEffectSoundNames.begin())));
}

void PspAudio::footsteps(unsigned count, bool musou) {
    while (count--) {
        if (musou && sceKernelGetSystemTimeWide() - lastSoundTime_ < 20000) continue;
        cosmeticRandom_ ^= cosmeticRandom_ << 13;
        cosmeticRandom_ ^= cosmeticRandom_ >> 17;
        cosmeticRandom_ ^= cosmeticRandom_ << 5;
        play(Sound(unsigned(Sound::Step1) + cosmeticRandom_ % 3));
    }
}

void PspAudio::ghostFootsteps(unsigned count, bool musou) {
    while (count--) if (!musou || sceKernelGetSystemTimeWide() - lastSoundTime_ >= 20000) play(Sound::GhostFoot);
}

void PspAudio::dashSound(unsigned side, bool start) {
    if (thread_ < 0 || effects_[static_cast<unsigned>(Sound::UlquiorraDash)].empty()) return;
    sceKernelWaitSema(lock_, 1, nullptr);
    requestedDashes_[side] = start ? 1 : -1;
    sceKernelSignalSema(lock_, 1);
}

int PspAudio::entry(SceSize, void* argument) {
    return (*static_cast<PspAudio**>(argument))->run();
}

void PspAudio::mixMusic(const Clip* track, std::array<int, BlockFrames * 2>& mixed,unsigned volume) {
    if (!track) return;
    unsigned output = 0;
    while (output < mixed.size()) {
        if (!musicSample_ && std::fseek(stream_, track->offset, SEEK_SET)) return;
        const unsigned take = std::min(unsigned(mixed.size()) - output, unsigned(track->samples) - musicSample_);
        if (std::fread(musicBuffer_.data(), sizeof(std::int16_t), take, stream_) != take) {
            musicSample_ = 0;
            return;
        }
        for (unsigned i = 0; i < take; ++i) mixed[output + i] = musicBuffer_[i] * int(volume) / 100;
        output += take;
        musicSample_ = (musicSample_ + take) % track->samples;
    }
}

int PspAudio::run() {
    unsigned buffer = 0;
    const Clip* track = nullptr;
    int previous[2]{};
    std::array<int, 2> latestDash{-1, -1};
    for (;;) {
        sceKernelWaitSema(lock_, 1, nullptr);
        const bool running = playing_;
        const auto* requested = requestedTrack_;
        const auto cues = std::exchange(requestedSounds_, {});
        const auto dashes = std::exchange(requestedDashes_, {});
        const auto soundVolume=soundVolume_,musicVolume=musicVolume_;
        sceKernelSignalSema(lock_, 1);
        if (!running) return 0;
        if (requested != track) {
            track = requested;
            musicSample_ = 0;
        }
        for (unsigned i = 0; i < effects_.size(); ++i) {
            if (effects_[i].empty()) continue;
            for (unsigned count = cues[i]; count; --count) {
                const auto free = std::find_if(voices_.begin(), voices_.end(),
                                               [](const Voice& voice) { return voice.effect < 0; });
                if (free == voices_.end()) break;
                *free = {int(i), 0};
            }
        }
        for (unsigned side = 0; side < dashes.size(); ++side) {
            if (dashes[side] < 0 && latestDash[side] >= 0) {
                dashVoices_[std::exchange(latestDash[side], -1)] = {};
            } else if (dashes[side] > 0) {
                const auto free = std::find_if(dashVoices_.begin(), dashVoices_.end(),
                                               [](const Voice& voice) { return voice.effect < 0; });
                if (free == dashVoices_.end()) continue;
                const int slot = int(free - dashVoices_.begin());
                for (int& active : latestDash) {
                    if (active == slot) active = -1;
                }
                latestDash[side] = slot;
                *free = {static_cast<int>(Sound::UlquiorraDash), 0};
            }
        }
        std::array<int, BlockFrames * 2> mixed{};
        mixMusic(track, mixed,musicVolume);
        for (auto channels : {std::span(voices_), std::span(dashVoices_)}) {
            for (auto& voice : channels) {
                if (voice.effect < 0) continue;
                const auto& pcm = effects_[voice.effect];
                const unsigned count = std::min(unsigned(mixed.size()), unsigned(pcm.size()) - voice.sample);
                for (unsigned i = 0; i < count; ++i) mixed[i] += pcm[voice.sample + i] * int(soundVolume) / 100;
                voice.sample += count;
                if (voice.sample == pcm.size()) voice.effect = -1;
            }
        }
        auto& out = output_[buffer];
        for (unsigned frame = 0; frame < BlockFrames; ++frame) {
            for (unsigned channel = 0; channel < 2; ++channel) {
                const int sample = std::clamp(mixed[frame * 2 + channel], -32768, 32767);
                out[frame * 4 + channel] = (previous[channel] + sample) / 2;
                out[frame * 4 + channel + 2] = sample;
                previous[channel] = sample;
            }
        }
        sceKernelDcacheWritebackRange(out.data(), sizeof(out));
        if (sceAudioOutputBlocking(channel_, PSP_AUDIO_VOLUME_MAX, out.data()) < 0) return 1;
        buffer ^= 1;
    }
}

} // namespace bvn
