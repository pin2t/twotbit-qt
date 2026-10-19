#include "theme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QFontDatabase>
#include <QPainter>
#include <QPixmap>
#include <QStyleHints>
#include <QTimer>
#include <QWidget>

namespace Theme {

namespace {

void applyAccent(QApplication& app) {
    QPalette palette = app.palette();
    QColor color = accent();
    palette.setColor(QPalette::Accent, color);
    palette.setColor(QPalette::Highlight, color);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, color);
    app.setPalette(palette);
}

QColor mix(const QColor& base, const QColor& over, double amount) {
    return QColor::fromRgbF(float(base.redF() * (1 - amount) + over.redF() * amount), float(base.greenF() * (1 - amount) + over.greenF() * amount),
                            float(base.blueF() * (1 - amount) + over.blueF() * amount));
}

} // namespace

void install(QApplication& app) {
    applyAccent(app);
    QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged, &app, [&app] {
        // The style updates the palette for the new scheme first.
        QTimer::singleShot(0, &app, [&app] {
            applyAccent(app);
            refresh();
        });
    });
}

bool dark() { return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark; }

QColor accent() { return dark() ? QColor(0xfa, 0xa0, 0x2e) : QColor(0xf7, 0x93, 0x1a); }

QColor secondaryText(const QPalette& palette) {
    return mix(palette.color(QPalette::Window), palette.color(QPalette::WindowText), dark() ? 0.62 : 0.58);
}

QColor card(const QPalette& palette) {
    return dark() ? mix(palette.color(QPalette::Window), Qt::white, 0.06) : mix(palette.color(QPalette::Window), Qt::white, 0.75);
}

QColor separator(const QPalette& palette) { return mix(palette.color(QPalette::Window), palette.color(QPalette::WindowText), dark() ? 0.16 : 0.12); }

QColor success() { return dark() ? QColor(0x6c, 0xcb, 0x5f) : QColor(0x0f, 0x7b, 0x0f); }

QColor danger() { return dark() ? QColor(0xff, 0x99, 0xa4) : QColor(0xc4, 0x2b, 0x1c); }

QFont mono(double scale) {
    static const QString family = [] {
#ifndef Q_OS_MACOS
        for (const char* f : {"Cascadia Mono", "Consolas", "Courier New"})
            if (QFontDatabase::hasFamily(f)) return QString(f);
#endif
        // The Mac's system monospaced font, SF Mono.
        return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    }();
    QFont f(family);
    f.setStyleHint(QFont::Monospace);
    f.setPointSizeF(QApplication::font().pointSizeF() * scale);
    return f;
}

QFont font(double scale, QFont::Weight weight) {
    QFont f = QApplication::font();
    f.setPointSizeF(f.pointSizeF() * scale);
    f.setWeight(weight);
    return f;
}

QString iconFamily() {
    static const QString family = QFontDatabase::hasFamily("Segoe Fluent Icons") ? QString("Segoe Fluent Icons") : QString("Segoe MDL2 Assets");
    return family;
}

QChar glyph(Glyph g) {
    switch (g) {
    case Glyph::home: return QChar(0xE80F);
    case Glyph::coins: return QChar(0xECCB);
    case Glyph::transactions: return QChar(0xE81C);
    case Glyph::settings: return QChar(0xE713);
    case Glyph::copy: return QChar(0xE8C8);
    case Glyph::receive: return QChar(0xE896);
    case Glyph::send: return QChar(0xE898);
    case Glyph::merge: return QChar(0xEA3C);
    case Glyph::split: return QChar(0xEA3D);
    case Glyph::arrowRight: return QChar(0xE72A);
    case Glyph::warning: return QChar(0xE7BA);
    case Glyph::warningFilled: return QChar(0xE814);
    case Glyph::checkFilled: return QChar(0xEC61);
    case Glyph::key: return QChar(0xE8D7);
    case Glyph::sortUp: return QChar(0xEDDB);
    case Glyph::sortDown: return QChar(0xEDDC);
    }
    return QChar(' ');
}

