#pragma once

#include <optional>
#include <string>
#include <vector>

#include "bitcoin/network.h"
#include "util/bytes.h"

namespace tb {

/// Script builds and inspects the standard output and input scripts.
namespace Script {

constexpr uint8_t opDup = 0x76;
constexpr uint8_t opHash160 = 0xa9;
constexpr uint8_t opEqual = 0x87;
constexpr uint8_t opEqualVerify = 0x88;
constexpr uint8_t opCheckSig = 0xac;
constexpr uint8_t opReturn = 0x6a;
constexpr uint8_t op1 = 0x51;

/// P2WPKH: OP_0 <20-byte key hash>.
Bytes p2wpkh(ByteView hash);

/// P2WPKH paying the compressed public key.
Bytes p2wpkhOfPubkey(ByteView pubkey);

/// P2PKH: OP_DUP OP_HASH160 <20-byte key hash> OP_EQUALVERIFY OP_CHECKSIG.
Bytes p2pkh(ByteView hash);

/// P2SH: OP_HASH160 <20-byte script hash> OP_EQUAL.
Bytes p2sh(ByteView hash);

/// A witness program: the version as OP_0 or OP_1 to OP_16 and the program
/// pushed.
Bytes witnessProgram(uint8_t version, ByteView program);

/// P2PK: <public key> OP_CHECKSIG.
Bytes p2pk(ByteView pubkey);

/// Whether the script is a witness program: a version opcode followed by
/// one canonical push of 2 to 40 bytes and nothing else.
bool isWitnessProgram(ByteView script);

/// The data every push of the script pushes, OP_0 included as empty data
/// and OP_1 to OP_16 not, as btcd's PushedData returns it. A push running
/// past the end of the script is an error.
std::vector<Bytes> pushedData(ByteView script);

/// Whether the data is a compressed or uncompressed public key by its
/// length and prefix.
bool isPubKey(ByteView data);

/// Whether the data is shaped like the control block ending a taproot
/// script path witness.
bool isControlBlock(ByteView data);

} // namespace Script

/// AddressError is a destination address that cannot be paid on the
/// network.
class AddressError : public Error {
public:
    explicit AddressError(const std::string& what) : Error(what) {}
};

/// Address encodes and decodes Bitcoin addresses: Base58Check P2PKH and
/// P2SH, Bech32 segwit version 0 and Bech32m taproot.
namespace Address {

/// Decodes an address of the network into the output script paying it,
/// accepting what btcutil's DecodeAddress accepts for the network: P2PKH,
/// P2SH, P2WPKH, P2WSH, P2TR and hex public keys. Surrounding whitespace is
/// ignored.
Bytes script(const std::string& text, const Network& network);

/// Whether the text is an address payable on the network.
bool isValid(const std::string& text, const Network& network);

std::string p2pkh(ByteView hash, const Network& network);
std::string p2sh(ByteView hash, const Network& network);
std::string segwit(uint8_t version, ByteView program, const Network& network);

/// The P2WPKH address of a compressed public key, as the wallet's BIP84
/// addresses are.
std::string p2wpkh(ByteView pubkey, const Network& network);

/// The single address an output script pays, as btcd's
/// ExtractPkScriptAddrs finds it, or nothing for a script without one, such
/// as OP_RETURN data or bare multisig.
std::optional<std::string> of(ByteView script, const Network& network);

} // namespace Address

} // namespace tb
