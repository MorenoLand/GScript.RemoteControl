#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace TMng {
    struct Frame {
        std::vector<std::uint8_t> png;
        std::uint32_t delayMilliseconds = 100;
    };
    struct Animation {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t ticksPerSecond = 100;
        std::uint32_t nominalLayerCount = 0;
        std::uint32_t nominalFrameCount = 0;
        std::uint32_t nominalPlayTime = 0;
        std::uint32_t simplicityProfile = 0;
        std::vector<Frame> frames;
    };
    bool isMNG(const void* content, std::size_t length);
    std::optional<Animation> parse(const void* content, std::size_t length);
    std::vector<std::uint8_t> firstPngFrame(const void* content, std::size_t length);
}
