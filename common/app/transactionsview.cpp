#include "transactionsview.h"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QMenu>

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

} // namespace

AddressList::AddressList(const std::vector<tb::TxAddress>& addresses, bool trailing, QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    Qt::Alignment align = (trailing ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter;
    for (size_t i = 0; i < addresses.size(); ++i) {
        if (i == TransactionCard::rows - 1 && addresses.size() > TransactionCard::rows) {
            auto* more = new QLabel(QString("%1 more").arg(addresses.size() - i));
            QFont f = Theme::font(0.95);
            f.setItalic(true);
            more->setFont(f);
            more->setAlignment(align);
            layout->addWidget(more);
            break;
        }
        const tb::TxAddress& a = addresses[i];
        auto* row = new ElidedLabel;
        QFont f = Theme::mono(0.95);
        if (a.mine) f.setBold(true);
        if (a.address.empty()) f.setItalic(true);
        row->setFont(f);
        row->setAlignment(align);
        row->setTextInteractionFlags(Qt::TextSelectableByMouse);
        row->setFullText(a.address.empty() ? QString("unknown") : q(tb::Format::groupAddress(a.address)));
        layout->addWidget(row);
    }
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

TransactionCard::TransactionCard(AppModel& model, const tb::WalletTransaction& tx, QWidget* parent)
    : Card(parent, 10), model_(model), tx_(tx), time_(new QLabel) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* top = new QHBoxLayout;
    top->setSpacing(12);
    time_->setFixedWidth(110);
    top->addWidget(time_);
    auto* id = new ElidedLabel;
    id->setFont(Theme::mono(0.95));
    id->setFullText(q(tx.txid.hex()));
    id->setTextInteractionFlags(Qt::TextSelectableByMouse);
    top->addWidget(id, 1);
    auto* copy = new CopyButton("Copy the transaction id");
    connect(copy, &QToolButton::clicked, this, [this] { model_.copy(q(tx_.txid.hex()), "Transaction id"); });
    top->addWidget(copy);
    top->addSpacing(8);
    auto* net = new QLabel(q(tb::Format::netAmount(tx.net)));
    net->setFont(Theme::font(1.0, QFont::Medium));
    top->addWidget(net);
    layout->addLayout(top);

    auto* flow = new QHBoxLayout;
    flow->setSpacing(10);
    flow->addWidget(new AddressList(tx.inputs, false), 1);
    flow->addWidget(new GlyphLabel(Glyph::arrowRight, 14), 0, Qt::AlignVCenter);
    flow->addWidget(new AddressList(tx.outputs, true), 1);
    layout->addLayout(flow);

    if (!tx.confirmed) {
        auto* unconfirmed = new QLabel("Unconfirmed");
        unconfirmed->setFont(Theme::font(0.88));
        layout->addWidget(unconfirmed);
        // The whole card in the secondary color, as unconfirmed ones read.
        for (QLabel* l : findChildren<QLabel*>())
            if (!qobject_cast<GlyphLabel*>(l)) Theme::setSecondary(l);
    }
    refreshTime();
}

void TransactionCard::refreshTime() {
    time_->setText(q(tb::Format::relativeTime(tx_.time, model_.now())));
    time_->setToolTip(dateText(tx_.time));
}

void TransactionCard::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    menu.addAction("Copy Transaction ID", this, [this] { model_.copy(q(tx_.txid.hex()), "Transaction id"); });
    menu.exec(event->globalPos());
}

TransactionsView::TransactionsView(AppModel& model, QWidget* parent)
    : QWidget(parent), model_(model), time_(new SortControl("Time")), amount_(new SortControl("Amount")), stack_(new QStackedWidget), list_(new QWidget),
      listLayout_(new QVBoxLayout(list_)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* header = new QHBoxLayout;
    header->setContentsMargins(26, 10, 26, 10);
    header->addWidget(time_);
    header->addStretch(1);
    header->addWidget(amount_);
    layout->addLayout(header);
    layout->addWidget(new Separator(Qt::Horizontal));

    stack_->addWidget(new Placeholder(Glyph::transactions, "No transactions yet", "Payments to and from the wallet appear here."));
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listLayout_->setContentsMargins(16, 16, 16, 16);
    listLayout_->setSpacing(10);
    listLayout_->addStretch(1);
    scroll->setWidget(list_);
    list_->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    stack_->addWidget(scroll);
    layout->addWidget(stack_, 1);

    connect(time_, &SortControl::sortRequested, this, [this](bool up) { sort(false, up); });
    connect(amount_, &SortControl::sortRequested, this, [this](bool up) { sort(true, up); });
    connect(&model, &AppModel::walletChanged, this, &TransactionsView::rebuild);
    connect(&model, &AppModel::nowChanged, this, &TransactionsView::refreshTimes);
    sort(false, false);
}

void TransactionsView::sort(bool byAmount, bool ascending) {
    byAmount_ = byAmount;
    ascending_ = ascending;
    time_->setState(!byAmount, ascending);
    amount_->setState(byAmount, ascending);
    rebuild();
}

void TransactionsView::rebuild() {
    // The transactions by time or net amount, ties broken by the id so the
    // order stays put across updates.
    std::vector<tb::WalletTransaction> sorted = model_.history();
    std::sort(sorted.begin(), sorted.end(), [this](const tb::WalletTransaction& a, const tb::WalletTransaction& b) {
        const tb::WalletTransaction& x = ascending_ ? a : b;
        const tb::WalletTransaction& y = ascending_ ? b : a;
        int64_t kx = byAmount_ ? x.net : x.time, ky = byAmount_ ? y.net : y.time;
        if (kx != ky) return kx < ky;
        return x.txid.hex() < y.txid.hex();
    });
    stack_->setCurrentIndex(sorted.empty() ? 0 : 1);
    if (sorted == shown_) return;
    // Cards of transactions that did not change are kept and only moved.
    std::map<std::string, TransactionCard*> old;
    for (TransactionCard* c : cards_) {
        listLayout_->removeWidget(c);
        old[c->transaction().txid.hex()] = c;
    }
    cards_.clear();
    for (const auto& tx : sorted) {
        TransactionCard* card;
        auto it = old.find(tx.txid.hex());
        if (it != old.end() && it->second->transaction() == tx) {
            card = it->second;
            old.erase(it);
        } else {
            card = new TransactionCard(model_, tx, list_);
        }
        listLayout_->insertWidget(int(cards_.size()), card);
        cards_.push_back(card);
    }
    for (auto& [id, card] : old) card->deleteLater();
    shown_ = std::move(sorted);
}

void TransactionsView::refreshTimes() {
    for (TransactionCard* c : cards_) c->refreshTime();
}
