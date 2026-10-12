#pragma once

#include <array>
#include <string>

#include "util/bytes.h"

namespace tb {

/// MnemonicError is entropy or a phrase BIP39 does not allow.
class MnemonicError : public Error {
public:
    explicit MnemonicError(const std::string& what) : Error(what) {}
    static MnemonicError invalidEntropy() { return MnemonicError("entropy must be 128 to 256 bits in steps of 32"); }
    static MnemonicError invalidMnemonic() { return MnemonicError("invalid BIP39 mnemonic"); }
};

/// BIP39 mnemonics: the 12 to 24 English words a wallet seed is written
/// down as, and the seed they stretch into.
namespace BIP39 {

/// The 2048 words of the English list, in order.
const std::array<const char*, 2048>& wordlist();

/// The index of the word in the list, -1 when it is not there.
int indexOf(const std::string& word);

/// Encodes the entropy, 16 to 32 bytes in steps of 4, as a mnemonic: the
/// entropy and the first bits of its SHA256 as a checksum, in 11-bit words.
std::string mnemonic(ByteView entropy);

/// A fresh mnemonic from the system's secure random source; 128 bits of
/// entropy make the default 12 words.
std::string generateMnemonic(int bits = 128);

/// The entropy a valid mnemonic encodes. Words are split on whitespace,
/// must number 12, 15, 18, 21 or 24, must all be in the English list
/// exactly, and must carry a matching checksum.
Bytes entropy(const std::string& mnemonic);

bool isValid(const std::string& mnemonic);

/// The 64-byte seed of the mnemonic and the optional passphrase:
/// PBKDF2-HMAC-SHA512 over the NFKD normalized sentence, salted with
/// "mnemonic" and the passphrase, 2048 rounds.
Bytes seed(const std::string& mnemonic, const std::string& passphrase = "");

} // namespace BIP39

} // namespace tb