#ifdef Q_OS_MACOS
QString symbolName(Glyph g) {
    switch (g) {
    case Glyph::home: return "house";
    case Glyph::coins: return "circle.circle";
    case Glyph::transactions: return "clock.arrow.circlepath";
    case Glyph::settings: return "gearshape";
    case Glyph::copy: return "doc.on.doc";
    case Glyph::receive: return "arrow.down.to.line";
    case Glyph::send: return "arrow.up.to.line";
    case Glyph::merge: return "arrow.triangle.merge";
    case Glyph::split: return "arrow.triangle.branch";
    case Glyph::arrowRight: return "arrow.right";
    case Glyph::warning: return "exclamationmark.triangle";
    case Glyph::warningFilled: return "exclamationmark.triangle.fill";
    case Glyph::checkFilled: return "checkmark.circle.fill";
    case Glyph::key: return "key.slash";
    case Glyph::sortUp: return "arrowtriangle.up.fill";
    case Glyph::sortDown: return "arrowtriangle.down.fill";
    }
    return {};
}
#endif

QPixmap glyphPixmap(Glyph g, const QColor& color, int size, double ratio) {
    int pixels = int(size * ratio + 0.5);
    QColor ink = color.isValid() ? color : QApplication::palette().color(QPalette::WindowText);
#ifdef Q_OS_MACOS
    // The symbol's shape, in the color.
    QImage image = symbol(symbolName(g), pixels);
    if (image.isNull()) {
        image = QImage(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
    }
    QPainter p(&image);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(image.rect(), ink);
    p.end();
    QPixmap pixmap = QPixmap::fromImage(image);
#else
    QPixmap pixmap(pixels, pixels);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont f(iconFamily());
    f.setPixelSize(pixels);
    p.setFont(f);
    p.setPen(ink);
    p.drawText(QRect(0, 0, pixels, pixels), Qt::AlignCenter, QString(glyph(g)));
    p.end();
#endif
    pixmap.setDevicePixelRatio(ratio);
    return pixmap;
}

QIcon icon(Glyph g, const QColor& color, int size) {
    QIcon result;
    for (double ratio : {1.0, 1.25, 1.5, 2.0}) result.addPixmap(glyphPixmap(g, color, size, ratio));
    return result;
}

namespace {

QColor toneColor(Tone tone) {
    QPalette app = QApplication::palette();
    switch (tone) {
    case Tone::secondary: return secondaryText(app);
    case Tone::success: return success();
    case Tone::danger: return danger();
    default: return app.color(QPalette::WindowText);
    }
}

void applyTone(QWidget* widget, Tone tone) {
    QColor c = toneColor(tone);
    QPalette p = widget->palette();
    p.setColor(QPalette::WindowText, c);
    p.setColor(QPalette::Text, c);
    p.setColor(QPalette::ButtonText, c);
    widget->setPalette(p);
}

void applyGlyph(QAbstractButton* button) {
    Glyph g = Glyph(button->property("glyph").toInt());
    int size = button->property("glyphSize").toInt();
    Tone tone = Tone(button->property("glyphTone").toInt());
    button->setIcon(icon(g, toneColor(tone), size));
}

} // namespace

void setTone(QWidget* widget, Tone tone) {
    widget->setProperty("tone", int(tone));
    applyTone(widget, tone);
}

void setGlyphIcon(QAbstractButton* button, Glyph glyph, int size, Tone tone) {
    button->setProperty("glyph", int(glyph));
    button->setProperty("glyphSize", size);
    button->setProperty("glyphTone", int(tone));
    applyGlyph(button);
}

void refresh() {
    for (QWidget* w : QApplication::allWidgets()) {
        QVariant tone = w->property("tone");
        if (tone.isValid()) applyTone(w, Tone(tone.toInt()));
        if (auto* b = qobject_cast<QAbstractButton*>(w); b && b->property("glyph").isValid()) applyGlyph(b);
        w->update();
    }
    emit notifier()->changed();
}

Notifier* notifier() {
    static Notifier* n = new Notifier;
    return n;
}

} // namespace Theme
