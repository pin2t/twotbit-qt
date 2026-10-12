#include "bitcoin/primitives.h"

#include <algorithm>
#include <unordered_set>

#include "crypto/hashes.h"
#include "util/strings.h"

namespace tb {

BlockHeader BlockHeader::read(ByteReader& r) {
    BlockHeader h;
    h.version = r.readInt32();
    h.prevBlock = r.readHash();
    h.merkleRoot = r.readHash();
    h.timestamp = r.readUInt32();
    h.bits = r.readUInt32();
    h.nonce = r.readUInt32();
    return h;
}

BlockHeader BlockHeader::parse(ByteView data) {
    ByteReader r(data);
    return read(r);
}

void BlockHeader::write(ByteWriter& w) const {
    w.writeInt32(version);
    w.write(prevBlock);
    w.write(merkleRoot);
    w.writeUInt32(timestamp);
    w.writeUInt32(bits);
    w.writeUInt32(nonce);
}

Bytes BlockHeader::serialized() const {
    ByteWriter w(80);
    write(w);
    return w.take();
}

Hash256 BlockHeader::hash() const {
    uint8_t buf[80];
    auto put32 = [&](size_t at, uint32_t v) {
        for (int i = 0; i < 4; ++i) buf[at + size_t(i)] = uint8_t(v >> (8 * i));
    };
    put32(0, uint32_t(version));
    std::copy(prevBlock.bytes.begin(), prevBlock.bytes.end(), buf + 4);
    std::copy(merkleRoot.bytes.begin(), merkleRoot.bytes.end(), buf + 36);
    put32(68, timestamp);
    put32(72, bits);
    put32(76, nonce);
    return Hashes::sha256d(ByteView(buf, 80));
}

std::optional<OutPoint> OutPoint::parse(const std::string& text) {
    auto parts = split(text, ':');
    if (parts.size() != 2) return std::nullopt;
    auto hash = Hash256::fromHex(parts[0]);
    uint32_t index;
    if (!hash || !parseUnsigned(parts[1], index)) return std::nullopt;
    return OutPoint{*hash, index};
}

Transaction Transaction::parse(ByteView data) {
    ByteReader r(data);
    Transaction tx = read(r);
    if (!r.isAtEnd()) throw DecodeError("trailing data after transaction");
    return tx;
}

Transaction Transaction::read(ByteReader& r) {
    Transaction tx;
    tx.version = r.readInt32();
    size_t count = r.readCount(r.remaining() / 41 + 1);
    bool witness = false;
    if (count == 0) {
        uint8_t flag = r.readUInt8();
        if (flag != 0x01) throw DecodeError("witness flag " + std::to_string(flag) + ", want 1");
        witness = true;
        count = r.readCount(r.remaining() / 41 + 1);
    }
    tx.inputs.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        TxIn in;
        in.previousOutPoint.hash = r.readHash();
        in.previousOutPoint.index = r.readUInt32();
        in.signatureScript = r.readVarBytes(10'000'000);
        in.sequence = r.readUInt32();
        tx.inputs.push_back(std::move(in));
    }
    size_t outCount = r.readCount(r.remaining() / 9 + 1);
    tx.outputs.reserve(outCount);
    for (size_t i = 0; i < outCount; ++i) {
        TxOut out;
        out.value = r.readInt64();
        out.pkScript = r.readVarBytes(10'000'000);
        tx.outputs.push_back(std::move(out));
    }
    if (witness) {
        for (auto& in : tx.inputs) {
            size_t items = r.readCount(r.remaining() + 1);
            in.witness.reserve(items);
            for (size_t i = 0; i < items; ++i) in.witness.push_back(r.readVarBytes(10'000'000));
        }
    }
    tx.lockTime = r.readUInt32();
    return tx;
}

bool Transaction::hasWitness() const {
    return std::any_of(inputs.begin(), inputs.end(), [](const TxIn& in) { return !in.witness.empty(); });
}

void Transaction::write(ByteWriter& w, bool witness) const {
    bool withWitness = witness && hasWitness();
    w.writeInt32(version);
    if (withWitness) {
        w.writeUInt8(0x00);
        w.writeUInt8(0x01);
    }
    w.writeVarInt(inputs.size());
    for (const auto& in : inputs) {
        in.previousOutPoint.write(w);
        w.writeVarBytes(in.signatureScript);
        w.writeUInt32(in.sequence);
    }
    w.writeVarInt(outputs.size());
    for (const auto& out : outputs) out.write(w);
    if (withWitness) {
        for (const auto& in : inputs) {
            w.writeVarInt(in.witness.size());
            for (const auto& item : in.witness) w.writeVarBytes(item);
        }
    }
    w.writeUInt32(lockTime);
}

Bytes Transaction::serialized(bool witness) const {
    ByteWriter w(256);
    write(w, witness);
    return w.take();
}

Hash256 Transaction::txid() const { return Hashes::sha256d(serialized(false)); }

