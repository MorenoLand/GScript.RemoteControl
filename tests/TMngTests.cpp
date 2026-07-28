#include "TMng.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

namespace {
    void addUint32(std::vector<std::uint8_t>& data, std::uint32_t value) {
        data.push_back(static_cast<std::uint8_t>(value >> 24));
        data.push_back(static_cast<std::uint8_t>(value >> 16));
        data.push_back(static_cast<std::uint8_t>(value >> 8));
        data.push_back(static_cast<std::uint8_t>(value));
    }
    void addChunk(std::vector<std::uint8_t>& data, const char* type, const std::vector<std::uint8_t>& content) {
        const std::uint32_t length = static_cast<std::uint32_t>(content.size());
        data.push_back(static_cast<std::uint8_t>(length >> 24));
        data.push_back(static_cast<std::uint8_t>(length >> 16));
        data.push_back(static_cast<std::uint8_t>(length >> 8));
        data.push_back(static_cast<std::uint8_t>(length));
        data.insert(data.end(), type, type + 4);
        data.insert(data.end(), content.begin(), content.end());
        data.insert(data.end(), 4, 0);
    }
}

int main() {
    std::vector<std::uint8_t> mng{0x8a, 0x4d, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    std::vector<std::uint8_t> header;
    for (std::uint32_t value : {32, 24, 50, 1, 2, 15, 7}) addUint32(header, value);
    addChunk(mng, "MHDR", header);
    std::vector<std::uint8_t> delay{0, 0, 1, 0, 0, 0};
    addUint32(delay, 5);
    addChunk(mng, "FRAM", delay);
    addChunk(mng, "IHDR", std::vector<std::uint8_t>(13, 0));
    addChunk(mng, "IDAT", {1, 2, 3});
    addChunk(mng, "IEND", {});
    delay.resize(6);
    addUint32(delay, 10);
    addChunk(mng, "FRAM", delay);
    addChunk(mng, "IHDR", std::vector<std::uint8_t>(13, 1));
    addChunk(mng, "IDAT", {4, 5});
    addChunk(mng, "IEND", {});
    assert(TMng::isMNG(mng.data(), mng.size()));
    const std::optional<TMng::Animation> animation = TMng::parse(mng.data(), mng.size());
    assert(animation.has_value());
    assert(animation->width == 32);
    assert(animation->height == 24);
    assert(animation->ticksPerSecond == 50);
    assert(animation->nominalLayerCount == 1);
    assert(animation->nominalFrameCount == 2);
    assert(animation->nominalPlayTime == 15);
    assert(animation->simplicityProfile == 7);
    assert(animation->frames.size() == 2);
    assert(animation->frames[0].delayMilliseconds == 100);
    assert(animation->frames[1].delayMilliseconds == 200);
    const std::vector<std::uint8_t> png = TMng::firstPngFrame(mng.data(), mng.size());
    const std::vector<std::uint8_t> signature{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    assert(png.size() == 60);
    assert(std::equal(signature.begin(), signature.end(), png.begin()));
    assert(TMng::firstPngFrame("not mng", 7).empty());
    mng.resize(mng.size() - 1);
    assert(TMng::firstPngFrame(mng.data(), mng.size()).empty());
    return 0;
}
