#include "texture_stream.hpp"

#include <algorithm>
#include <new>

#include <pspiofilemgr.h>
#include <pspkernel.h>

namespace bvn {

TextureStream::TextureStream(const char* path, std::size_t offset, std::size_t payload, std::size_t maxTexture,
                             std::size_t windowBytes)
    : start_(offset), payload_(payload), windowBytes_(windowBytes), capacity_(windowBytes + maxTexture) {
    for (auto& window : windows_) {
        window.data.reset(new (std::nothrow) Block[(capacity_ + sizeof(Block) - 1) / sizeof(Block)]);
        if (!window.data) return;
    }
    fd_ = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd_ < 0) return;
    auto& first = windows_[0];
    const auto size = windowSize(0);
    if (sceIoLseek(fd_, start_, PSP_SEEK_SET) == SceOff(start_) &&
        sceIoRead(fd_, first.data.get(), size) == int(size)) {
        first.size = size;
        first.age = ++age_;
        queue(1, windowBytes_);
    }
}

TextureStream::~TextureStream() {
    if (fd_ < 0) return;
    if (pending_ >= 0) {
        SceInt64 result;
        sceIoWaitAsync(fd_, &result);
    }
    sceIoClose(fd_);
}

std::size_t TextureStream::windowSize(std::size_t offset) const {
    return offset < payload_ ? std::min(capacity_, payload_ - offset) : 0;
}

void TextureStream::complete() {
    if (pending_ < 0) return;
    SceInt64 result = 0;
    const int status = sceIoPollAsync(fd_, &result);
    if (status > 0) return;
    auto& window = windows_[pending_];
    if (status < 0 || result != SceInt64(window.size)) window.size = 0;
    pending_ = -1;
}

void TextureStream::queue(unsigned index, std::size_t offset) {
    const auto size = windowSize(offset);
    if (pending_ >= 0 || !size) return;
    if (sceIoLseek(fd_, start_ + offset, PSP_SEEK_SET) != SceOff(start_ + offset)) return;
    auto& window = windows_[index];
    window.size = 0;
    if (sceIoReadAsync(fd_, window.data.get(), size) < 0) return;
    window.offset = offset;
    window.size = size;
    window.age = ++age_;
    pending_ = int(index);
}

const std::byte* TextureStream::get(std::size_t offset, std::size_t size) {
    complete();
    // Prefer the aligned window even if the tail of the previous window also covers this texture.
    const auto block = offset - offset % windowBytes_;
    int hit = -1;
    for (unsigned i = 0; i < windows_.size(); ++i) {
        const auto& window = windows_[i];
        if (int(i) == pending_ || !window.size || offset < window.offset) continue;
        const auto local = offset - window.offset;
        if (local > window.size || size > window.size - local) continue;
        hit = int(i);
        if (window.offset == block) break;
    }
    if (hit >= 0) {
        auto& window = windows_[hit];
        auto& other = windows_[1 - hit];
        window.age = ++age_;
        if (block >= frontier_) {
            frontier_ = block;
            const auto next = window.offset + windowBytes_;
            if (!other.size || other.offset != next) queue(1 - hit, next);
        }
        return reinterpret_cast<const std::byte*>(window.data.get()) + (offset - window.offset);
    }
    const unsigned older = windows_[0].age <= windows_[1].age ? 0 : 1;
    if (pending_ < 0) {
        frontier_ = block;
        queue(older, block);
    }
    return nullptr;
}

} // namespace bvn
