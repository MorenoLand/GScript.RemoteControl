#include "TMng.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace {
    constexpr std::array<std::uint8_t, 8> mngSignature{0x8a, 0x4d, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    constexpr std::array<std::uint8_t, 8> pngSignature{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};

    std::uint32_t readBigEndian(const std::uint8_t* data) {
        return static_cast<std::uint32_t>(data[0]) << 24 | static_cast<std::uint32_t>(data[1]) << 16 | static_cast<std::uint32_t>(data[2]) << 8 | data[3];
    }

    std::optional<std::uint32_t> frameDelay(const std::uint8_t* data, std::size_t length) {
        if (length == 0) return std::nullopt;
        std::size_t offset = 1;
        while (offset < length && data[offset] != 0) offset++;
        if (offset == length || ++offset >= length) return std::nullopt;
        const bool hasDelay = data[offset++] != 0;
        if (length - offset < 3) return std::nullopt;
        offset += 3;
        if (!hasDelay || length - offset < 4) return std::nullopt;
        return readBigEndian(data + offset);
    }
}

bool TMng::isMNG(const void* content, std::size_t length) {
    if (content == nullptr || length < mngSignature.size()) return false;
    return std::equal(mngSignature.begin(), mngSignature.end(), static_cast<const std::uint8_t*>(content));
}

std::optional<TMng::Animation> TMng::parse(const void* content, std::size_t length) {
    if (!isMNG(content, length)) return std::nullopt;
    const auto* bytes = static_cast<const std::uint8_t*>(content);
    Animation animation;
    std::vector<std::uint8_t> frame;
    std::optional<std::uint32_t> pendingDelay;
    bool inPng = false;
    std::size_t offset = mngSignature.size();
    while (offset < length) {
        if (length - offset < 12) return std::nullopt;
        const std::uint32_t dataLength = readBigEndian(bytes + offset);
        const std::size_t chunkLength = static_cast<std::size_t>(dataLength) + 12;
        if (chunkLength < 12 || chunkLength > length - offset) return std::nullopt;
        const char* type = reinterpret_cast<const char*>(bytes + offset + 4);
        const std::uint8_t* data = bytes + offset + 8;
        if (std::memcmp(type, "MHDR", 4) == 0) {
            if (dataLength < 28) return std::nullopt;
            animation.width = readBigEndian(data);
            animation.height = readBigEndian(data + 4);
            const std::uint32_t ticks = readBigEndian(data + 8);
            if (ticks > 0) animation.ticksPerSecond = ticks;
            animation.nominalLayerCount = readBigEndian(data + 12);
            animation.nominalFrameCount = readBigEndian(data + 16);
            animation.nominalPlayTime = readBigEndian(data + 20);
            animation.simplicityProfile = readBigEndian(data + 24);
        } else if (std::memcmp(type, "FRAM", 4) == 0) pendingDelay = frameDelay(data, dataLength);
        if (!inPng && std::memcmp(type, "IHDR", 4) == 0) {
            frame.assign(pngSignature.begin(), pngSignature.end());
            inPng = true;
        }
        if (inPng) {
            frame.insert(frame.end(), bytes + offset, bytes + offset + chunkLength);
            if (std::memcmp(type, "IEND", 4) == 0) {
                const double milliseconds = pendingDelay.has_value() ? std::round(static_cast<double>(*pendingDelay) * 1000.0 / animation.ticksPerSecond) : 100.0;
                animation.frames.push_back({std::move(frame), static_cast<std::uint32_t>(std::max(0.0, milliseconds))});
                frame.clear();
                pendingDelay.reset();
                inPng = false;
            }
        }
        offset += chunkLength;
    }
    if (inPng || animation.frames.empty()) return std::nullopt;
    return animation;
}

std::vector<std::uint8_t> TMng::firstPngFrame(const void* content, std::size_t length) {
    std::optional<Animation> animation = parse(content, length);
    if (!animation.has_value()) return {};
    return std::move(animation->frames.front().png);
}
