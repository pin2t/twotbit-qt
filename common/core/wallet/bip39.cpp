#include "wallet/bip39.h"

#include <QString>

#include <cstring>
#include <unordered_map>

#include "crypto/hashes.h"
#include "util/strings.h"

namespace tb::BIP39 {

namespace {

const std::unordered_map<std::string, int>& wordIndex() {
    static const std::unordered_map<std::string, int> index = [] {
        std::unordered_map<std::string, int> m;
        for (size_t i = 0; i < wordlist().size(); ++i) m[wordlist()[i]] = int(i);
        return m;
    }();
    return index;
}

Bytes nfkd(const std::string& text) {
    QByteArray utf8 = QString::fromUtf8(text.data(), qsizetype(text.size())).normalized(QString::NormalizationForm_KD).toUtf8();
    return Bytes(utf8.begin(), utf8.end());
}

} // namespace

int indexOf(const std::string& word) {
    auto it = wordIndex().find(word);
    return it == wordIndex().end() ? -1 : it->second;
}

std::string mnemonic(ByteView entropy) {
    if (entropy.size() < 16 || entropy.size() > 32 || entropy.size() % 4 != 0) throw MnemonicError::invalidEntropy();
    size_t checksumBits = entropy.size() * 8 / 32;
    std::vector<bool> bits;
    for (uint8_t byte : entropy)
        for (int i = 7; i >= 0; --i) bits.push_back((byte >> i) & 1);
    Bytes hash = Hashes::sha256(entropy);
    for (size_t i = 0; i < checksumBits; ++i) bits.push_back((hash[i / 8] >> (7 - i % 8)) & 1);
    std::vector<std::string> words;
    for (size_t chunk = 0; chunk < bits.size(); chunk += 11) {
        int index = 0;
        for (size_t b = chunk; b < chunk + 11; ++b) index = index << 1 | (bits[b] ? 1 : 0);
        words.emplace_back(wordlist()[size_t(index)]);
    }
    return join(words, " ");
}

std::string generateMnemonic(int bits) {
    if (bits % 32 != 0) throw MnemonicError::invalidEntropy();
    return mnemonic(Hashes::randomBytes(size_t(bits / 8)));
}

Bytes entropy(const std::string& mnemonic) {
    std::vector<std::string> list = words(mnemonic);
    size_t n = list.size();
    if (n != 12 && n != 15 && n != 18 && n != 21 && n != 24) throw MnemonicError::invalidMnemonic();
    std::vector<bool> bits;
    for (const auto& word : list) {
        int index = indexOf(word);
        if (index < 0) throw MnemonicError::invalidMnemonic();
        for (int i = 10; i >= 0; --i) bits.push_back((index >> i) & 1);
    }
    size_t checksumBits = bits.size() / 33;
    size_t entropyBits = bits.size() - checksumBits;
    Bytes entropy(entropyBits / 8, 0);
    for (size_t i = 0; i < entropyBits; ++i)
        if (bits[i]) entropy[i / 8] |= uint8_t(1 << (7 - i % 8));
    Bytes hash = Hashes::sha256(entropy);
    for (size_t i = 0; i < checksumBits; ++i) {
        bool want = (hash[i / 8] >> (7 - i % 8)) & 1;
        if (bits[entropyBits + i] != want) throw MnemonicError::invalidMnemonic();
    }
    return entropy;
}

bool isValid(const std::string& mnemonic) {
    try {
        entropy(mnemonic);
        return true;
    } catch (const MnemonicError&) {
        return false;
    }
}

Bytes seed(const std::string& mnemonic, const std::string& passphrase) {
    return Hashes::pbkdf2SHA512(nfkd(mnemonic), nfkd("mnemonic" + passphrase), 2048, 64);
}

} // namespace tb::BIP39
