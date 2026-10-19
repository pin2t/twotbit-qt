#pragma once

#include <filesystem>
#include <string>

namespace tb {

/// The path of UTF-8 text, as the wallet passes paths around.
inline std::filesystem::path pathOf(const std::string& utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

/// The UTF-8 text of a path.
inline std::string utf8Of(const std::filesystem::path& path) {
    std::u8string s = path.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

/// Log records what the wallet does, as bitfyn's log.Printf lines: to the
/// debugger output on Windows, where DebugView shows it, and to twotbit.log
/// in the data directory once a file is set.
namespace Log {

/// Appends the log from now on to the file, setting an old one aside as
/// twotbit.log.old once it grew past 20 MB.
void configure(const std::filesystem::path& file);

/// Stops appending to the file.
void close();

void info(const std::string& message);

} // namespace Log

} // namespace tb
