#pragma once

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>

#include "appmodel.h"
#include "widgets.h"

/// SendDialog is the payment form: the destination, the amount in sats or
/// BTC with a Max button, the fee rate, and the coins to spend, none ticked
/// meaning chosen automatically. It shows the fee and change as entered, or
/// what is missing or wrong; Review asks for confirmation before the payment
/// is signed and broadcast.
class SendDialog : public QDialog {
    Q_OBJECT
public:
    SendDialog(AppModel& model, const CoinSet& preselected, QWidget* parent = nullptr);

private:
    tb::AmountUnit unit() const { return unit_->current() == 0 ? tb::AmountUnit::sats : tb::AmountUnit::btc; }
    std::vector<tb::Coin> chosenCoins() const;
    AppModel::PlannedSend plan() const;
    void reloadCoins();
    void refreshSummary();
    void convert(int next);
    void fillMax();
    void review();
    void send(const AppModel::PlannedSend& plan);
    void fail(const QString& text);

    AppModel& model_;
    CoinSet chosen_;
    QLineEdit* address_;
    QLineEdit* amount_;
    SegmentedControl* unit_;
    int shownUnit_ = 0;
    QLineEdit* feeRate_;
    QListWidget* coins_;
    QLabel* summary_;
};
