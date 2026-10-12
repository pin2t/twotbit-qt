#include "receivedialog.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

ReceiveDialog::ReceiveDialog(AppModel& model, QWidget* parent)
    : QDialog(parent), model_(model), amount_(new QLineEdit), unit_(new SegmentedControl({"sats", "BTC"})), problem_(new QLabel), qr_(new QRCodeView(280)),
      uri_(new WrappingText) {
    setWindowTitle("Receive");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(14);
    auto* title = new QLabel("Receive");
    title->setFont(Theme::font(1.45, QFont::Bold));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);
    auto* amountRow = new QHBoxLayout;
    amount_->setPlaceholderText("Amount (optional)");
    amountRow->addWidget(amount_, 1);
    amountRow->addWidget(unit_);
    layout->addLayout(amountRow);
    Theme::setTone(problem_, Theme::Tone::danger);
    problem_->setFixedHeight(problem_->fontMetrics().height());
    layout->addWidget(problem_);
    layout->addWidget(qr_, 0, Qt::AlignHCenter);
    uri_->setFont(Theme::mono(0.95));
    layout->addWidget(uri_);
    auto* buttons = new QHBoxLayout;
    auto* copy = glyphButton(Glyph::copy, "Copy Invoice");
    buttons->addWidget(copy);
    buttons->addStretch(1);
    auto* close = new QPushButton("Close");
    close->setMinimumWidth(96);
    buttons->addWidget(close);
    layout->addSpacing(6);
    layout->addLayout(buttons);
    setFixedWidth(460);

    connect(amount_, &QLineEdit::textChanged, this, &ReceiveDialog::refresh);
    connect(unit_, &SegmentedControl::changed, this, &ReceiveDialog::refresh);
    connect(&model, &AppModel::addressChanged, this, &ReceiveDialog::refresh);
    connect(copy, &QPushButton::clicked, this, [this] { model_.copy(uri_->text(), "Invoice"); });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    refresh();
}

void ReceiveDialog::refresh() {
    int64_t sats = 0;
    QString problem;
    try {
        sats = tb::Format::parseAmount(amount_->text().toStdString(), unit_->current() == 0 ? tb::AmountUnit::sats : tb::AmountUnit::btc);
    } catch (const std::exception& e) {
        problem = e.what();
    }
    problem_->setText(problem);
    QString uri = QString::fromStdString(tb::Format::invoiceURI(model_.address().toStdString(), sats));
    qr_->setText(uri);
    uri_->setText(uri);
    // The dialog fits its height to the request, which wraps onto another
    // line once an amount makes it longer than the dialog is wide.
    if (isVisible()) resize(width(), layout()->totalHeightForWidth(width()));
}
