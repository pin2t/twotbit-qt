#include "tests.h"

#include <algorithm>

#include "bitcoin/script.h"
#include "crypto/secp256k1.h"
#include "wallet/bip39.h"
#include "util/strings.h"
#include "wallet/hdwallet.h"

namespace tb::test {

void WalletTests::wordlist() {
    const auto& words = BIP39::wordlist();
    EXPECT_EQ(words.size(), size_t(2048), "count");
    EXPECT(std::is_sorted(words.begin(), words.end(), [](const char* a, const char* b) { return std::string(a) < b; }), "sorted");
    EXPECT_EQ(std::string(words.front()), std::string("abandon"), "first");
    EXPECT_EQ(std::string(words.back()), std::string("zoo"), "last");
}

void WalletTests::mnemonics() {
    const Json& f = fixture("wallet");
    for (const auto& v : f["mnemonics"].items()) {
        EXPECT_EQ(BIP39::mnemonic(v["entropy"].hex()), v["mnemonic"].str(), v["entropy"].str());
        EXPECT_EQ(toHex(BIP39::entropy(v["mnemonic"].str())), v["entropy"].str(), v["mnemonic"].str());
        EXPECT_EQ(toHex(BIP39::seed(v["mnemonic"].str())), v["seed"].str(), v["mnemonic"].str());
        EXPECT_EQ(toHex(BIP39::seed(v["mnemonic"].str(), "TREZOR")), v["seedTrezor"].str(), v["mnemonic"].str());
    }
    for (const auto& bad : f["invalidMnemonics"].strings()) EXPECT(!BIP39::isValid(bad), bad);
    std::string fresh = BIP39::generateMnemonic();
    EXPECT_EQ(words(fresh).size(), size_t(12), fresh);
    EXPECT(BIP39::isValid(fresh), fresh);
}

void WalletTests::derivation() {
    for (const auto& v : fixture("wallet")["wallets"].items()) {
        HDWallet wallet(v["mnemonic"].str(), networkNamed(v["network"].str()));
        EXPECT_EQ(wallet.accountXPub(), v["xpub"].str(), v["network"].str());
        for (const auto& a : v["receive"].items()) {
            DerivedAddress d = wallet.receiveAddress(uint32_t(a["index"].u64()));
            EXPECT_EQ(d.address, a["address"].str(), a["path"].str());
            EXPECT_EQ(d.path, a["path"].str(), a["path"].str());
            EXPECT_EQ(toHex(d.pubkey), a["pubkey"].str(), a["path"].str());
        }
        for (const auto& a : v["change"].items()) {
            DerivedAddress d = wallet.changeAddress(uint32_t(a["index"].u64()));
            EXPECT_EQ(d.address, a["address"].str(), a["path"].str());
            EXPECT_EQ(d.path, a["path"].str(), a["path"].str());
            EXPECT_EQ(toHex(d.pubkey), a["pubkey"].str(), a["path"].str());
        }
    }
}

void WalletTests::bip84Vectors() {
    HDWallet wallet(vectorMnemonic, Network::mainnet());
    EXPECT_EQ(wallet.receiveAddress(0).address, std::string("bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu"), "receive 0");
    EXPECT_EQ(wallet.receiveAddress(1).address, std::string("bc1qnjg0jd8228aq7egyzacy8cys3knf9xvrerkf9g"), "receive 1");
    EXPECT_EQ(wallet.changeAddress(0).address, std::string("bc1q8c6fshw2dlwun7ekn9qwf37cu2rn755upcp6el"), "change 0");
    EXPECT_EQ(wallet.accountXPub(),
              std::string("zpub6rFR7y4Q2AijBEqTUquhVz398htDFrtymD9xYYfG1m4wAcvPhXNfE3EfH1r1ADqtfSdVCToUG868RvUUkgDKf31mGDtKsAYz2oz2AGutZYs"), "zpub");
    bool threw = false;
    try {
        HDWallet bad("abandon abandon", Network::mainnet());
    } catch (const Error&) {
        threw = true;
    }
    EXPECT(threw, "short mnemonic");
}

void WalletTests::paths() {
    auto path = ExtendedKey::parsePath("m/84'/0'/0'/1/3");
    EXPECT(path == (std::vector<uint32_t>{84 + 0x80000000u, 0x80000000u, 0x80000000u, 1, 3}), "parsed");
    for (const char* bad : {"", "m", "84'/0'", "m/x", "m/2147483648", "m/+1", "m//1", "n/1"}) {
        bool threw = false;
        try {
            ExtendedKey::parsePath(bad);
        } catch (const KeyError&) {
            threw = true;
        }
        EXPECT(threw, bad);
    }
}

void WalletTests::spends() {
    // Every planned and signed spend matches the Go wallet byte for byte:
    // coin selection, fees, change, BIP69 order and RFC6979 signatures.
    for (const auto& v : fixture("wallet")["spends"].items()) {
        std::string name = v["name"].str();
        const Network& network = networkNamed(v["network"].str());
        HDWallet wallet(vectorMnemonic, network);
        std::vector<Coin> coins;
        for (const auto& c : v["coins"].items()) {
            coins.push_back(Coin{OutPoint{*Hash256::fromHex(c["txid"].str()), uint32_t(c["vout"].u64())}, c["value"].i64(), c["script"].hex(),
                                 c["address"].str(), c["path"].str(), c["confirmed"].boolean(), 0});
            DerivedAddress derived = wallet.derive(uint32_t(c["chain"].u64()), uint32_t(c["index"].u64()));
            EXPECT_EQ(derived.address, coins.back().address, name);
            EXPECT(derived.script() == coins.back().pkScript, name);
        }
        Bytes dest = wallet.addressScript(v["dest"].str());
        EXPECT_EQ(toHex(dest), v["destScript"].str(), name);
        Bytes change = wallet.changeAddress(uint32_t(v["changeIndex"].u64())).script();
        EXPECT_EQ(toHex(change), v["changeScript"].str(), name);
        EXPECT_EQ(SpendPlanner::maxAmount(coins, dest, v["feeRate"].i64()), v["max"].i64(), name);
        try {
            Spend spend = SpendPlanner::plan(coins, v["useAll"].boolean(), dest, v["amount"].i64(), v["feeRate"].i64(), change);
            EXPECT(!v.has("error"), name + ": planned, want " + (v.has("error") ? v["error"].str() : ""));
            std::vector<OutPoint> want;
            for (const auto& i : v["inputs"].items()) want.push_back(coins[size_t(i.u64())].outPoint);
            std::vector<OutPoint> got;
            for (const auto& c : spend.inputs) got.push_back(c.outPoint);
            EXPECT(got == want, name + ": inputs");
            EXPECT_EQ(spend.fee, v["fee"].i64(), name);
            EXPECT_EQ(spend.change, v["change"].i64(), name);
            Transaction tx = wallet.sign(spend, dest, change);
            EXPECT_EQ(toHex(tx.serialized()), v["txHex"].str(), name);
            EXPECT_EQ(tx.txid().hex(), v["txid"].str(), name);
            EXPECT_EQ(int64_t(tx.vsize()), v["vsize"].i64(), name);
            std::vector<Bytes> outputs{dest};
            if (spend.change > 0) outputs.push_back(change);
            EXPECT_EQ(SpendPlanner::estimateVSize(spend.inputs.size(), outputs), v["estimate"].i64(), name);
            EXPECT(Transaction::parse(tx.serialized()) == tx, name + ": round trip");
            for (size_t i = 0; i < tx.inputs.size(); ++i) {
                const TxIn& input = tx.inputs[i];
                auto coin = std::find_if(coins.begin(), coins.end(), [&](const Coin& c) { return c.outPoint == input.previousOutPoint; });
                Bytes hash = SigHashes(tx).p2wpkh(tx, i, coin->pkScript, coin->value);
                Bytes signature(input.witness[0].begin(), input.witness[0].end() - 1);
                EXPECT(Secp256k1::verify(signature, hash, input.witness[1]), name + ": signature");
                EXPECT(Script::p2wpkhOfPubkey(input.witness[1]) == coin->pkScript, name + ": key");
            }
        } catch (const SpendError& e) {
            EXPECT(v.has("error") && v["error"].str() == e.what(), name + ": " + e.what());
        }
    }
}

void WalletTests::sizesAndDust() {
    const Json& f = fixture("wallet");
    for (const auto& v : f["sizes"].items())
        EXPECT_EQ(SpendPlanner::estimateVSize(size_t(v["inputs"].u64()), v["outputs"].hexes()), v["vsize"].i64(), "size");
    for (const auto& v : f["dust"].items()) EXPECT_EQ(SpendPlanner::dustLimit(v["script"].hex()), v["dust"].i64(), v["script"].str());
}

void WalletTests::addressScripts() {
    for (const auto& v : fixture("wallet")["addressScripts"].items()) {
        std::optional<Bytes> script;
        try {
            script = Address::script(v["address"].str(), networkNamed(v["network"].str()));
        } catch (const AddressError&) {
        }
        EXPECT_EQ(!script, v["error"].boolean(), v["network"].str() + " " + v["address"].str());
        if (script && v.has("script")) EXPECT_EQ(toHex(*script), v["script"].str(), v["address"].str());
    }
}

void WalletTests::bip143Sighash() {
    // BIP143's native P2WPKH example: the sighash of its second input.
    Transaction tx = Transaction::parse(
        h("0100000002fff7f7881a8099afa6940d42d1e7f6362bec38171ea3edf433541db4e4ad969f0000000000eeffffffef51e1b804cc89d182d279655c3aa89e815b1b"
          "309fe287d9b2b55d57b90ec68a0100000000ffffffff02202cb206000000001976a9148280b37df378db99f66f85c95a783a76ac7a6d5988ac9093510d000000001976a9"
          "143bde42dbee7e4dbe6a21b2d50ce2f0167faa815988ac11000000"));
    Bytes hash = SigHashes(tx).p2wpkh(tx, 1, h("00141d0f172a0ecb48aee1be1f2687d2963ae33f71a1"), 600'000'000);
    EXPECT_EQ(toHex(hash), std::string("c37af31116d1b27caf68aae9e3ac82f1477929014d5b917657d0eb49478cb670"), "sighash");
}

} // namespace tb::test