Hash256 Transaction::wtxid() const { return Hashes::sha256d(serialized(true)); }

void Transaction::sortBIP69() {
    std::sort(inputs.begin(), inputs.end(), [](const TxIn& a, const TxIn& b) {
        if (a.previousOutPoint.hash != b.previousOutPoint.hash) return a.previousOutPoint.hash < b.previousOutPoint.hash;
        return a.previousOutPoint.index < b.previousOutPoint.index;
    });
    std::sort(outputs.begin(), outputs.end(), [](const TxOut& a, const TxOut& b) {
        if (a.value != b.value) return a.value < b.value;
        return std::lexicographical_compare(a.pkScript.begin(), a.pkScript.end(), b.pkScript.begin(), b.pkScript.end());
    });
}

Block Block::parse(ByteView data) {
    ByteReader r(data);
    return read(r);
}

Block Block::read(ByteReader& r) {
    Block b;
    b.header = BlockHeader::read(r);
    size_t count = r.readCount(r.remaining() / 60 + 1);
    b.transactions.reserve(count);
    for (size_t i = 0; i < count; ++i) b.transactions.push_back(Transaction::read(r));
    return b;
}

Bytes Block::serialized() const {
    ByteWriter w(1024);
    header.write(w);
    w.writeVarInt(transactions.size());
    for (const auto& tx : transactions) tx.write(w, true);
    return w.take();
}

Hash256 Block::merkleRoot(std::vector<Hash256> level) {
    if (level.empty()) return Hash256();
    uint8_t buffer[64];
    while (level.size() > 1) {
        std::vector<Hash256> next;
        next.reserve((level.size() + 1) / 2);
        for (size_t i = 0; i < level.size(); i += 2) {
            const Hash256& left = level[i];
            const Hash256& right = i + 1 < level.size() ? level[i + 1] : left;
            std::copy(left.bytes.begin(), left.bytes.end(), buffer);
            std::copy(right.bytes.begin(), right.bytes.end(), buffer + 32);
            next.push_back(Hashes::sha256d(ByteView(buffer, 64)));
        }
        level = std::move(next);
    }
    return level[0];
}

const Bytes& Block::witnessMagic() {
    static const Bytes magic{0x6a, 0x24, 0xaa, 0x21, 0xa9, 0xed};
    return magic;
}

void Block::verify(const Hash256& expected) const {
    Hash256 got = hash();
    if (got != expected) throw BlockError("block hash " + got.hex() + ", want " + expected.hex());
    if (transactions.empty()) throw BlockError("block has no transactions");
    std::unordered_set<Hash256, Hash256Hasher> seen;
    std::vector<Hash256> txids;
    txids.reserve(transactions.size());
    for (const auto& tx : transactions) {
        Hash256 id = tx.txid();
        if (!seen.insert(id).second) throw BlockError("duplicate transaction " + id.hex());
        txids.push_back(id);
    }
    Hash256 root = merkleRoot(txids);
    if (root != header.merkleRoot) throw BlockError("merkle root " + root.hex() + ", header commits to " + header.merkleRoot.hex());
    verifyWitnessCommitment();
}

void Block::verifyWitnessCommitment() const {
    const Transaction& coinbase = transactions[0];
    if (coinbase.inputs.empty()) throw BlockError("witness commitment: transaction has no inputs");
    std::optional<Bytes> commitment;
    if (coinbase.isCoinbase()) {
        const Bytes& magic = witnessMagic();
        for (auto it = coinbase.outputs.rbegin(); it != coinbase.outputs.rend(); ++it) {
            const Bytes& s = it->pkScript;
            if (s.size() >= 38 && std::equal(magic.begin(), magic.end(), s.begin())) {
                commitment = Bytes(s.begin() + 6, s.begin() + 38);
                break;
            }
        }
    }
    if (!commitment) {
        bool anyWitness = std::any_of(transactions.begin(), transactions.end(), [](const Transaction& tx) { return tx.hasWitness(); });
        if (anyWitness)
            throw BlockError("witness commitment: block contains transaction with witness data, yet no witness commitment present");
        return;
    }
    const auto& witness = coinbase.inputs[0].witness;
    if (witness.size() != 1)
        throw BlockError("witness commitment: the coinbase transaction has " + std::to_string(witness.size()) +
                         " items in its witness stack when only one is allowed");
    if (witness[0].size() != 32)
        throw BlockError("witness commitment: the coinbase witness nonce has " + std::to_string(witness[0].size()) +
                         " bytes when it must be 32");
    std::vector<Hash256> wtxids;
    wtxids.reserve(transactions.size());
    for (const auto& tx : transactions) wtxids.push_back(tx.wtxid());
    wtxids[0] = Hash256();
    Bytes preimage = merkleRoot(wtxids).data();
    append(preimage, witness[0]);
    Hash256 got = Hashes::sha256d(preimage);
    if (!std::equal(got.bytes.begin(), got.bytes.end(), commitment->begin())) throw BlockError("witness commitment does not match");
}

} // namespace tb
