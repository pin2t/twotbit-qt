#include "homeview.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QVBoxLayout>

#include "transactionsview.h"

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

} // namespace

StatRow::StatRow(const QString& name, QWidget* parent) : QWidget(parent), value_(new QLabel), usd_(new QLabel) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 10, 0, 10);
    layout->setSpacing(12);
    auto* nameLabel = label(name, Theme::Tone::secondary);
    nameLabel->setFont(Theme::font(1.08));
    layout->addWidget(nameLabel, 0, Qt::AlignTop);
    layout->addStretch(1);
    auto* values = new QVBoxLayout;
    values->setSpacing(1);
    value_->setFont(Theme::font(1.08));
    value_->setAlignment(Qt::AlignRight);
    values->addWidget(value_);
    usd_->setFont(Theme::font(0.88));
    usd_->setAlignment(Qt::AlignRight);
    Theme::setSecondary(usd_);
    usd_->hide();
    values->addWidget(usd_);
    layout->addLayout(values);
    setMinimumWidth(220);
}

void StatRow::setValue(const QString& value, const QString& shorter, const QString& usd) {
    long_ = value;
    short_ = shorter.isEmpty() ? value : shorter;
    usd_->setText(usd);
    usd_->setVisible(!usd.isEmpty());
    fit();
}

void StatRow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    fit();
}

void StatRow::fit() {
    int room = width() - 120;
    QString text = value_->fontMetrics().horizontalAdvance(long_) <= room || width() == 0 ? long_ : short_;
    if (value_->text() != text) value_->setText(text);
}

StatsView::StatsView(AppModel& model, QWidget* parent)
    : Card(parent, 12), model_(model), received_(new StatRow("Received")), sent_(new StatRow("Sent")), fees_(new StatRow("Fees")),
      transactions_(new StatRow("Transactions")), coins_(new StatRow("Coins")), lifetime_(new StatRow("Lifetime")) {
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(18, 8, 18, 8);
    grid->setHorizontalSpacing(20);
    grid->setVerticalSpacing(0);
    StatRow* left[] = {received_, sent_, fees_};
    StatRow* right[] = {transactions_, coins_, lifetime_};
    for (int i = 0; i < 3; ++i) {
        grid->addWidget(left[i], i * 2, 0);
        grid->addWidget(new Separator(Qt::Vertical), i * 2, 1);
        grid->addWidget(right[i], i * 2, 2);
        if (i < 2) grid->addWidget(new Separator(Qt::Horizontal), i * 2 + 1, 0, 1, 3);
    }
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    refresh();
}

void StatsView::refresh() {
    const tb::WalletStats& s = model_.stats();
    received_->setValue(q(tb::Format::amount(s.received)), {}, model_.usd(s.received));
    sent_->setValue(q(tb::Format::amount(s.sent)), {}, model_.usd(s.sent));
    fees_->setValue(q(tb::Format::amount(s.fees)), {}, model_.usd(s.fees));
    transactions_->setValue(QString::number(s.transactions));
    coins_->setValue(QString("%1 (%2 unspent)").arg(s.coins).arg(s.unspent));
    lifetime_->setValue(q(tb::Format::lifetime(model_.createdAt(), model_.now(), 2)), q(tb::Format::lifetime(model_.createdAt(), model_.now(), 1)));
    adjustSize();
}

BalanceView::BalanceView(AppModel& model, QWidget* parent) : QWidget(parent), model_(model), balance_(new QLabel), pending_(new QLabel), usd_(new QLabel) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    auto* row = new QHBoxLayout;
    row->setSpacing(6);
    row->addStretch(1);
    balance_->setFont(Theme::font(2.6, QFont::Bold));
    row->addWidget(balance_, 0, Qt::AlignBaseline);
    pending_->setFont(Theme::font(0.95));
    Theme::setSecondary(pending_);
    row->addWidget(pending_, 0, Qt::AlignBaseline);
    row->addStretch(1);
    layout->addLayout(row);
    usd_->setFont(Theme::font(1.25));
    usd_->setAlignment(Qt::AlignHCenter);
    Theme::setSecondary(usd_);
    layout->addWidget(usd_);
    refresh();
}

