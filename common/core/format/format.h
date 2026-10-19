#pragma once

#include <functional>
#include <string>
#include <vector>

#include "util/bytes.h"
#include "wallet/history.h"

namespace tb {

/// AmountUnit is a unit amounts are entered in.
enum class AmountUnit { sats, btc };

/// The unit's name: "sats" or "BTC".
inline const char* unitName(AmountUnit unit) { return unit == AmountUnit::sats ? "sats" : "BTC"; }

class AmountError : public Error {
public:
    explicit AmountError(const std::string& what) : Error(what) {}
};

/// Format renders and reads the amounts, times and texts the wallet shows,
/// as bitfyn's GUI does.
namespace Format {

constexpr int64_t satsPerBTC = 100'000'000;
/// The amount from which balances are shown in BTC instead of sats: 0.05
/// BTC.
constexpr int64_t btcThreshold = 5'000'000;
/// The 21 million BTC supply cap in satoshis.
constexpr int64_t maxSats = 21'000'000 * satsPerBTC;

/// An amount for display: in sats below 0.05 BTC and in BTC otherwise, the
/// whole part grouped by threes, as in "1 234 567 sats" or "1 234.456789
/// BTC". Trailing zeros of the BTC fraction are dropped.
std::string amount(int64_t sats);

/// Groups a string of digits by threes from the right with spaces.
std::string group(const std::string& digits);

/// The main balance line: the spendable balance, which counts unconfirmed
/// transactions too.
inline std::string balance(int64_t confirmed, int64_t pending) { return amount(confirmed + pending); }

/// The note next to the balance naming its incoming unconfirmed part; empty
/// when nothing is incoming: pending spends get no note.
std::string pendingNote(int64_t pending);

/// The worth of an amount in US cents at a rate in cents per bitcoin,
/// rounded half up to the cent, the whole bitcoins and the rest valued apart
/// so the product cannot overflow.
int64_t usdValue(int64_t sats, int64_t centsPerBTC);

/// US cents as dollars, grouped by threes with two decimals, as in "12
/// 345.67 USD".
std::string usd(int64_t cents);

/// Reads an entered amount in the unit: whole sats, or BTC with at most
/// eight decimals. Spaces grouping digits are ignored and an empty entry is
/// zero.
int64_t parseAmount(const std::string& text, AmountUnit unit);

/// Sats in the unit without grouping, for an entry field.
std::string unitText(int64_t sats, AmountUnit unit);

/// Sats as a plain BTC decimal with trailing zeros dropped, as BIP21 amounts
/// are written.
std::string btcDecimal(int64_t sats);

/// The BIP21 payment request for the address, with the amount when one is
/// given.
std::string invoiceURI(const std::string& address, int64_t sats);

/// What a transaction did to the balance, signed: "+ 1 000 sats" or "- 0.5
/// BTC".
std::string netAmount(int64_t net);

/// How long ago the unix time was, in its largest whole unit: "just now",
/// "5 minutes ago", "1 day ago", up to years. An unknown time reads "—" and a
/// future one, as block times may be slightly ahead, "just now".
std::string relativeTime(int64_t unix, int64_t now);

/// How long the wallet has existed since its creation, in at most that many
/// of its largest units: "1 year 2 months", "3 days", "less than a minute".
std::string lifetime(int64_t created, int64_t now, int units);

/// The address in groups of four characters counted from its end, for
/// reading it out: the characters left over join the first group, so it
/// alone may be longer, as in "bc1q0r 80j3 88qf … t4xl".
std::string groupAddress(const std::string& address);

/// The address shortened to its start and its last six characters around an
/// ellipsis when longer than the room: the start as long as fits, at least
/// five characters, "bc1q" and one more, or else dropped: "…ryt4xl". fits
/// tells whether a text fits.
std::string fitAddress(const std::string& address, const std::function<bool(const std::string&)>& fits);

} // namespace Format

/// WalletStats sums up the wallet's lifetime, unconfirmed transactions
/// included as in the balance: the value received from others and sent to
/// others and the fees paid, in satoshis, the transactions, the coins ever
/// received and those still unspent.
struct WalletStats {
    int64_t received = 0;
    int64_t sent = 0;
    int64_t fees = 0;
    int transactions = 0;
    int coins = 0;
    int unspent = 0;

    /// Sums the transactions. One the wallet paid for, every input its own,
    /// counts its fee and the outputs to others as sent: a payment between
    /// the wallet's own addresses sends nothing. Any other counts what it
    /// added to the balance as received, or what it took as sent.
    static WalletStats of(const std::vector<WalletTransaction>& txs, int unspent);

    friend bool operator==(const WalletStats&, const WalletStats&) = default;
};

} // namespace tb
