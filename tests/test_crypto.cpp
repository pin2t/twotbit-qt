#include "tests.h"

#include "bitcoin/uint256.h"
#include "crypto/hashes.h"
#include "crypto/secp256k1.h"
#include "encoding/encodings.h"

namespace tb::test {

void CryptoTests::ripemd160() {
    const std::pair<std::string, std::string> cases[] = {
        {"", "9c1185a5c5e9fc54612808977ee8f548b2258d31"},
        {"abc", "8eb208f7e05d987a9b044a8e98c6b087f15a0bfc"},
        {"message digest", "5d0689ef49d2fae572b881b123a85ffa21595f36"},
        {"abcdefghijklmnopqrstuvwxyz", "f71c27109c692c1b56bbdceb5b9d2865b3708dbc"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "12a053384a9c0c88e405a06c27dcf49ada62eb2b"},
    };
    for (const auto& [input, want] : cases) EXPECT_EQ(toHex(RIPEMD160::hash(bytesOf(input))), want, input);
    std::string repeated;
    for (int i = 0; i < 8; ++i) repeated += "1234567890";
    EXPECT_EQ(toHex(RIPEMD160::hash(bytesOf(repeated))), std::string("9b752e45573d4b39f4dbd3323cab82bf63326bfb"), "8 × 1234567890");
    EXPECT_EQ(toHex(RIPEMD160::hash(Bytes(1'000'000, 'a'))), std::string("52783243c1697bdbe16d37f97f68f08325dc1528"), "a million a");
}

void CryptoTests::sha256AndSha512() {
    EXPECT_EQ(toHex(Hashes::sha256(Bytes())), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "empty");
    EXPECT_EQ(toHex(Hashes::sha256(bytesOf("abc"))), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "abc");
    EXPECT_EQ(toHex(Hashes::sha256(bytesOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))),
              std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"), "two blocks");
    EXPECT_EQ(toHex(Hashes::sha256(Bytes(1'000'000, 'a'))), std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"),
              "a million a");
    EXPECT_EQ(toHex(Hashes::sha512(bytesOf("abc"))),
              std::string("ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"),
              "sha512 abc");
    // Written in pieces of every length up to two blocks, the hash is the
    // same as of the whole.
    Bytes data;
    for (int i = 0; i < 300; ++i) data.push_back(uint8_t(i * 7));
    for (size_t piece = 1; piece < 140; piece += 13) {
        SHA256 sha;
        for (size_t at = 0; at < data.size(); at += piece) sha.write(data.data() + at, std::min(piece, data.size() - at));
        Bytes out(32);
        sha.finalize(out.data());
        EXPECT_EQ(out, Hashes::sha256(data), "pieces of " + std::to_string(piece));
    }
    EXPECT_EQ(Hashes::sha256d(bytesOf("hello")).hex(), std::string("503d8319a48348cdc610a582f7bf754b5833df65038606eb48510790dfc99595"), "sha256d");
}

void CryptoTests::sipHash() {
    for (const auto& v : fixture("sync")["siphash"].items()) {
        Bytes key = v["key"].hex();
        uint64_t k0 = 0, k1 = 0;
        for (int i = 0; i < 8; ++i) k0 |= uint64_t(key[size_t(i)]) << (8 * i);
        for (int i = 0; i < 8; ++i) k1 |= uint64_t(key[size_t(8 + i)]) << (8 * i);
        EXPECT_EQ(SipHash::hash(k0, k1, v["data"].hex()), v["hash"].u64(), v["data"].str());
    }
    uint64_t k0 = 0, k1 = 0;
    for (int i = 0; i < 8; ++i) k0 |= uint64_t(i) << (8 * i);
    for (int i = 0; i < 8; ++i) k1 |= uint64_t(8 + i) << (8 * i);
    Bytes data;
    for (int i = 0; i < 15; ++i) data.push_back(uint8_t(i));
    EXPECT_EQ(SipHash::hash(k0, k1, data), uint64_t(0xa129ca6149be45e5ull), "reference vector");
}

void CryptoTests::murmur3() {
    for (const auto& v : fixture("sync")["murmur"].items())
        EXPECT_EQ(Murmur3::hash(uint32_t(v["seed"].u64()), v["data"].hex()), uint32_t(v["hash"].u64()), v["data"].str());
}

void CryptoTests::pbkdf2AndHMAC() {
    Bytes mac = Hashes::hmacSHA512(Bytes(20, 0x0b), bytesOf("Hi There"));
    EXPECT_EQ(toHex(mac),
              std::string("87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cdedaa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854"),
              "hmac");
    Bytes key = Hashes::pbkdf2SHA512(bytesOf("password"), bytesOf("salt"), 1, 64);
    EXPECT_EQ(toHex(key),
              std::string("867f70cf1ade02cff3752599a3a53dc4af34c7a669815ae5d513554e1c8cf252c02d470a285a0501bad999bfe943c08f050235d7d68b1da55e63f73b60a57fce"),
              "pbkdf2");
    // A key longer than the block is hashed first.
    Bytes longKey(131, 0xaa);
    EXPECT_EQ(toHex(Hashes::hmacSHA512(longKey, bytesOf("Test Using Larger Than Block-Size Key - Hash Key First"))),
              std::string("80b24263c7c1a3ebb71493c1dd7be8b49b46d1f41b4aeec1121b013783f8f3526b56d037e05f2598bd0fd2215d6a1e5295e64f73f63f0aec8b915a985d786598"),
              "long key");
}

void CryptoTests::secp256k1SignAndVerify() {
    Bytes key = h("0000000000000000000000000000000000000000000000000000000000000001");
    Bytes pub = Secp256k1::publicKey(key);
    EXPECT_EQ(toHex(pub), std::string("0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"), "public key");
    Bytes hash = Hashes::sha256(bytesOf("Satoshi Nakamoto"));
    Bytes sig = Secp256k1::sign(hash, key);
    // RFC6979 vector for key 1 and "Satoshi Nakamoto" (low S).
    EXPECT_EQ(toHex(sig),
              std::string("3045022100934b1ea10a4b3c1757e2b0c017d0b6143ce3c9a7e6a4a49860d7a6ab210ee3d802202442ce9d2b916064108014783e923ec36b49743e2ffa1c4496f01a512aafd9e5"),
              "signature");
    EXPECT(Secp256k1::verify(sig, hash, pub), "verifies");
    EXPECT(!Secp256k1::verify(sig, Hashes::sha256(bytesOf("x")), pub), "other hash");
    EXPECT(!Secp256k1::isValidPrivateKey(Bytes(32, 0)), "zero key");
    EXPECT(!Secp256k1::isValidPrivateKey(h("fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141")), "curve order");
    bool threw = false;
    try {
        Secp256k1::tweakAdd(key, h("fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141"));
    } catch (const KeyError& e) {
        threw = e.kind == KeyError::Kind::invalidChild;
    }
    EXPECT(threw, "tweak by the order is an invalid child");
}

void CryptoTests::base58() {
    EXPECT_EQ(Base58::encode(h("00000000000000000000")), std::string("1111111111"), "zeros");
    EXPECT_EQ(Base58::encode(bytesOf("hello world")), std::string("StV1DL6CwTryKyV"), "hello world");
    EXPECT(Base58::decode("StV1DL6CwTryKyV") == bytesOf("hello world"), "decode");
    EXPECT(!Base58::decode("0OIl"), "invalid characters");
    auto check = Base58::decodeCheck("1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2");
    EXPECT(check && toHex(*check) == "0077bff20c60e522dfaa3350c39b030a5d004e839a", "check");
    EXPECT(!Base58::decodeCheck("1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN3"), "bad checksum");
}

void CryptoTests::bech32() {
    for (const char* valid : {"A12UEL5L", "a12uel5l", "an83characterlonghumanreadablepartthatcontainsthenumber1andtheexcludedcharactersbio1tt5tgs",
                              "abcdef1qpzry9x8gf2tvdw0s3jn54khce6mua7lmqqqxw", "split1checkupstagehandshakeupstreamerranterredcaperred2y9e3w"}) {
        auto d = Bech32::decode(valid);
        EXPECT(d && d->variant == Bech32::Variant::bech32, valid);
    }
    for (const char* valid : {"A1LQFN3A", "a1lqfn3a", "abcdef1l7aum6echk45nj3s0wdvt2fg8x9yrzpqzd3ryx", "?1v759aa"}) {
        auto d = Bech32::decode(valid);
        EXPECT(d && d->variant == Bech32::Variant::bech32m, valid);
    }
    for (const char* invalid : {"pzry9x0s0muk", "1pzry9x0s0muk", "x1b4n0q5v", "li1dgmt3", "A1G7SGD8", "10a06t8", "1qzzfhee", "a12UEL5L"})
        EXPECT(!Bech32::decode(invalid), invalid);
    auto segwit = Bech32::decodeSegwit("bc", "BC1QW508D6QEJXTDG4Y5R3ZARVARY0C5XW7KV8F3T4");
    EXPECT(segwit && segwit->version == 0 && toHex(segwit->program) == "751e76e8199196d454941c45d1b3a323f1433bd6", "segwit v0");
    EXPECT_EQ(Bech32::encodeSegwit("bc", 0, h("751e76e8199196d454941c45d1b3a323f1433bd6")), std::string("bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4"),
              "encode");
    auto taproot = Bech32::decodeSegwit("bc", "bc1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqzk5jj0");
    EXPECT(taproot && taproot->version == 1, "taproot");
    EXPECT(!Bech32::decodeSegwit("bc", "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kemeawh"), "v0 in bech32m");
    EXPECT(!Bech32::decodeSegwit("bc", "bc1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqh2y7hd"), "v1 in bech32");
    EXPECT(!Bech32::decodeSegwit("tb", "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4"), "wrong prefix");
}

void CryptoTests::varInts() {
    const std::pair<uint64_t, size_t> cases[] = {{0, 1}, {0xfc, 1}, {0xfd, 3}, {0xffff, 3}, {0x10000, 5}, {0xffffffff, 5}, {0x100000000, 9}};
    for (const auto& [value, size] : cases) {
        ByteWriter w;
        w.writeVarInt(value);
        EXPECT_EQ(w.data().size(), size, std::to_string(value));
        EXPECT_EQ(varIntSize(value), size, std::to_string(value));
        ByteReader r(w.data());
        EXPECT_EQ(r.readVarInt(), value, std::to_string(value));
    }
    Bytes nonCanonical = h("fd0100");
    ByteReader r(nonCanonical);
    bool threw = false;
    try {
        r.readVarInt();
    } catch (const DecodeError&) {
        threw = true;
    }
    EXPECT(threw, "non-canonical compact size");
}

void CryptoTests::uint256AndCompact() {
    UInt256 limit = *UInt256::fromHex("ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    EXPECT_EQ(Compact::bits(limit), uint32_t(0x1d00ffff), "bits");
    EXPECT_EQ(Compact::target(0x1d00ffff)->hex(), std::string("00000000ffff0000000000000000000000000000000000000000000000000000"), "target");
    EXPECT_EQ(limit.bitLength(), 224, "bit length");
    EXPECT_EQ((limit >> 200).hex(), std::string(58, '0') + "ffffff", "shift");
    EXPECT(UInt256(1000).scaled(3, 7) == UInt256(428), "scaled");
}

} // namespace tb::test
