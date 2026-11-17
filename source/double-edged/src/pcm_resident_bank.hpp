#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// Immutable, complete short clips. Streamed voices keep the original read path.
// No conversion, resampling, voice count or completion behavior is changed.
class ResidentPcmBank {
    static constexpr std::uint32_t Missing = 0xffffffffu;
    std::vector<std::uint32_t> offsets_;
    std::vector<std::uint32_t> frames_;
    std::vector<std::uint8_t> pcm_;
public:
    template<class Records>
    bool warm(std::FILE* file, const Records& records, unsigned maximum_clip_bytes,
              unsigned budget_bytes) {
        offsets_.assign(records.size(), Missing);
        frames_.resize(records.size());
        unsigned used = 0;
        for (unsigned i = 0; i < records.size(); ++i) {
            frames_[i] = records[i].frames;
            std::uint64_t bytes = std::uint64_t(records[i].frames) * 4;
            if (bytes && bytes <= maximum_clip_bytes && used + bytes <= budget_bytes) {
                offsets_[i] = used;
                used += unsigned(bytes);
            }
        }
        pcm_.resize(used);
        for (unsigned i = 0; i < records.size(); ++i) {
            if (!contains(i)) continue;
            if (std::fseek(file, records[i].offset, SEEK_SET) ||
                std::fread(pcm_.data() + offsets_[i], 4, records[i].frames, file) != records[i].frames)
                return false;
        }
        return true;
    }
    bool contains(unsigned handle) const {
        return handle < offsets_.size() && offsets_[handle] != Missing;
    }
    bool read(unsigned handle, unsigned first, unsigned count, void* output) const {
        if (!contains(handle) || first > frames_[handle] || count > frames_[handle] - first)
            return false;
        std::memcpy(output, pcm_.data() + offsets_[handle] + std::size_t(first) * 4,
                    std::size_t(count) * 4);
        return true;
    }
    std::size_t bytes() const { return pcm_.size(); }
};
