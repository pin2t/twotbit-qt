#pragma once

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QImage>
#include <QObject>
#include <QPalette>
#include <QPixmap>
#include <QString>

class QAbstractButton;
class QApplication;
class QWidget;

/// Glyph names the icons the window uses, drawn as the platform's own apps
/// draw theirs: from the Segoe Fluent Icons or Segoe MDL2 Assets font on
/// Windows, and as the SF Symbols the Swift app shows on the Mac.
enum class Glyph { home, coins, transactions, settings, copy, receive, send, merge, split, arrowRight, warning, warningFilled, checkFilled, key, sortUp, sortDown };

/// Theme is the look of the window in light and dark mode: the Bitcoin
/// orange accent, the secondary, card, separator and status colors, the
/// monospaced font and the icons.
namespace Theme {

/// Sets the accent and keeps it, light or dark, as the color scheme
/// changes.
void install(QApplication& app);

bool dark();

QColor accent();
QColor secondaryText(const QPalette& palette);
QColor card(const QPalette& palette);
QColor separator(const QPalette& palette);
QColor success();
QColor danger();

/// The monospaced font, at the app font's size times the scale.
QFont mono(double scale = 1.0);

/// The app font scaled, at the weight when one is given.
QFont font(double scale, QFont::Weight weight = QFont::Normal);

/// The family of the icon font, and the glyph's character in it.
QString iconFamily();
QChar glyph(Glyph g);

#ifdef Q_OS_MACOS
/// The name of the glyph's SF Symbol.
QString symbolName(Glyph g);

/// The SF Symbol drawn in black on a transparent square of the pixels,
/// centered, by macos/app/symbols.mm; null when the system has no such
/// symbol.
QImage symbol(const QString& name, int pixels);
#endif

/// The glyph as an icon in the color, the window text's by default.
QIcon icon(Glyph g, const QColor& color = QColor(), int size = 16);

/// The glyph drawn size pixels square at the device pixel ratio.
QPixmap glyphPixmap(Glyph g, const QColor& color, int size, double ratio);

/// Tone is the color a widget's text takes, kept as the color scheme
/// changes.
enum class Tone { normal, secondary, success, danger };

void setTone(QWidget* widget, Tone tone);

/// Shows the widget's text in the secondary color.
inline void setSecondary(QWidget* widget) { setTone(widget, Tone::secondary); }

/// Gives the button the glyph's icon in the tone, drawn again as the color
/// scheme changes.
void setGlyphIcon(QAbstractButton* button, Glyph glyph, int size = 16, Tone tone = Tone::normal);

/// Applies the tones and glyph icons again and repaints every widget, as
/// after the color scheme changed.
void refresh();

/// Notifier announces that the color scheme changed and the theme was
/// applied again.
class Notifier : public QObject {
    Q_OBJECT
signals:
    void changed();
};

Notifier* notifier();

} // namespace Theme
