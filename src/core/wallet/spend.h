#pragma once

#include <string>
#include <vector>

#include "bitcoin/primitives.h"

namespace tb {

/// Coin is one spendable wallet output with the derivation path of the key
/// that controls it, and the unix time it appeared: the time of its block,
/// or when its unconfirmed transaction was first seen. Time is 0 when
/// unknown.
struct Coin {
    OutPoint outPoint;
    int64_t value = 0;
    Bytes pkScript;
    std::string address;
    std::string path;
    bool confirmed = false;
    int64_t time = 0;

    friend bool operator==(const Coin&, const Coin&) = default;
};

/// Spend is a planned transaction: the coins it spends, the amount paid,
/// the fee and the change returned to the wallet, all in satoshis. Change is
/// zero when it would be dust; the remainder then goes to the fee.
struct Spend {
    std::vector<Coin> inputs;
    int64_t amount = 0;
    int64_t fee = 0;
    int64_t change = 0;

    friend bool operator==(const Spend&, const Spend&) = default;
};

class SpendError : public Error {
public:
    enum class Kind { insufficientFunds, dustAmount, feeRateTooLow, signing };

    SpendError(Kind kind, const std::string& what) : Error(what), kind(kind) {}
    static SpendError insufficientFunds() { return {Kind::insufficientFunds, "insufficient funds"}; }
    static SpendError dustAmount(int64_t amount, int64_t limit) {
        return {Kind::dustAmount, "amount " + std::to_string(amount) + " sat is below the " + std::to_string(limit) + " sat dust limit"};
    }
    static SpendError feeRateTooLow() { return {Kind::feeRateTooLow, "fee rate must be at least 1 sat/vB"}; }
    static SpendError signing(const std::string& what) { return {Kind::signing, what}; }

    Kind kind;
};

/// SpendPlanner sizes and plans payments from wallet coins: the virtual size
/// estimate, the dust limit, coin selection and the maximum amount.
namespace SpendPlanner {

/// The sequence of every input: BIP125 replaceable, so a stuck payment can
/// be bumped later.
constexpr uint32_t sequenceRBF = 0xffffffff - 2;

/// The virtual size in vbytes of a transaction spending that many P2WPKH
/// inputs to the output scripts. It never underestimates: signatures are
/// counted at their maximum length.
int64_t estimateVSize(size_t inputs, const std::vector<Bytes>& outputs);

/// The smallest output value Bitcoin Core relays for the script at its
/// default dust relay fee of 3 sat/vB: the cost of creating and later
/// spending the output.
int64_t dustLimit(ByteView script);

/// Plans paying the amount to the destination script at the fee rate in
/// sat/vB. With useAll every given coin is spent, as when the user picked
/// them; otherwise coins are added largest first, confirmed ones before
/// unconfirmed, until they cover the amount and the fee. Change goes to the
/// change script unless it would be dust.
Spend plan(const std::vector<Coin>& coins, bool useAll, const Bytes& dest, int64_t amount, int64_t feeRate, const Bytes& change);

/// The most the coins can pay to the destination at the fee rate, with no
/// change: their value minus the fee, or 0 when that is dust.
int64_t maxAmount(const std::vector<Coin>& coins, const Bytes& dest, int64_t feeRate);

} // namespace SpendPlanner

} // namespace tb
