#pragma once

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace tb {

inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

/// The text without the surrounding spaces and tabs.
inline std::string trimSpaces(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
    return std::string(s.substr(b, e - b));
}

/// The text without the surrounding whitespace, newlines included.
inline std::string trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && isSpace(s[b])) ++b;
    while (e > b && isSpace(s[e - 1])) --e;
    return std::string(s.substr(b, e - b));
}

inline std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = char(c + 32);
    return out;
}

inline std::string upper(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'a' && c <= 'z') c = char(c - 32);
    return out;
}

inline bool startsWith(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }
inline bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

inline bool allDigits(std::string_view s) { return std::all_of(s.begin(), s.end(), isDigit); }

/// The words of the text split on whitespace, empty ones dropped.
inline std::vector<std::string> words(std::string_view s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isSpace(s[i])) ++i;
        size_t start = i;
        while (i < s.size() && !isSpace(s[i])) ++i;
        if (i > start) out.emplace_back(s.substr(start, i - start));
    }
    return out;
}

/// The parts of the text between the separators, empty ones kept.
inline std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t at = s.find(sep, start);
        if (at == std::string_view::npos) {
            out.emplace_back(s.substr(start));
            return out;
        }
        out.emplace_back(s.substr(start, at - start));
        start = at + 1;
    }
}

template <class Range>
std::string join(const Range& parts, std::string_view sep) {
    std::string out;
    bool first = true;
    for (const auto& p : parts) {
        if (!first) out += sep;
        out += p;
        first = false;
    }
    return out;
}

/// Parses an unsigned decimal number of digits only, nothing when it does
/// not fit.
template <class T>
bool parseUnsigned(std::string_view s, T& out) {
    if (s.empty() || !allDigits(s)) return false;
    unsigned long long v = 0;
    for (char c : s) {
        unsigned d = unsigned(c - '0');
        if (v > (~0ull - d) / 10) return false;
        v = v * 10 + d;
    }
    if (v > static_cast<unsigned long long>(std::numeric_limits<T>::max())) return false;
    out = static_cast<T>(v);
    return true;
}

} // namespace tb
