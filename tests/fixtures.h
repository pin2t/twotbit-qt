#pragma once

#include <QTest>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "bitcoin/network.h"
#include "util/bytes.h"

namespace tb::test {

/// Json is a parsed JSON value with numbers kept as their text, so 64-bit
/// fixture values survive exactly.
class Json {
public:
    enum class Type { null, boolean, number, string, array, object };

    static Json parse(const std::string& text);

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::null; }

    /// The member of an object, a null value when it is missing.
    const Json& operator[](const std::string& key) const;
    const Json& operator[](size_t index) const { return items_.at(index); }
    bool has(const std::string& key) const;
    size_t size() const { return type_ == Type::object ? members_.size() : items_.size(); }
    const std::vector<Json>& items() const { return items_; }

    const std::string& str() const;
    bool boolean() const;
    int64_t i64() const;
    uint64_t u64() const;
    double dbl() const;

    /// Hex text as bytes.
    Bytes hex() const;
    std::vector<std::string> strings() const;
    std::vector<Bytes> hexes() const;

private:
    friend class JsonParser;
    Type type_ = Type::null;
    bool bool_ = false;
    std::string text_;
    std::vector<Json> items_;
    std::vector<std::pair<std::string, Json>> members_;
};

/// The test vectors generated from the Go bitfyn and btcd code, shared with
/// the macOS app (see its Fixtures/README.md).
const Json& fixture(const std::string& name);

/// The raw bytes of a fixture file.
Bytes fixtureData(const std::string& file);

/// Hex test data; it must be valid.
Bytes h(const std::string& hex);

/// The network of a name used in the fixtures, btcd's or the user's.
const Network& networkNamed(const std::string& name);

/// The BIP84 test vector's mnemonic.
inline const char* vectorMnemonic = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";

} // namespace tb::test

/// Checks the condition and fails the test with the message when it does
/// not hold.
#define EXPECT(cond, msg) QVERIFY2((cond), std::string(msg).c_str())
#define EXPECT_EQ(a, b, msg)                                                                                                                     \
    do {                                                                                                                                         \
        if (!((a) == (b))) QFAIL((std::string(msg) + ": " #a " != " #b).c_str());                                                                \
    } while (0)
