#pragma once

#include <string>
#include <vector>

#include "bitcoin/hash256.h"

namespace tb {

/// TxAddress is an address a transaction spends from or pays to, and
/// whether it is the wallet's. Address is empty when it cannot be told: an
/// input whose previous output the wallet does not hold, spending a script
/// that leaves no trace of its address. A coinbase input reads "coinbase".
struct TxAddress {
    std::string address;
    bool mine = false;

    friend bool operator==(const TxAddress&, const TxAddress&) = default;
};

/// WalletTransaction is a wallet transaction as the history shows it: when
/// it was confirmed or first seen, what it did to the balance, the fee the
/// wallet paid, set when every input spends a wallet coin, and the addresses
/// it spends from and pays to.
struct WalletTransaction {
    Hash256 txid;
    int64_t time = 0;
    bool confirmed = false;
    int64_t net = 0;
    int64_t fee = 0;
    std::vector<TxAddress> inputs;
    std::vector<TxAddress> outputs;

    friend bool operator==(const WalletTransaction&, const WalletTransaction&) = default;
};

} // namespace tb