void BalanceView::refresh() {
    const tb::SyncStatus& s = model_.status();
    balance_->setText(q(tb::Format::balance(s.balance, s.pending)));
    QString note = q(tb::Format::pendingNote(s.pending));
    pending_->setText(note);
    pending_->setVisible(!note.isEmpty());
    QString usd = model_.usdBalance();
    usd_->setText(usd);
    usd_->setVisible(!usd.isEmpty());
}

HomeView::HomeView(AppModel& model, QWidget* parent)
    : QWidget(parent), model_(model), main_(new QWidget), divider_(new Separator(Qt::Vertical)), transactions_(new TransactionsView(model)),
      qrRoom_(new QWidget), qr_(new QRCodeView(qrSide)), address_(new QLabel) {
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(main_, 1);
    outer->addWidget(divider_);
    outer->addWidget(transactions_, 1);

    auto* layout = new QVBoxLayout(main_);
    layout->setContentsMargins(28, 16, 28, 24);
    layout->setSpacing(0);
    layout->addSpacerItem(new QSpacerItem(0, qrSide - qrRoom, QSizePolicy::Minimum, QSizePolicy::MinimumExpanding));
    layout->setStretch(layout->count() - 1, 1);
    // The code is laid over its room, outside the layout, its bottom on the
    // room's.
    qrRoom_->setFixedSize(qrSide, qrRoom);
    layout->addWidget(qrRoom_, 0, Qt::AlignHCenter);
    qr_->setParent(main_);
    qrRoom_->installEventFilter(this);
    layout->addSpacing(14);

    auto* addressRow = new QHBoxLayout;
    addressRow->setSpacing(6);
    addressRow->addStretch(1);
    address_->setFont(Theme::mono(1.25));
    address_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    addressRow->addWidget(address_);
    auto* copy = new CopyButton("Copy the address");
    connect(copy, &QToolButton::clicked, this, [this] { model_.copy(model_.address(), "Address"); });
    addressRow->addWidget(copy);
    addressRow->addStretch(1);
    layout->addLayout(addressRow);
    layout->addSpacing(22);

    auto* balance = new BalanceView(model);
    layout->addWidget(balance);
    layout->addSpacing(22);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(14);
    buttons->addStretch(1);
    auto* receive = glyphButton(Glyph::receive, "Receive");
    send_ = glyphButton(Glyph::send, "Send");
    for (QPushButton* b : {receive, send_}) {
        b->setMinimumSize(150, 34);
        buttons->addWidget(b);
    }
    buttons->addStretch(1);
    connect(receive, &QPushButton::clicked, this, [this] { model_.openReceive(); });
    connect(send_, &QPushButton::clicked, this, [this] { model_.openSend(); });
    layout->addLayout(buttons);
    layout->addStretch(1);
    layout->addSpacing(24);

    auto* stats = new StatsView(model);
    layout->addWidget(stats, 0, Qt::AlignHCenter);

    connect(&model, &AppModel::addressChanged, this, &HomeView::refreshAddress);
    connect(&model, &AppModel::statusChanged, balance, &BalanceView::refresh);
    connect(&model, &AppModel::rateChanged, balance, &BalanceView::refresh);
    connect(&model, &AppModel::walletChanged, stats, &StatsView::refresh);
    connect(&model, &AppModel::rateChanged, stats, &StatsView::refresh);
    connect(&model, &AppModel::nowChanged, stats, &StatsView::refresh);
    connect(&model, &AppModel::walletChanged, this, [this] { send_->setEnabled(!model_.coins().empty()); });
    refreshAddress();
    send_->setEnabled(!model_.coins().empty());
    setWide(false);
}

void HomeView::setWide(bool wide) {
    divider_->setVisible(wide);
    transactions_->setVisible(wide);
    if (wide)
        main_->setFixedWidth(640);
    else
        main_->setMaximumWidth(QWIDGETSIZE_MAX), main_->setMinimumWidth(0);
}

bool HomeView::eventFilter(QObject* watched, QEvent* event) {
    if (watched == qrRoom_ && (event->type() == QEvent::Move || event->type() == QEvent::Resize))
        qr_->move(qrRoom_->x(), qrRoom_->geometry().bottom() + 1 - qr_->height());
    return QWidget::eventFilter(watched, event);
}

void HomeView::refreshAddress() {
    address_->setText(q(tb::Format::groupAddress(model_.address().toStdString())));
    qr_->setText(model_.address());
}
