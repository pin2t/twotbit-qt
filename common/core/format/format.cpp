#include "format/format.h"

#include <charconv>
#include <cstdio>

#include "util/strings.h"

namespace tb {

namespace Format {

namespace {

std::string trimZeros(std::string s) {
    while (!s.empty() && s.back() == '0') s.pop_back();
    return s;
}

std::string padded8(uint64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "%08llu", (unsigned long long)v);
    return buf;
}

uint64_t magnitude(int64_t v) { return v < 0 ? uint64_t(0) - uint64_t(v) : uint64_t(v); }

struct TimeUnit {
    int64_t seconds;
    const char* name;
};

/// The units ages are told in, the largest first.
constexpr TimeUnit timeUnits[] = {
    {365 * 24 * 3600, "year"}, {30 * 24 * 3600, "month"}, {24 * 3600, "day"}, {3600, "hour"}, {60, "minute"},
};

/// n of the unit: "1 day", "3 days".
std::string count(int64_t n, const char* unit) { return n == 1 ? std::string("1 ") + unit : std::to_string(n) + " " + unit + "s"; }

bool parseInt64(const std::string& s, int64_t& out) {
    const char* begin = s.data();
    const char* end = s.data() + s.size();
    if (begin != end && *begin == '+') ++begin;
    auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc() && ptr == end && begin != end;
}

} // namespace

std::string amount(int64_t sats) {
    std::string sign = sats < 0 ? "-" : "";
    uint64_t abs = magnitude(sats);
    if (abs < uint64_t(btcThreshold)) return sign + group(std::to_string(abs)) + " sats";
    std::string whole = group(std::to_string(abs / uint64_t(satsPerBTC)));
    std::string frac = trimZeros(padded8(abs % uint64_t(satsPerBTC)));
    return frac.empty() ? sign + whole + " BTC" : sign + whole + "." + frac + " BTC";
}

std::string group(const std::string& digits) {
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out.push_back(' ');
        out.push_back(digits[i]);
    }
    return out;
}

std::string pendingNote(int64_t pending) { return pending <= 0 ? "" : "(" + amount(pending) + " pending)"; }

int64_t usdValue(int64_t sats, int64_t centsPerBTC) {
    int64_t sign = sats < 0 ? -1 : 1;
    uint64_t abs = magnitude(sats);
    __int128 whole = __int128(abs / uint64_t(satsPerBTC)) * centsPerBTC;
    __int128 frac = (__int128(abs % uint64_t(satsPerBTC)) * centsPerBTC + satsPerBTC / 2) / satsPerBTC;
    return int64_t(sign * (whole + frac));
}

std::string usd(int64_t cents) {
    std::string sign = cents < 0 ? "-" : "";
    uint64_t abs = magnitude(cents);
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02llu", (unsigned long long)(abs % 100));
    return sign + group(std::to_string(abs / 100)) + "." + buf + " USD";
}

int64_t parseAmount(const std::string& text, AmountUnit unit) {
    std::string clean;
    for (char c : trimSpaces(text))
        if (c != ' ') clean.push_back(c);
    if (clean.empty()) return 0;
    int64_t sats;
    if (unit == AmountUnit::sats) {
        bool digits = allDigits(clean) || (clean.size() > 1 && clean[0] == '+' && allDigits(std::string_view(clean).substr(1)));
        int64_t n;
        if (!digits || !parseInt64(clean, n) || n < 0) throw AmountError("enter a whole number of sats");
        sats = n;
    } else {
        size_t dot = clean.find('.');
        std::string whole = dot == std::string::npos ? clean : clean.substr(0, dot);
        std::string frac = dot == std::string::npos ? "" : clean.substr(dot + 1);
        if (frac.size() > 8) throw AmountError("BTC amounts have at most 8 decimals");
        bool sign = clean.find_first_of("+-") != std::string::npos;
        int64_t w = 0, f = 0;
        bool ok = !sign && allDigits(whole) && allDigits(frac) && !(whole.empty() && frac.empty()) &&
                  parseInt64(whole.empty() ? "0" : whole, w) && w <= INT64_MAX / satsPerBTC &&
                  parseInt64(frac + std::string(8 - frac.size(), '0'), f);
        if (!ok) throw AmountError("enter an amount like 0.0015");
        sats = w * satsPerBTC + f;
    }
    if (sats > maxSats) throw AmountError("amount exceeds 21 million BTC");
    return sats;
}

std::string unitText(int64_t sats, AmountUnit unit) { return unit == AmountUnit::sats ? std::to_string(sats) : btcDecimal(sats); }

std::string btcDecimal(int64_t sats) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "%08lld", (long long)(sats % satsPerBTC));
    std::string frac = trimZeros(buf);
    std::string whole = std::to_string(sats / satsPerBTC);
    return frac.empty() ? whole : whole + "." + frac;
}

std::string invoiceURI(const std::string& address, int64_t sats) {
    return sats > 0 ? "bitcoin:" + address + "?amount=" + btcDecimal(sats) : "bitcoin:" + address;
}

std::string netAmount(int64_t net) {
    if (net > 0) return "+ " + amount(net);
    if (net < 0) return "- " + amount(-net);
    return amount(0);
}

std::string relativeTime(int64_t unix, int64_t now) {
    if (unix == 0) return "—";
    int64_t age = now - unix;
    for (const auto& u : timeUnits)
        if (age / u.seconds >= 1) return count(age / u.seconds, u.name) + " ago";
    return "just now";
}

std::string lifetime(int64_t created, int64_t now, int units) {
    if (created == 0) return "—";
    int64_t age = now - created;
    constexpr size_t n = sizeof timeUnits / sizeof timeUnits[0];
    for (size_t i = 0; i < n; ++i) {
        const TimeUnit& u = timeUnits[i];
        int64_t whole = age / u.seconds;
        if (whole < 1) continue;
        std::string text = count(whole, u.name);
        if (units > 1 && i + 1 < n) {
            const TimeUnit& next = timeUnits[i + 1];
            int64_t rest = age % u.seconds / next.seconds;
            if (rest >= 1) text += " " + count(rest, next.name);
        }
        return text;
    }
    return "less than a minute";
}

std::string groupAddress(const std::string& address) {
    if (address.size() <= 4) return address;
    size_t rest = address.size() % 4;
    size_t first = rest == 0 ? 4 : rest + 4;
    std::string out = address.substr(0, first);
    for (size_t i = first; i < address.size(); i += 4) out += " " + address.substr(i, 4);
    return out;
}

std::string fitAddress(const std::string& address, const std::function<bool(const std::string&)>& fits) {
    const int tail = 6;
    const std::string ellipsis = "…";
    int length = int(address.size());
    if (length <= tail + 1 || fits(address)) return address;
    std::string end = address.substr(size_t(length - tail));
    int lo = 5, hi = length - tail - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (fits(address.substr(0, size_t(mid)) + ellipsis + end)) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return best < 0 ? ellipsis + end : address.substr(0, size_t(best)) + ellipsis + end;
}

} // namespace Format

WalletStats WalletStats::of(const std::vector<WalletTransaction>& txs, int unspent) {
    WalletStats s;
    s.transactions = int(txs.size());
    s.unspent = unspent;
    for (const auto& t : txs) {
        for (const auto& o : t.outputs)
            if (o.mine) ++s.coins;
        if (t.fee > 0) {
            s.fees += t.fee;
            s.sent += -t.net - t.fee;
        } else if (t.net > 0) {
            s.received += t.net;
        } else {
            s.sent -= t.net;
        }
    }
    return s;
}

} // namespace tb
