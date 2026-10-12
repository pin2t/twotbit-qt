#pragma once

#include <optional>
#include <string>
#include <vector>

#include "util/bytes.h"

namespace tb {

/// BlockError is a block that is not the one the header chain committed to.
class BlockError : public Error {
public:
    explicit BlockError(const std::string& what) : Error(what) {}
};

/// BlockHeader is the 80-byte header of a block.
struct BlockHeader {
    int32_t version = 0;
    Hash256 prevBlock;
    Hash256 merkleRoot;
    uint32_t timestamp = 0;
    uint32_t bits = 0;
    uint32_t nonce = 0;

    static BlockHeader read(ByteReader& reader);
    static BlockHeader parse(ByteView data);
    void write(ByteWriter& writer) const;
    Bytes serialized() const;

    /// The block hash: the double SHA256 of the serialized header.
    Hash256 hash() const;

    friend bool operator==(const BlockHeader&, const BlockHeader&) = default;
};

/// OutPoint names one transaction output: the id of its transaction and its
/// index there.
struct OutPoint {
    Hash256 hash;
    uint32_t index = 0;

    /// The previous output of a coinbase input.
    static OutPoint coinbase() { return {Hash256(), 0xffffffff}; }
    bool isCoinbase() const { return index == 0xffffffff && hash.isZero(); }

    /// "txid:index", as btcd writes outpoints.
    std::string description() const { return hash.hex() + ":" + std::to_string(index); }

    /// Parses the "txid:index" form.
    static std::optional<OutPoint> parse(const std::string& text);

    void write(ByteWriter& writer) const {
        writer.write(hash);
        writer.writeUInt32(index);
    }

    Bytes serialized() const {
        ByteWriter w(36);
        write(w);
        return w.take();
    }

    friend bool operator==(const OutPoint&, const OutPoint&) = default;
};

struct OutPointHasher {
    size_t operator()(const OutPoint& o) const { return size_t(o.hash.word(0) ^ (uint64_t(o.index) * 0x9e3779b97f4a7c15ull)); }
};

struct TxIn {
    OutPoint previousOutPoint;
    Bytes signatureScript;
    std::vector<Bytes> witness;
    uint32_t sequence = 0xffffffff;

    friend bool operator==(const TxIn&, const TxIn&) = default;
};

struct TxOut {
    int64_t value = 0;
    Bytes pkScript;

    void write(ByteWriter& writer) const {
        writer.writeInt64(value);
        writer.writeVarBytes(pkScript);
    }

    friend bool operator==(const TxOut&, const TxOut&) = default;
};

/// Transaction is a Bitcoin transaction, read and written with its witness
/// data in the BIP144 encoding when it has any.
struct Transaction {
    int32_t version = 2;
    std::vector<TxIn> inputs;
    std::vector<TxOut> outputs;
    uint32_t lockTime = 0;

    /// Reads a transaction that must span the whole data.
    static Transaction parse(ByteView data);

    /// Reads a transaction in either encoding: a zero input count followed
    /// by the flag byte 1 marks the witness encoding.
    static Transaction read(ByteReader& reader);

    /// Whether any input carries witness data.
    bool hasWitness() const;

    void write(ByteWriter& writer, bool witness) const;

    /// The serialized transaction, with its witness data unless witness is
    /// false.
    Bytes serialized(bool witness = true) const;

    /// The transaction id: the hash of the serialization without witness.
    Hash256 txid() const;

    /// The witness transaction id, which commits to the witness data too.
    Hash256 wtxid() const;

    bool isCoinbase() const { return inputs.size() == 1 && inputs[0].previousOutPoint.isCoinbase(); }

    /// The weight per BIP141: the stripped size three times plus the full
    /// size.
    size_t weight() const { return serialized(false).size() * 3 + serialized(true).size(); }

    /// The virtual size in vbytes: the weight divided by four, rounded up.
    size_t vsize() const { return (weight() + 3) / 4; }

    /// Sorts the inputs and outputs per BIP69: inputs by previous
    /// transaction id, as displayed, then output index; outputs by value,
    /// then script bytes.
    void sortBIP69();

    friend bool operator==(const Transaction&, const Transaction&) = default;
};

/// Block is a full block: its header and its transactions.
struct Block {
    BlockHeader header;
    std::vector<Transaction> transactions;

    static Block parse(ByteView data);
    static Block read(ByteReader& reader);

    Hash256 hash() const { return header.hash(); }
    Bytes serialized() const;

    /// The merkle root of the hashes: pairs hashed together level by level,
    /// the last hash of an odd level paired with itself.
    static Hash256 merkleRoot(std::vector<Hash256> hashes);

    /// The witness commitment prefix of a coinbase output: OP_RETURN, a
    /// 36-byte push and the BIP141 magic.
    static const Bytes& witnessMagic();

    /// Verifies that the block is the one the header chain committed to:
    /// its header hashes to the expected hash, its transactions hash to the
    /// merkle root without duplicates, and its witness data matches the
    /// coinbase witness commitment, as btcd's ValidateWitnessCommitment
    /// checks it.
    void verify(const Hash256& expected) const;

    void verifyWitnessCommitment() const;
};

} // namespace tb
