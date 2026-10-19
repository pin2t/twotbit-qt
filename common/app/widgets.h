#pragma once

#include <QButtonGroup>
#include <QFrame>
#include <QLabel>
#include <QPointer>
#include <QPropertyAnimation>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <optional>

#include "format/qrcode.h"
#include "sync/engine.h"
#include "theme.h"

class QGraphicsOpacityEffect;
class QHBoxLayout;
class QPushButton;

/// Card is a rounded panel in the card color, as the statistics, the
/// transactions and the settings groups are drawn on.
class Card : public QFrame {
    Q_OBJECT
public:
    explicit Card(QWidget* parent = nullptr, int radius = 10);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int radius_;
};

/// Separator is a hairline in the separator color, across or upright.
class Separator : public QWidget {
    Q_OBJECT
public:
    explicit Separator(Qt::Orientation orientation, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Qt::Orientation orientation_;
};

/// SignalBars draws the connectivity as three ascending bars: one solid bar
/// per connected peer, the others outlined.
class SignalBars : public QWidget {
    Q_OBJECT
public:
    static constexpr int count = 3;

    explicit SignalBars(QWidget* parent = nullptr);
    void setLevel(int level);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int level_ = 0;
};

/// ConnectionStatus is the status line with the bars, at the top right of
/// the window.
class ConnectionStatus : public QWidget {
    Q_OBJECT
public:
    explicit ConnectionStatus(QWidget* parent = nullptr);
    void setStatus(const tb::SyncStatus& status);

private:
    QLabel* text_;
    SignalBars* bars_;
};

/// QRCodeView draws the QR code of a text dark on a light card with the
/// quiet zone scanners need around it, its square modules aligned to whole
/// device pixels so the code stays crisp.
class QRCodeView : public QWidget {
    Q_OBJECT
public:
    explicit QRCodeView(int side = 240, QWidget* parent = nullptr);
    void setText(const QString& text);
    QSize sizeHint() const override { return {side_, side_}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int side_;
    QString text_;
    std::optional<tb::QRCode> code_;
};

/// CopyButton is a small borderless copy icon.
class CopyButton : public QToolButton {
    Q_OBJECT
public:
    explicit CopyButton(const QString& help, QWidget* parent = nullptr);

private:
    void refreshIcon();
};

/// GlyphLabel shows one icon glyph at a size in a tone.
class GlyphLabel : public QLabel {
    Q_OBJECT
public:
    GlyphLabel(Glyph glyph, int pixelSize, Theme::Tone tone = Theme::Tone::secondary, QWidget* parent = nullptr);
    void setGlyph(Glyph glyph);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Glyph glyph_;
    int pixelSize_;
};

/// SortControl is a pair of small arrows, up for ascending over down for
/// descending, after the column's title: clicking sorts by its column, the
/// newest or largest first, or in the other order when it sorts already,
/// and the arrow of the order in use is highlighted.
class SortControl : public QWidget {
    Q_OBJECT
public:
    explicit SortControl(const QString& title, QWidget* parent = nullptr);
    void setState(bool active, bool ascending);
    QSize sizeHint() const override;

signals:
    void sortRequested(bool ascending);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QString title_;
    bool active_ = false;
    bool ascending_ = false;
};

/// SegmentedControl is a row of joined toggle buttons of which one is on,
/// as the unit picker.
class SegmentedControl : public QWidget {
    Q_OBJECT
public:
    SegmentedControl(const QStringList& items, QWidget* parent = nullptr);
    int current() const;
    void setCurrent(int index);

signals:
    void changed(int index);

private:
    QButtonGroup* group_;
};

/// Placeholder tells why a list is empty: a large glyph, a title and a
/// description, centered.
class Placeholder : public QWidget {
    Q_OBJECT
public:
    Placeholder(Glyph glyph, const QString& title, const QString& description, QWidget* parent = nullptr);
};

/// ElidedLabel shows a text shortened in the middle to its width, the whole
/// text in its tooltip and when copied.
class ElidedLabel : public QLabel {
    Q_OBJECT
public:
    explicit ElidedLabel(QWidget* parent = nullptr);
    void setFullText(const QString& text);
    const QString& fullText() const { return full_; }
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void elide();

    QString full_;
};

/// WrappingText is centered, selectable text that breaks a word too long for
/// a line anywhere, as SwiftUI's Text does with a payment request or a path,
/// where a word-wrapped label lets it run past its edge.
class WrappingText : public QTextEdit {
    Q_OBJECT
public:
    explicit WrappingText(QWidget* parent = nullptr);
    void setText(const QString& text);
    QString text() const { return toPlainText(); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
};

/// Toast is a short note that fades away at the bottom of its parent, as
/// after copying, or an error, which stays longer.
class Toast : public QWidget {
    Q_OBJECT
public:
    explicit Toast(QWidget* parent);
    void showMessage(const QString& text, bool error);
    void reposition();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void fade(bool in);

    GlyphLabel* icon_;
    QLabel* text_;
    QGraphicsOpacityEffect* opacity_;
    QPropertyAnimation* animation_;
    QTimer timer_;
};

/// A label in the tone.
QLabel* label(const QString& text, Theme::Tone tone = Theme::Tone::normal, QWidget* parent = nullptr);

/// The absolute date and time of a unix time, for tooltips, or "" for an
/// unknown one.
QString dateText(int64_t unix);

/// A push button with the glyph's icon.
QPushButton* glyphButton(Glyph glyph, const QString& text, QWidget* parent = nullptr);

/// A row holding the widget centered at its own width. A word-wrapped label
/// is centered so rather than by aligning it in a column, which measures its
/// height at the column's whole width and so cuts off the lines it wraps
/// onto when narrower.
QHBoxLayout* centered(QWidget* widget);
