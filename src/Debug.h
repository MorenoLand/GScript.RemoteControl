#pragma once

#include <cstdarg>
#include <cstdio>

extern bool remoteControlDebug;

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
