#pragma once

#include <QLabel>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <map>

#include "appmodel.h"
#include "widgets.h"

/// AddressList stacks addresses closely, grouped by fours and each
/// shortened to fit, the wallet's own in bold and unknown ones in italics,
/// aligned right when trailing.
class AddressList : public QWidget {
    Q_OBJECT
public:
    AddressList(const std::vector<tb::TxAddress>& addresses, bool trailing, QWidget* parent = nullptr);
};

/// TransactionCard is one transaction: when it was confirmed or seen, its id
/// with a copy button, what it did to the balance, and the addresses it
/// spends from and pays to. Unconfirmed ones are grey.
class TransactionCard : public Card {
    Q_OBJECT
public:
    /// The most addresses a card lists on either side; past it the last row
    /// counts the ones left out.
    static constexpr size_t rows = 6;

    TransactionCard(AppModel& model, const tb::WalletTransaction& tx, QWidget* parent = nullptr);
    const tb::WalletTransaction& transaction() const { return tx_; }
    void refreshTime();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    AppModel& model_;
    tb::WalletTransaction tx_;
    QLabel* time_;
};

/// TransactionsView lists the wallet transactions as cards, sortable by
/// time and amount, the newest first.
class TransactionsView : public QWidget {
    Q_OBJECT
public:
    explicit TransactionsView(AppModel& model, QWidget* parent = nullptr);

private:
    void sort(bool byAmount, bool ascending);
    void rebuild();
    void refreshTimes();

    AppModel& model_;
    bool byAmount_ = false;
    bool ascending_ = false;
    SortControl* time_;
    SortControl* amount_;
    QStackedWidget* stack_;
    QWidget* list_;
    QVBoxLayout* listLayout_;
    std::vector<TransactionCard*> cards_;
    std::vector<tb::WalletTransaction> shown_;
};
