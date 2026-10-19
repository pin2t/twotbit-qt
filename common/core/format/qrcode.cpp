#include "format/qrcode.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdlib>

namespace tb {

namespace {

using Correction = QRCode::Correction;

// The error correction tables of ISO/IEC 18004 by level, L, M, Q and H, and
// version: the error correction codewords of each block and the blocks.
constexpr int eccPerBlockOf[4][41] = {
    {-1, 7,  10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28,
     28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
    {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26,
     26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28},
    {-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30,
     28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
    {-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28,
     30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
};
constexpr int eccBlocksOf[4][41] = {
    {-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, 8, 9, 9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25},
    {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49},
    {-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68},
    {-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81},
};

int eccPerBlock(Correction c, int version) { return eccPerBlockOf[int(c)][version]; }
int eccBlocks(Correction c, int version) { return eccBlocksOf[int(c)][version]; }

/// The two format bits of the level.
int formatBits(Correction c) {
    switch (c) {
    case Correction::L: return 1;
    case Correction::M: return 0;
    case Correction::Q: return 3;
    case Correction::H: return 2;
    }
    return 0;
}

/// The modules of the version that hold data and error correction, after
/// the function patterns.
int rawDataModules(int version) {
    int result = (16 * version + 128) * version + 64;
    if (version >= 2) {
        int align = version / 7 + 2;
        result -= (25 * align - 10) * align - 55;
        if (version >= 7) result -= 36;
    }
    return result;
}

int dataCodewords(Correction c, int version) { return rawDataModules(version) / 8 - eccPerBlock(c, version) * eccBlocks(c, version); }

std::vector<int> alignmentPositions(int version) {
    if (version == 1) return {};
    int align = version / 7 + 2;
    int size = version * 4 + 17;
    int step = (version * 8 + align * 3 + 5) / (align * 4 - 4) * 2;
    std::vector<int> result;
    for (int i = 0, pos = size - 7; i < align - 1; ++i, pos -= step) result.insert(result.begin(), pos);
    result.insert(result.begin(), 6);
    return result;
}

uint8_t gfMultiply(uint8_t x, uint8_t y) {
    int z = 0;
    for (int i = 7; i >= 0; --i) {
        z = (z << 1) ^ ((z >> 7) * 0x11d);
        z ^= ((y >> i) & 1) * x;
    }
    return uint8_t(z);
}

std::vector<uint8_t> reedSolomonDivisor(int degree) {
    std::vector<uint8_t> result(size_t(degree), 0);
    result.back() = 1;
    uint8_t root = 1;
    for (int i = 0; i < degree; ++i) {
        for (size_t j = 0; j < result.size(); ++j) {
            result[j] = gfMultiply(result[j], root);
            if (j + 1 < result.size()) result[j] ^= result[j + 1];
        }
        root = gfMultiply(root, 0x02);
    }
    return result;
}

std::vector<uint8_t> reedSolomonRemainder(const std::vector<uint8_t>& data, const std::vector<uint8_t>& divisor) {
    std::vector<uint8_t> result(divisor.size(), 0);
    for (uint8_t b : data) {
        uint8_t factor = b ^ result.front();
        result.erase(result.begin());
        result.push_back(0);
        for (size_t i = 0; i < result.size(); ++i) result[i] ^= gfMultiply(divisor[i], factor);
    }
    return result;
}

/// The data codewords followed by the error correction ones, split into the
/// version's blocks and interleaved.
std::vector<uint8_t> addECCAndInterleave(const std::vector<uint8_t>& data, Correction c, int version) {
    int numBlocks = eccBlocks(c, version);
    int blockECCLen = eccPerBlock(c, version);
    int rawCodewords = rawDataModules(version) / 8;
    int numShortBlocks = numBlocks - rawCodewords % numBlocks;
    int shortBlockLen = rawCodewords / numBlocks;
    std::vector<std::vector<uint8_t>> blocks;
    std::vector<uint8_t> divisor = reedSolomonDivisor(blockECCLen);
    for (int i = 0, k = 0; i < numBlocks; ++i) {
        int length = shortBlockLen - blockECCLen + (i < numShortBlocks ? 0 : 1);
        std::vector<uint8_t> block(data.begin() + k, data.begin() + k + length);
        k += length;
        std::vector<uint8_t> ecc = reedSolomonRemainder(block, divisor);
        if (i < numShortBlocks) block.push_back(0);
        block.insert(block.end(), ecc.begin(), ecc.end());
        blocks.push_back(std::move(block));
    }
    std::vector<uint8_t> result;
    for (size_t i = 0; i < blocks[0].size(); ++i)
        for (size_t j = 0; j < blocks.size(); ++j)
            if (i != size_t(shortBlockLen - blockECCLen) || j >= size_t(numShortBlocks)) result.push_back(blocks[j][i]);
    return result;
}

class BitBuffer {
public:
    void append(uint32_t value, int bits) {
        for (int i = bits - 1; i >= 0; --i) bits_.push_back(((value >> i) & 1) != 0);
    }
    size_t size() const { return bits_.size(); }
    std::vector<uint8_t> bytes() const {
        std::vector<uint8_t> out(bits_.size() / 8, 0);
        for (size_t i = 0; i < bits_.size(); ++i)
            if (bits_[i]) out[i >> 3] |= uint8_t(1 << (7 - (i & 7)));
        return out;
    }

private:
    std::vector<bool> bits_;
};

} // namespace

QRCode::QRCode(int version, Correction correction)
    : version_(version), size_(version * 4 + 17), correction_(correction), modules_(size_t(size_ * size_), false),
      function_(size_t(size_ * size_), false) {}

std::optional<QRCode> QRCode::encode(const std::string& text, Correction correction) {
    std::vector<uint8_t> bytes(text.begin(), text.end());
    int version = 1;
    for (;; ++version) {
        if (version > 40) return std::nullopt;
        int countBits = version <= 9 ? 8 : 16;
        if (bytes.size() >= (size_t(1) << countBits)) continue;
        size_t needed = 4 + size_t(countBits) + bytes.size() * 8;
        if (needed <= size_t(dataCodewords(correction, version)) * 8) break;
    }
    int countBits = version <= 9 ? 8 : 16;
    size_t capacity = size_t(dataCodewords(correction, version)) * 8;
    BitBuffer bb;
    bb.append(0x4, 4);
    bb.append(uint32_t(bytes.size()), countBits);
    for (uint8_t b : bytes) bb.append(b, 8);
    bb.append(0, int(std::min<size_t>(4, capacity - bb.size())));
    bb.append(0, int((8 - bb.size() % 8) % 8));
    for (uint8_t pad = 0xec; bb.size() < capacity; pad ^= 0xec ^ 0x11) bb.append(pad, 8);

    QRCode code(version, correction);
    code.drawFunctionPatterns();
    code.drawCodewords(addECCAndInterleave(bb.bytes(), correction, version));
    long best = LONG_MAX;
    int bestMask = 0;
    for (int mask = 0; mask < 8; ++mask) {
        code.applyMask(mask);
        code.drawFormatBits(mask);
        long p = code.penalty();
        if (p < best) {
            best = p;
            bestMask = mask;
        }
        code.applyMask(mask);
    }
    code.mask_ = bestMask;
    code.applyMask(bestMask);
    code.drawFormatBits(bestMask);
    code.function_.clear();
    return code;
}

void QRCode::setFunction(int x, int y, bool dark) {
    modules_[size_t(y * size_ + x)] = dark;
    function_[size_t(y * size_ + x)] = true;
}

void QRCode::drawFunctionPatterns() {
    for (int i = 0; i < size_; ++i) {
        setFunction(6, i, i % 2 == 0);
        setFunction(i, 6, i % 2 == 0);
    }
    drawFinderPattern(3, 3);
    drawFinderPattern(size_ - 4, 3);
    drawFinderPattern(3, size_ - 4);
    std::vector<int> positions = alignmentPositions(version_);
    size_t n = positions.size();
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            if ((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0)) continue;
            drawAlignmentPattern(positions[i], positions[j]);
        }
    }
    drawFormatBits(0);
    drawVersion();
}

void QRCode::drawFormatBits(int mask) {
    int data = formatBits(correction_) << 3 | mask;
    int rem = data;
    for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    int bits = (data << 10 | rem) ^ 0x5412;
    auto bit = [&](int i) { return ((bits >> i) & 1) != 0; };
    for (int i = 0; i <= 5; ++i) setFunction(8, i, bit(i));
    setFunction(8, 7, bit(6));
    setFunction(8, 8, bit(7));
    setFunction(7, 8, bit(8));
    for (int i = 9; i < 15; ++i) setFunction(14 - i, 8, bit(i));
    for (int i = 0; i < 8; ++i) setFunction(size_ - 1 - i, 8, bit(i));
    for (int i = 8; i < 15; ++i) setFunction(8, size_ - 15 + i, bit(i));
    setFunction(8, size_ - 8, true);
}

void QRCode::drawVersion() {
    if (version_ < 7) return;
    int rem = version_;
    for (int i = 0; i < 12; ++i) rem = (rem << 1) ^ ((rem >> 11) * 0x1f25);
    long bits = long(version_) << 12 | rem;
    for (int i = 0; i < 18; ++i) {
        bool bit = ((bits >> i) & 1) != 0;
        int a = size_ - 11 + i % 3;
        int b = i / 3;
        setFunction(a, b, bit);
        setFunction(b, a, bit);
    }
}

void QRCode::drawFinderPattern(int x, int y) {
    for (int dy = -4; dy <= 4; ++dy) {
        for (int dx = -4; dx <= 4; ++dx) {
            int dist = std::max(std::abs(dx), std::abs(dy));
            int xx = x + dx, yy = y + dy;
            if (xx >= 0 && xx < size_ && yy >= 0 && yy < size_) setFunction(xx, yy, dist != 2 && dist != 4);
        }
    }
}

void QRCode::drawAlignmentPattern(int x, int y) {
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) setFunction(x + dx, y + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
}

void QRCode::drawCodewords(const std::vector<uint8_t>& data) {
    size_t i = 0;
    for (int right = size_ - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int vert = 0; vert < size_; ++vert) {
            for (int j = 0; j < 2; ++j) {
                int x = right - j;
                bool upward = ((right + 1) & 2) == 0;
                int y = upward ? size_ - 1 - vert : vert;
                size_t at = size_t(y * size_ + x);
                if (!function_[at] && i < data.size() * 8) {
                    modules_[at] = ((data[i >> 3] >> (7 - (i & 7))) & 1) != 0;
                    ++i;
                }
            }
        }
    }
}

void QRCode::applyMask(int mask) {
    for (int y = 0; y < size_; ++y) {
        for (int x = 0; x < size_; ++x) {
            bool invert;
            switch (mask) {
            case 0: invert = (x + y) % 2 == 0; break;
            case 1: invert = y % 2 == 0; break;
            case 2: invert = x % 3 == 0; break;
            case 3: invert = (x + y) % 3 == 0; break;
            case 4: invert = (x / 3 + y / 2) % 2 == 0; break;
            case 5: invert = x * y % 2 + x * y % 3 == 0; break;
            case 6: invert = (x * y % 2 + x * y % 3) % 2 == 0; break;
            default: invert = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
            }
            size_t at = size_t(y * size_ + x);
            if (invert && !function_[at]) modules_[at] = !modules_[at];
        }
    }
}

long QRCode::penalty() const {
    const long n1 = 3, n2 = 3, n3 = 40, n4 = 10;
    long result = 0;
    auto addHistory = [&](int run, std::array<int, 7>& history) {
        if (history[0] == 0) run += size_;
        std::copy_backward(history.begin(), history.end() - 1, history.end());
        history[0] = run;
    };
    auto countPatterns = [&](const std::array<int, 7>& h) {
        int n = h[1];
        bool core = n > 0 && h[2] == n && h[3] == n * 3 && h[4] == n && h[5] == n;
        return (core && h[0] >= n * 4 && h[6] >= n ? 1 : 0) + (core && h[6] >= n * 4 && h[0] >= n ? 1 : 0);
    };
    auto terminate = [&](bool color, int run, std::array<int, 7>& history) {
        if (color) {
            addHistory(run, history);
            run = 0;
        }
        run += size_;
        addHistory(run, history);
        return countPatterns(history);
    };
    for (int pass = 0; pass < 2; ++pass) {
        for (int a = 0; a < size_; ++a) {
            bool runColor = false;
            int run = 0;
            std::array<int, 7> history{};
            for (int b = 0; b < size_; ++b) {
                bool dark = pass == 0 ? isDark(b, a) : isDark(a, b);
                if (dark == runColor) {
                    ++run;
                    if (run == 5)
                        result += n1;
                    else if (run > 5)
                        ++result;
                } else {
                    addHistory(run, history);
                    if (!runColor) result += countPatterns(history) * n3;
                    runColor = dark;
                    run = 1;
                }
            }
            result += terminate(runColor, run, history) * n3;
        }
    }
    for (int y = 0; y < size_ - 1; ++y) {
        for (int x = 0; x < size_ - 1; ++x) {
            bool c = isDark(x, y);
            if (c == isDark(x + 1, y) && c == isDark(x, y + 1) && c == isDark(x + 1, y + 1)) result += n2;
        }
    }
    long dark = 0;
    for (bool m : modules_) dark += m ? 1 : 0;
    long total = long(size_) * size_;
    long k = (std::labs(dark * 20 - total * 10) + total - 1) / total - 1;
    result += k * n4;
    return result;
}

QRCode::Range QRCode::center(double fraction) const {
    int n = int(std::lround(size_ * fraction));
    if ((size_ - n) % 2 != 0) ++n;
    int start = (size_ - n) / 2;
    return {start, start + n};
}

bool QRCode::isFinder(int x, int y) const {
    for (int dy = 0; dy < 7; ++dy) {
        for (int dx = 0; dx < 7; ++dx) {
            int ring = std::min({dx, dy, 6 - dx, 6 - dy});
            if (isDark(x + dx, y + dy) != (ring != 1)) return false;
        }
    }
    return true;
}

bool QRCode::isUpright() const {
    return isFinder(0, 0) && isFinder(size_ - 7, 0) && isFinder(0, size_ - 7) && !isFinder(size_ - 7, size_ - 7);
}

} // namespace tb
