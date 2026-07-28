#pragma once

#include <cstdarg>
#include <cstdio>

extern bool remoteControlDebug;
extern bool remoteControlPacketLog;

inline void remoteControlDebugLog(const char* format, ...) {
    if (!remoteControlDebug) return;
    va_list arguments;
    va_start(arguments, format);
    std::fputs("[RemoteControl] ", stderr);
    std::vfprintf(stderr, format, arguments);
    std::fputc('\n', stderr);
    std::fflush(stderr);
    va_end(arguments);
}

inline void remoteControlPacketLogMessage(int packetId, const void* data, int length) {
    if (!remoteControlPacketLog) return;
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    std::fprintf(stderr, "[RemoteControl packet] id=%d length=%d data=", packetId, length);
    for (int index = 0; index < length; ++index) std::fprintf(stderr, "%02X", bytes == nullptr ? 0 : bytes[index]);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}
