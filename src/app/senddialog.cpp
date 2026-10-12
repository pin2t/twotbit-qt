#include "senddialog.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

/// SendError is what the form shows when the fee rate is not a number of at
/// least 1.
class FeeRateError : public tb::Error {
public:
    FeeRateError() : tb::Error("enter a fee rate of at least 1 sat/vB") {}
};

bool parseRate(const QString& text, int64_t* rate) {
    bool ok = false;
    qlonglong v = text.trimmed().toLongLong(&ok);
    if (!ok || v < 1) return false;
    *rate = v;
    return true;
}

} // namespace

SendDialog::SendDialog(AppModel& model, const CoinSet& preselected, QWidget* parent)
    : QDialog(parent), model_(model), chosen_(preselected), address_(new QLineEdit), amount_(new QLineEdit), unit_(new SegmentedControl({"sats", "BTC"})),
      feeRate_(new QLineEdit(QString::number(AppModel::defaultFeeRate))), coins_(new QListWidget), summary_(new QLabel) {
    setWindowTitle("Send");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(14);
    auto* title = new QLabel("Send");
    title->setFont(Theme::font(1.45, QFont::Bold));
    layout->addWidget(title);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);
    auto* to = new QLabel("To");
    to->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(to, 0, 0);
    address_->setPlaceholderText("Destination address");
    address_->setFont(Theme::mono());
    grid->addWidget(address_, 0, 1);
    auto* amountLabel = new QLabel("Amount");
    amountLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(amountLabel, 1, 0);
    auto* amountRow = new QHBoxLayout;
    amount_->setPlaceholderText("Amount");
    amountRow->addWidget(amount_, 1);
    amountRow->addWidget(unit_);
    auto* max = new QPushButton("Max");
    max->setAutoDefault(false);
    amountRow->addWidget(max);
    grid->addLayout(amountRow, 1, 1);
    auto* feeLabel = new QLabel("Fee rate");
    feeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(feeLabel, 2, 0);
    auto* feeRow = new QHBoxLayout;
    feeRate_->setFixedWidth(80);
    feeRow->addWidget(feeRate_);
    feeRow->addWidget(label("sat/vB", Theme::Tone::secondary));
    feeRow->addStretch(1);
    grid->addLayout(feeRow, 2, 1);
    grid->setColumnStretch(1, 1);
    layout->addLayout(grid);

    layout->addWidget(label("Coins to spend (none ticked: chosen automatically)", Theme::Tone::secondary));
    coins_->setMinimumHeight(150);
    coins_->setSelectionMode(QAbstractItemView::NoSelection);
    coins_->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(coins_, 1);
    summary_->setWordWrap(true);
    summary_->setMinimumHeight(36);
    summary_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(summary_);

    auto* buttons = new QHBoxLayout;
    auto* cancel = new QPushButton("Cancel");
    cancel->setAutoDefault(false);
    cancel->setMinimumWidth(96);
    buttons->addWidget(cancel);
    buttons->addStretch(1);
    auto* reviewButton = new QPushButton("Review");
    reviewButton->setDefault(true);
    reviewButton->setMinimumWidth(96);
    buttons->addWidget(reviewButton);
    layout->addLayout(buttons);
    resize(640, 600);

    connect(address_, &QLineEdit::textChanged, this, &SendDialog::refreshSummary);
    connect(amount_, &QLineEdit::textChanged, this, &SendDialog::refreshSummary);
    connect(feeRate_, &QLineEdit::textChanged, this, &SendDialog::refreshSummary);
    connect(unit_, &SegmentedControl::changed, this, &SendDialog::convert);
    connect(max, &QPushButton::clicked, this, &SendDialog::fillMax);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(reviewButton, &QPushButton::clicked, this, &SendDialog::review);
    connect(&model, &AppModel::walletChanged, this, &SendDialog::reloadCoins);
    reloadCoins();
}

void SendDialog::reloadCoins() {
    coins_->clear();
    CoinSet listed;
    for (const tb::Coin& coin : model_.coins()) {
        listed.insert(coin.outPoint);
        auto* item = new QListWidgetItem(coins_);
        auto* row = new QWidget;
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(8, 3, 8, 3);
        layout->setSpacing(10);
        auto* check = new QCheckBox;
        check->setChecked(chosen_.count(coin.outPoint) > 0);
        layout->addWidget(check);
        auto* value = new QLabel(q(tb::Format::amount(coin.value)));
        value->setFixedWidth(140);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        layout->addWidget(value);
        auto* address = new ElidedLabel;
        address->setFont(Theme::mono(0.95));
        address->setFullText(q(coin.address));
        layout->addWidget(address, 1);
        if (!coin.confirmed) {
            auto* note = label("unconfirmed", Theme::Tone::secondary);
            note->setFont(Theme::font(0.88));
            layout->addWidget(note);
        }
        tb::OutPoint op = coin.outPoint;
        connect(check, &QCheckBox::toggled, this, [this, op](bool on) {
            if (on)
                chosen_.insert(op);
            else
                chosen_.erase(op);
            refreshSummary();
        });
        item->setSizeHint(row->sizeHint());
        coins_->setItemWidget(item, row);
    }
    // Coins spent meanwhile cannot stay chosen.
    for (auto it = chosen_.begin(); it != chosen_.end();) it = listed.count(*it) ? std::next(it) : chosen_.erase(it);
    refreshSummary();
}

