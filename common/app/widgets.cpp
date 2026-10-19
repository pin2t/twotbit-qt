#include "widgets.h"

#include <QApplication>
#include <QDateTime>
#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QTextDocument>
#include <QVBoxLayout>

#include <cmath>
#include <memory>

Card::Card(QWidget* parent, int radius) : QFrame(parent), radius_(radius) {
    setAttribute(Qt::WA_StyledBackground, false);
    setFrameShape(QFrame::NoFrame);
}

void Card::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(Theme::separator(palette()), 1));
    p.setBrush(Theme::card(palette()));
    p.drawRoundedRect(r, radius_, radius_);
}

Separator::Separator(Qt::Orientation orientation, QWidget* parent) : QWidget(parent), orientation_(orientation) {
    if (orientation == Qt::Horizontal) {
        setFixedHeight(1);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    } else {
        setFixedWidth(1);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    }
}

void Separator::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Theme::separator(palette()));
}

SignalBars::SignalBars(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAccessibleName("0 of 3 peers connected");
}

void SignalBars::setLevel(int level) {
    if (level == level_) return;
    level_ = level;
    setAccessibleName(QString("%1 of %2 peers connected").arg(level).arg(count));
    update();
}

QSize SignalBars::sizeHint() const { return {count * 6 + (count - 1) * 3 + 2, 22}; }

void SignalBars::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor color = palette().color(QPalette::WindowText);
    const double width = 6, gap = 3;
    double bottom = height() - 1.5;
    for (int i = 0; i < count; ++i) {
        double h = 8 + 6 * i;
        QRectF bar(1 + i * (width + gap), bottom - h, width, h);
        p.setPen(QPen(color, 1));
        p.setBrush(i < level_ ? QBrush(color) : Qt::NoBrush);
        p.drawRoundedRect(bar.adjusted(0.5, 0.5, -0.5, -0.5), 1.5, 1.5);
    }
}

ConnectionStatus::ConnectionStatus(QWidget* parent) : QWidget(parent), text_(new QLabel), bars_(new SignalBars) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 0, 6, 0);
    layout->setSpacing(10);
    Theme::setSecondary(text_);
    layout->addWidget(text_);
    layout->addWidget(bars_, 0, Qt::AlignVCenter);
}

void ConnectionStatus::setStatus(const tb::SyncStatus& status) {
    text_->setText(QString::fromStdString(status.text()));
    bars_->setLevel(status.bars());
    setToolTip(QString("%1 peer%2 connected, block %3").arg(status.peers).arg(status.peers == 1 ? "" : "s").arg(status.height));
}

QRCodeView::QRCodeView(int side, QWidget* parent) : QWidget(parent), side_(side) {
    setFixedSize(side, side);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void QRCodeView::setText(const QString& text) {
    if (text == text_ && code_) return;
    text_ = text;
    code_ = tb::QRCode::encode(text.toStdString());
    setAccessibleName("QR code of " + text);
    update();
}

void QRCodeView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawRoundedRect(rect(), 12, 12);
    if (!code_) return;
    // Modules in whole device pixels: the code keeps sharp edges at any
    // display scale.
    double ratio = devicePixelRatioF();
    const int quiet = 2;
    int total = code_->size() + 2 * quiet;
    int sidePx = int(std::floor(std::min(width(), height()) * ratio));
    int cell = sidePx / total;
    int origin = (sidePx - cell * total) / 2 + cell * quiet;
    p.setRenderHint(QPainter::Antialiasing, false);
    p.scale(1 / ratio, 1 / ratio);
    QPainterPath path;
    for (int y = 0; y < code_->size(); ++y)
        for (int x = 0; x < code_->size(); ++x)
            if (code_->isDark(x, y)) path.addRect(origin + x * cell, origin + y * cell, cell, cell);
    p.fillPath(path, Qt::black);
}

CopyButton::CopyButton(const QString& help, QWidget* parent) : QToolButton(parent) {
    setToolTip(help);
    setAccessibleName(help);
    setAutoRaise(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setIconSize(QSize(14, 14));
    refreshIcon();
}

void CopyButton::refreshIcon() { Theme::setGlyphIcon(this, Glyph::copy, 14, Theme::Tone::secondary); }

GlyphLabel::GlyphLabel(Glyph glyph, int pixelSize, Theme::Tone tone, QWidget* parent) : QLabel(parent), glyph_(glyph), pixelSize_(pixelSize) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    Theme::setTone(this, tone);
}

void GlyphLabel::setGlyph(Glyph glyph) {
    glyph_ = glyph;
    update();
}

QSize GlyphLabel::sizeHint() const { return {pixelSize_, pixelSize_}; }

