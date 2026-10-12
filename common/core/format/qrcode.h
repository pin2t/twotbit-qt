#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tb {

/// QRCode is the module matrix of the QR code of a text at error correction
/// level M, as bitfyn's receive codes use: the text's UTF-8 bytes in byte
/// mode, in the smallest version that holds them, with the mask of the
/// lowest penalty per ISO/IEC 18004.
class QRCode {
public:
    /// The code of the text, nothing when it is too long for any version.
    static std::optional<QRCode> encode(const std::string& text);

    /// The number of modules on a side.
    int size() const { return size_; }
    int version() const { return version_; }
    int mask() const { return mask_; }

    bool isDark(int x, int y) const { return modules_[size_t(y * size_ + x)]; }

    /// Whether the code stands upright: finder patterns at the top left, top
    /// right and bottom left corners and none at the bottom right.
    bool isUpright() const;

    friend bool operator==(const QRCode&, const QRCode&) = default;

private:
    QRCode(int version);

    void drawFunctionPatterns();
    void drawFormatBits(int mask);
    void drawVersion();
    void drawFinderPattern(int x, int y);
    void drawAlignmentPattern(int x, int y);
    void setFunction(int x, int y, bool dark);
    void drawCodewords(const std::vector<uint8_t>& data);
    void applyMask(int mask);
    long penalty() const;
    bool isFinder(int x, int y) const;

    int version_ = 1;
    int size_ = 21;
    int mask_ = 0;
    std::vector<bool> modules_;
    std::vector<bool> function_;
};

} // namespace tb
