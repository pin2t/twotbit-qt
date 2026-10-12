#include "util/log.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace tb::Log {

namespace {

/// The size from which the log file is set aside at startup as
/// twotbit.log.old.
constexpr uintmax_t maxFileSize = 20 * 1024 * 1024;

std::mutex mutex;
FILE* file = nullptr;

/// Whether log lines are echoed to standard error, as when the
/// TWOTBIT_LOG_STDERR environment variable is set.
bool echo() {
    static const bool on = std::getenv("TWOTBIT_LOG_STDERR") != nullptr;
    return on;
}

std::string timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm parts{};
#ifdef _WIN32
    localtime_s(&parts, &t);
#else
    localtime_r(&t, &parts);
#endif
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d/%02d/%02d %02d:%02d:%02d", parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
                  parts.tm_hour, parts.tm_min, parts.tm_sec);
    return buf;
}

} // namespace

void configure(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::file_size(path, ec) > maxFileSize && !ec) {
        std::filesystem::path old = path;
        old += ".old";
        std::filesystem::remove(old, ec);
        std::filesystem::rename(path, old, ec);
    }
#ifdef _WIN32
    FILE* f = _wfopen(path.wstring().c_str(), L"ab");
#else
    FILE* f = std::fopen(path.string().c_str(), "ab");
#endif
    if (!f) return;
    std::lock_guard lock(mutex);
    if (file) std::fclose(file);
    file = f;
}

void close() {
    std::lock_guard lock(mutex);
    if (file) std::fclose(file);
    file = nullptr;
}

void info(const std::string& message) {
    std::string line = timestamp() + " " + message + "\n";
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, 0, line.data(), int(line.size()), nullptr, 0);
    std::wstring wide(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, line.data(), int(line.size()), wide.data(), n);
    OutputDebugStringW(wide.c_str());
#endif
    std::lock_guard lock(mutex);
    if (echo()) {
        std::fwrite(line.data(), 1, line.size(), stderr);
        std::fflush(stderr);
    }
    if (file) {
        std::fwrite(line.data(), 1, line.size(), file);
        std::fflush(file);
    }
}

} // namespace tb::Log
