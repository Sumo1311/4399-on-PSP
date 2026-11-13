#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace bvn {

class TextureStream {
public:
    static constexpr std::size_t DefaultWindowBytes = 768 * 1024;
    TextureStream(const char* path, std::size_t offset, std::size_t payload, std::size_t maxTexture,
                  std::size_t windowBytes);
    ~TextureStream();
    bool ready() const { return fd_ >= 0; }
    const std::byte* get(std::size_t offset, std::size_t size);

private:
    struct alignas(64) Block { std::byte bytes[64]; };
    struct Window {
        std::unique_ptr<Block[]> data;
        std::size_t offset = 0, size = 0;
        unsigned age = 0;
    };

    void complete();
    void queue(unsigned index, std::size_t offset);
    std::size_t windowSize(std::size_t offset) const;

    std::array<Window, 2> windows_;
    std::size_t start_, payload_, windowBytes_, capacity_, frontier_ = 0;
    unsigned age_ = 0;
    int fd_ = -1, pending_ = -1;
};

} // namespace bvn
