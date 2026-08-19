#include "TCrashLogger.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <iterator>
#include <new>

#ifdef _WIN32
#include <eh.h>
#include <windows.h>
#else
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace {

std::atomic_flag crashStarted = ATOMIC_FLAG_INIT;

#ifdef _WIN32
HANDLE crashFile = INVALID_HANDLE_VALUE;
#else
int crashFile = -1;
#endif

bool crashFileOpen() {
#ifdef _WIN32
    return crashFile != INVALID_HANDLE_VALUE;
#else
    return crashFile >= 0;
#endif
}

void writeBytes(const char* data, std::size_t length) {
    if (!crashFileOpen() || data == nullptr) return;
#ifdef _WIN32
    while (length > 0) {
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(length, 1u << 20));
        DWORD written = 0;
        if (!WriteFile(crashFile, data, requested, &written, nullptr) || written == 0) return;
        data += written;
        length -= written;
    }
#else
    while (length > 0) {
        const ssize_t written = ::write(crashFile, data, length);
        if (written <= 0) return;
        data += written;
        length -= static_cast<std::size_t>(written);
    }
#endif
}

void writeText(const char* text) { if (text != nullptr) writeBytes(text, std::strlen(text)); }
void writeLine(const char* text) { writeText(text); writeText("\n"); }

void writeUnsigned(std::uint64_t value) {
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    writeText(buffer);
}

void writeHex(std::uint64_t value) {
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%llX", static_cast<unsigned long long>(value));
    writeText(buffer);
}

void writeAddress(const void* address) { writeHex(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(address))); }

void writeTimestamp() {
#ifdef _WIN32
    SYSTEMTIME now = {};
    GetLocalTime(&now);
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "[%04u-%02u-%02u %02u:%02u:%02u.%03u] ", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
#else
    const std::time_t now = std::time(nullptr);
    struct tm local = {};
    localtime_r(&now, &local);
    char buffer[64] = {};
    std::strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S] ", &local);
#endif
    writeText(buffer);
}

void writeProcessContext() {
    writeText("pid=");
#ifdef _WIN32
    writeUnsigned(GetCurrentProcessId());
    writeText(" thread=");
    writeUnsigned(GetCurrentThreadId());
#else
    writeUnsigned(static_cast<std::uint64_t>(::getpid()));
#endif
    writeText("\n");
}

bool beginCrash(const char* kind) {
    if (crashStarted.test_and_set(std::memory_order_acq_rel)) return false;
    writeTimestamp();
    writeText("crash kind=");
    writeLine(kind);
    writeTimestamp();
    writeProcessContext();
    return true;
}

#ifdef _WIN32

void writeWindowsStack() {
    void* frames[32] = {};
    const USHORT count = CaptureStackBackTrace(0, static_cast<DWORD>(std::size(frames)), frames, nullptr);
    writeLine("stack:");
    for (USHORT index = 0; index < count; ++index) {
        writeText("  #");
        writeUnsigned(index);
        writeText(" ");
        writeAddress(frames[index]);
        writeText("\n");
    }
}

void writeWindowsException(EXCEPTION_POINTERS* info) {
    if (info == nullptr || info->ExceptionRecord == nullptr) {
        writeLine("exception=unavailable");
        return;
    }
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    writeText("exception_code=");
    writeHex(record->ExceptionCode);
    writeText(" flags=");
    writeHex(record->ExceptionFlags);
    writeText(" address=");
    writeAddress(record->ExceptionAddress);
    writeText("\n");
    for (DWORD index = 0; index < record->NumberParameters && index < EXCEPTION_MAXIMUM_PARAMETERS; ++index) {
        writeText("exception_parameter_");
        writeUnsigned(index);
        writeText("=");
        writeHex(record->ExceptionInformation[index]);
        writeText("\n");
    }
    if (info->ContextRecord != nullptr) {
#if defined(_M_X64) || defined(__x86_64__)
        writeText("instruction_pointer=");
        writeHex(info->ContextRecord->Rip);
        writeText("\n");
#elif defined(_M_IX86) || defined(__i386__)
        writeText("instruction_pointer=");
        writeHex(info->ContextRecord->Eip);
        writeText("\n");
#endif
    }
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* info) {
    if (beginCrash("unhandled_exception")) {
        writeWindowsException(info);
        writeWindowsStack();
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void onPureCall() {
    if (beginCrash("purecall")) writeWindowsStack();
    std::abort();
}

void onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int line, uintptr_t) {
    if (beginCrash("invalid_parameter")) {
        writeText("line=");
        writeUnsigned(line);
        writeText("\n");
        writeWindowsStack();
    }
    std::abort();
}

#else

void writePosixStack() {
    void* frames[32] = {};
    const int count = backtrace(frames, static_cast<int>(std::size(frames)));
    writeLine("stack:");
    if (crashFileOpen()) backtrace_symbols_fd(frames, count, crashFile);
}

void onPosixSignal(int signalNumber, siginfo_t*, void*) {
    if (beginCrash("signal")) {
        writeText("signal=");
        writeUnsigned(signalNumber);
        writeText("\n");
        writePosixStack();
    }
    _exit(128 + signalNumber);
}

#endif

void onTerminate() noexcept {
    if (beginCrash("terminate")) {
        const std::exception_ptr exception = std::current_exception();
        if (exception != nullptr) {
            try {
                std::rethrow_exception(exception);
            } catch (const std::exception& error) {
                writeText("what=");
                writeLine(error.what());
            } catch (...) {
                writeLine("what=<non-standard exception>");
            }
        } else writeLine("what=<no current exception>");
#ifdef _WIN32
        writeWindowsStack();
#else
        writePosixStack();
#endif
    }
    std::abort();
}

void onOutOfMemory() {
    if (beginCrash("out_of_memory")) {
#ifdef _WIN32
        writeWindowsStack();
#else
        writePosixStack();
#endif
    }
    std::abort();
}

void installSignalHandlers() {
#ifdef _WIN32
    std::signal(SIGABRT, [](int) { if (beginCrash("signal")) writeWindowsStack(); std::_Exit(134); });
    std::signal(SIGFPE, [](int) { if (beginCrash("signal")) writeWindowsStack(); std::_Exit(136); });
    std::signal(SIGILL, [](int) { if (beginCrash("signal")) writeWindowsStack(); std::_Exit(132); });
    std::signal(SIGSEGV, [](int) { if (beginCrash("signal")) writeWindowsStack(); std::_Exit(139); });
#else
    struct sigaction action = {};
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = onPosixSignal;
    action.sa_flags = SA_SIGINFO;
    for (const int signalNumber : {SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV}) sigaction(signalNumber, &action, nullptr);
#endif
}

void openCrashFile(const std::filesystem::path& applicationDirectory) {
    const std::filesystem::path logDirectory = applicationDirectory / "logs";
    std::error_code error;
    std::filesystem::create_directories(logDirectory, error);
    const std::filesystem::path logPath = logDirectory / "crash.log";
#ifdef _WIN32
    crashFile = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
#else
    crashFile = ::open(logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
#endif
}

}

void installRemoteControlCrashLogger(const std::filesystem::path& applicationDirectory) {
    openCrashFile(applicationDirectory);
    std::set_terminate(onTerminate);
    std::set_new_handler(onOutOfMemory);
#ifdef _WIN32
    _set_purecall_handler(onPureCall);
    _set_invalid_parameter_handler(onInvalidParameter);
    SetUnhandledExceptionFilter(onUnhandledException);
#endif
    installSignalHandlers();
    if (crashFileOpen()) {
        writeTimestamp();
        writeLine("crash_logger=installed");
    }
}