QSize GlyphLabel::minimumSizeHint() const { return sizeHint(); }

// The glyph in the window text color, which the tone sets.
void GlyphLabel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    QPixmap pixmap = Theme::glyphPixmap(glyph_, palette().color(QPalette::WindowText), pixelSize_, devicePixelRatioF());
    QRect box(QPoint(0, 0), QSize(pixelSize_, pixelSize_));
    box.moveCenter(rect().center());
    p.drawPixmap(box.topLeft(), pixmap);
}

SortControl::SortControl(const QString& title, QWidget* parent) : QWidget(parent), title_(title) {
    setCursor(Qt::PointingHandCursor);
    setToolTip("Sort by " + title.toLower());
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void SortControl::setState(bool active, bool ascending) {
    active_ = active;
    ascending_ = ascending;
    update();
}

QSize SortControl::sizeHint() const {
    QFontMetrics fm(Theme::font(1.0, QFont::DemiBold));
    return {fm.horizontalAdvance(title_) + 16, fm.height() + 6};
}

void SortControl::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = Theme::font(1.0, QFont::DemiBold);
    p.setFont(f);
    QFontMetrics fm(f);
    p.setPen(palette().color(QPalette::WindowText));
    int textWidth = fm.horizontalAdvance(title_);
    p.drawText(QRect(0, 0, textWidth, height()), Qt::AlignVCenter | Qt::AlignLeft, title_);
    QColor on = Theme::accent(), off = Theme::secondaryText(palette());
    double x = textWidth + 5, cy = height() / 2.0;
    auto triangle = [&](bool up, const QColor& color) {
        QPainterPath path;
        if (up) {
            path.moveTo(x, cy - 1.5);
            path.lineTo(x + 6, cy - 1.5);
            path.lineTo(x + 3, cy - 5.5);
        } else {
            path.moveTo(x, cy + 1.5);
            path.lineTo(x + 6, cy + 1.5);
            path.lineTo(x + 3, cy + 5.5);
        }
        path.closeSubpath();
        p.fillPath(path, color);
    };
    triangle(true, active_ && ascending_ ? on : off);
    triangle(false, active_ && !ascending_ ? on : off);
}

void SortControl::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) emit sortRequested(active_ ? !ascending_ : false);
}

SegmentedControl::SegmentedControl(const QStringList& items, QWidget* parent) : QWidget(parent), group_(new QButtonGroup(this)) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    for (int i = 0; i < items.size(); ++i) {
        auto* button = new QPushButton(items[i]);
        button->setCheckable(true);
        button->setFocusPolicy(Qt::TabFocus);
        button->setMinimumWidth(56);
        group_->addButton(button, i);
        layout->addWidget(button);
    }
    group_->setExclusive(true);
    if (!items.isEmpty()) group_->button(0)->setChecked(true);
    connect(group_, &QButtonGroup::idToggled, this, [this](int id, bool on) {
        if (on) emit changed(id);
    });
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

int SegmentedControl::current() const { return group_->checkedId(); }

void SegmentedControl::setCurrent(int index) {
    if (QAbstractButton* b = group_->button(index)) b->setChecked(true);
}

Placeholder::Placeholder(Glyph glyph, const QString& title, const QString& description, QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->addStretch(1);
    auto* icon = new GlyphLabel(glyph, 40);
    layout->addWidget(icon, 0, Qt::AlignHCenter);
    layout->addSpacing(10);
    auto* titleLabel = new QLabel(title);
    titleLabel->setFont(Theme::font(1.45, QFont::DemiBold));
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);
    auto* descriptionLabel = label(description, Theme::Tone::secondary);
    descriptionLabel->setAlignment(Qt::AlignCenter);
    descriptionLabel->setWordWrap(true);
    layout->addWidget(descriptionLabel);
    layout->addStretch(1);
}

ElidedLabel::ElidedLabel(QWidget* parent) : QLabel(parent) {
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    setTextFormat(Qt::PlainText);
}

void ElidedLabel::setFullText(const QString& text) {
    full_ = text;
    setToolTip(text);
    elide();
}

QSize ElidedLabel::minimumSizeHint() const { return {fontMetrics().horizontalAdvance("…") * 4, QLabel::minimumSizeHint().height()}; }

QSize ElidedLabel::sizeHint() const { return {fontMetrics().horizontalAdvance(full_) + 2, QLabel::sizeHint().height()}; }

void ElidedLabel::resizeEvent(QResizeEvent* event) {
    QLabel::resizeEvent(event);
    elide();
}

