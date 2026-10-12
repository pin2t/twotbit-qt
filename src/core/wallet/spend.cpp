#include "wallet/spend.h"

#include <algorithm>

#include "bitcoin/script.h"

namespace tb::SpendPlanner {

namespace {

/// Transaction weights in weight units: the fixed part of a segwit
/// transaction, and one P2WPKH input with a worst-case 72-byte signature.
constexpr int64_t txOverheadWeight = (4 + 4 + 1 + 1) * 4 + 2;
constexpr int64_t p2wpkhInputWeight = (32 + 4 + 1 + 4) * 4 + 1 + 1 + 72 + 1 + 33;

} // namespace

int64_t estimateVSize(size_t inputs, const std::vector<Bytes>& outputs) {
    int64_t weight = txOverheadWeight + int64_t(inputs) * p2wpkhInputWeight;
    for (const auto& script : outputs) weight += int64_t(8 + varIntSize(script.size()) + script.size()) * 4;
    return (weight + 3) / 4;
}

int64_t dustLimit(ByteView script) {
    int64_t spend = 32 + 4 + 1 + 107 + 4;
    if (Script::isWitnessProgram(script)) spend = 32 + 4 + 1 + 107 / 4 + 4;
    return (int64_t(8 + varIntSize(script.size()) + script.size()) + spend) * 3;
}

Spend plan(const std::vector<Coin>& coins, bool useAll, const Bytes& dest, int64_t amount, int64_t feeRate, const Bytes& change) {
    if (amount < dustLimit(dest)) throw SpendError::dustAmount(amount, dustLimit(dest));
    if (feeRate < 1) throw SpendError::feeRateTooLow();
    std::vector<Coin> candidates = coins;
    if (!useAll) {
        std::stable_sort(candidates.begin(), candidates.end(), [](const Coin& a, const Coin& b) {
            if (a.confirmed != b.confirmed) return a.confirmed;
            return a.value > b.value;
        });
    }
    for (size_t n = useAll ? candidates.size() : 1; n <= candidates.size(); ++n) {
        std::vector<Coin> inputs(candidates.begin(), candidates.begin() + long(n));
        int64_t sum = 0;
        for (const auto& c : inputs) sum += c.value;
        int64_t withChange = estimateVSize(n, {dest, change}) * feeRate;
        int64_t rest = sum - amount - withChange;
        if (rest >= dustLimit(change)) return Spend{inputs, amount, withChange, rest};
        int64_t noChange = estimateVSize(n, {dest}) * feeRate;
        if (sum - amount >= noChange) return Spend{inputs, amount, sum - amount, 0};
    }
    throw SpendError::insufficientFunds();
}

int64_t maxAmount(const std::vector<Coin>& coins, const Bytes& dest, int64_t feeRate) {
    int64_t sum = 0;
    for (const auto& c : coins) sum += c.value;
    int64_t amount = sum - estimateVSize(coins.size(), {dest}) * feeRate;
    return amount < dustLimit(dest) ? 0 : amount;
}

} // namespace tb::SpendPlanner