std::vector<tb::Coin> SendDialog::chosenCoins() const {
    std::vector<tb::Coin> out;
    for (const tb::Coin& c : model_.coins())
        if (chosen_.count(c.outPoint)) out.push_back(c);
    return out;
}

AppModel::PlannedSend SendDialog::plan() const {
    int64_t sats = tb::Format::parseAmount(amount_->text().toStdString(), unit());
    int64_t rate;
    if (!parseRate(feeRate_->text(), &rate)) throw FeeRateError();
    return model_.planSend(address_->text(), sats, rate, chosenCoins());
}

void SendDialog::refreshSummary() {
    // The fee and change of the payment as entered, what is missing or
    // wrong, or a plain hint while the form is untouched.
    if (address_->text().trimmed().isEmpty() && amount_->text().trimmed().isEmpty()) {
        summary_->setText("Enter the destination address and the amount.");
        Theme::setTone(summary_, Theme::Tone::secondary);
        return;
    }
    try {
        AppModel::PlannedSend p = plan();
        QString text = "Fee: " + AppModel::feeText(p);
        if (p.spend.change > 0) text += "\nChange: " + q(tb::Format::amount(p.spend.change));
        summary_->setText(text);
        Theme::setTone(summary_, Theme::Tone::normal);
    } catch (const std::exception& e) {
        summary_->setText(e.what());
        Theme::setTone(summary_, Theme::Tone::danger);
    }
}

void SendDialog::convert(int next) {
    // Rewrites the entered amount in the newly chosen unit.
    tb::AmountUnit from = shownUnit_ == 0 ? tb::AmountUnit::sats : tb::AmountUnit::btc;
    tb::AmountUnit to = next == 0 ? tb::AmountUnit::sats : tb::AmountUnit::btc;
    shownUnit_ = next;
    try {
        int64_t sats = tb::Format::parseAmount(amount_->text().toStdString(), from);
        if (sats > 0) amount_->setText(q(tb::Format::unitText(sats, to)));
    } catch (const std::exception&) {
    }
    refreshSummary();
}

void SendDialog::fillMax() {
    // Enters the most the chosen coins, or all coins, can pay at the fee
    // rate.
    int64_t rate = AppModel::defaultFeeRate;
    parseRate(feeRate_->text(), &rate);
    amount_->setText(q(tb::Format::unitText(model_.maxAmount(address_->text(), rate, chosenCoins()), unit())));
}

void SendDialog::review() {
    AppModel::PlannedSend p;
    try {
        p = plan();
    } catch (const std::exception& e) {
        fail(e.what());
        return;
    }
    QMessageBox box(QMessageBox::Question, "Confirm payment", "Confirm payment", QMessageBox::NoButton, this);
    box.setInformativeText(AppModel::confirmText(p));
    QPushButton* sign = box.addButton("Sign && Send", QMessageBox::AcceptRole);
    box.addButton("Back", QMessageBox::RejectRole);
    box.setDefaultButton(sign);
    box.exec();
    if (box.clickedButton() == sign) send(p);
}

void SendDialog::send(const AppModel::PlannedSend& plan) {
    std::pair<tb::Hash256, size_t> sent;
    try {
        sent = model_.sendPlanned(plan);
    } catch (const std::exception& e) {
        fail(QString("payment not sent: ") + e.what());
        return;
    }
    QMessageBox box(QMessageBox::Information, "Payment sent", "Payment sent", QMessageBox::Ok, this);
    box.setInformativeText(QString("Transaction %1\nbroadcast to %2 peer%3.").arg(q(sent.first.hex())).arg(sent.second).arg(sent.second == 1 ? "" : "s"));
    box.setTextInteractionFlags(Qt::TextSelectableByMouse);
    box.exec();
    accept();
}

void SendDialog::fail(const QString& text) {
    QMessageBox box(QMessageBox::Warning, "Payment not sent", "Payment not sent", QMessageBox::Ok, this);
    box.setInformativeText(text);
    box.exec();
}