void ElidedLabel::changeEvent(QEvent* event) {
    QLabel::changeEvent(event);
    if (event->type() == QEvent::FontChange) {
        elide();
        updateGeometry();
    }
}

void ElidedLabel::elide() {
    QString shown = fontMetrics().elidedText(full_, Qt::ElideMiddle, width());
    if (shown != text()) setText(shown);
}

WrappingText::WrappingText(QWidget* parent) : QTextEdit(parent) {
    setReadOnly(true);
    setTextInteractionFlags(Qt::TextSelectableByMouse);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document()->setDocumentMargin(0);
    QTextOption option = document()->defaultTextOption();
    option.setAlignment(Qt::AlignHCenter);
    document()->setDefaultTextOption(option);
    viewport()->setAutoFillBackground(false);
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

void WrappingText::setText(const QString& text) {
    if (text == toPlainText()) return;
    setPlainText(text);
    updateGeometry();
}

int WrappingText::heightForWidth(int width) const {
    // Laid out on a copy, as the shown text keeps its own width.
    std::unique_ptr<QTextDocument> doc(document()->clone());
    doc->setTextWidth(std::max(1, width - 2 * frameWidth()));
    return int(std::ceil(doc->size().height())) + 2 * frameWidth();
}

QSize WrappingText::sizeHint() const {
    std::unique_ptr<QTextDocument> doc(document()->clone());
    doc->setTextWidth(-1);
    int width = std::min(int(std::ceil(doc->idealWidth())) + 2 * frameWidth(), maximumWidth());
    return {width, heightForWidth(width)};
}

QSize WrappingText::minimumSizeHint() const {
    return {std::min(sizeHint().width(), 120), 0};
}

Toast::Toast(QWidget* parent)
    : QWidget(parent), icon_(new GlyphLabel(Glyph::checkFilled, 16, Theme::Tone::success)), text_(new QLabel), opacity_(new QGraphicsOpacityEffect(this)),
      animation_(new QPropertyAnimation(opacity_, "opacity", this)) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 8, 16, 8);
    layout->setSpacing(8);
    layout->addWidget(icon_, 0, Qt::AlignVCenter);
    text_->setFont(Theme::font(1.0, QFont::Medium));
    text_->setWordWrap(true);
    text_->setMaximumWidth(480);
    layout->addWidget(text_);
    setGraphicsEffect(opacity_);
    opacity_->setOpacity(0);
    animation_->setDuration(200);
    connect(animation_, &QPropertyAnimation::finished, this, [this] {
        if (opacity_->opacity() < 0.01) hide();
    });
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, [this] { fade(false); });
    parent->installEventFilter(this);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();
}

void Toast::showMessage(const QString& text, bool error) {
    icon_->setGlyph(error ? Glyph::warningFilled : Glyph::checkFilled);
    Theme::setTone(icon_, error ? Theme::Tone::danger : Theme::Tone::success);
    text_->setText(text);
    adjustSize();
    reposition();
    raise();
    show();
    fade(true);
    timer_.start(error ? 6000 : 2000);
}

void Toast::fade(bool in) {
    animation_->stop();
    animation_->setStartValue(opacity_->opacity());
    animation_->setEndValue(in ? 1.0 : 0.0);
    animation_->start();
}

void Toast::reposition() {
    QWidget* p = parentWidget();
    if (!p) return;
    QSize size = sizeHint();
    resize(size);
    move((p->width() - size.width()) / 2, p->height() - size.height() - 28);
}

bool Toast::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) reposition();
    return QWidget::eventFilter(watched, event);
}

void Toast::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    QColor fill = Theme::dark() ? QColor(0x3a, 0x3a, 0x3a) : QColor(0xfc, 0xfc, 0xfc);
    p.setPen(QPen(Theme::separator(palette()), 1));
    p.setBrush(fill);
    p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
}

QLabel* label(const QString& text, Theme::Tone tone, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    if (tone != Theme::Tone::normal) Theme::setTone(l, tone);
    return l;
}

QString dateText(int64_t unix) {
    if (unix == 0) return {};
    QDateTime t = QDateTime::fromSecsSinceEpoch(unix);
    QLocale locale;
    return locale.toString(t.date(), "MMM d, yyyy") + " at " + locale.toString(t.time(), QLocale::ShortFormat);
}

QPushButton* glyphButton(Glyph glyph, const QString& text, QWidget* parent) {
    auto* b = new QPushButton(text, parent);
    Theme::setGlyphIcon(b, glyph, 16);
    b->setIconSize(QSize(14, 14));
    return b;
}

QHBoxLayout* centered(QWidget* widget) {
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->addStretch(1);
    row->addWidget(widget);
    row->addStretch(1);
    return row;
}
