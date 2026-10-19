#include "theme.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <locale.h>
#include <xlocale.h>

namespace Theme {

namespace {

/// CLocale switches the thread to the C locale while it lives: AppKit
/// measures symbols wrongly, as a few points across, under a locale whose
/// decimal separator is a comma, and Qt takes the user's locale.
class CLocale {
public:
    CLocale() : c_(newlocale(LC_ALL_MASK, "C", nullptr)), previous_(c_ ? uselocale(c_) : nullptr) {}
    ~CLocale() {
        if (!c_) return;
        uselocale(previous_);
        freelocale(c_);
    }
    CLocale(const CLocale&) = delete;
    CLocale& operator=(const CLocale&) = delete;

private:
    locale_t c_;
    locale_t previous_;
};

} // namespace

// By default the symbol at a point size that makes it about as large as a
// Segoe icon glyph of the same pixel size, scaled down further when it would
// not fit.
QImage symbol(const QString& name, int pixels, double points, bool bold) {
    if (pixels <= 0) return {};
    if (points <= 0) points = pixels * 0.7;
    CLocale c;
    @autoreleasepool {
        NSImage* image = [NSImage imageWithSystemSymbolName:name.toNSString() accessibilityDescription:nil];
        if (!image) return {};
        NSImageSymbolConfiguration* configuration = [NSImageSymbolConfiguration configurationWithPointSize:points
                                                                                                   weight:bold ? NSFontWeightBold : NSFontWeightRegular
                                                                                                    scale:NSImageSymbolScaleMedium];
        image = [image imageWithSymbolConfiguration:configuration];
        NSSize size = image.size;
        if (!image || size.width <= 0 || size.height <= 0) return {};
        double fit = std::min({1.0, pixels / size.width, pixels / size.height});
        NSRect target = NSMakeRect((pixels - size.width * fit) / 2, (pixels - size.height * fit) / 2, size.width * fit, size.height * fit);

        QImage out(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::transparent);
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        CGContextRef context = CGBitmapContextCreate(out.bits(), size_t(pixels), size_t(pixels), 8, size_t(out.bytesPerLine()), space,
                                                     kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
        CGColorSpaceRelease(space);
        if (!context) return {};
        // Rasterized at the size it is drawn at, then drawn on its own: an
        // NSGraphicsContext over the bitmap draws nothing in a Qt app.
        NSRect proposed = NSMakeRect(0, 0, target.size.width, target.size.height);
        NSDictionary* hints = @{NSImageHintCTM : [NSAffineTransform transform]};
        CGImageRef raster = [image CGImageForProposedRect:&proposed context:nil hints:hints];
        if (raster) CGContextDrawImage(context, NSRectToCGRect(target), raster);
        CGContextRelease(context);
        return raster ? out : QImage();
    }
}

} // namespace Theme
