#pragma once

#include <QLabel>
#include <QPushButton>
#include <QWidget>

#include "appmodel.h"
#include "widgets.h"

class TransactionsView;

/// StatRow is one statistic: its name at the left, its value at the right,
/// and for an amount its worth in US dollars under the value. A value may
/// come in a shorter form for when the long one does not fit.
class StatRow : public QWidget {
    Q_OBJECT
public:
    explicit StatRow(const QString& name, QWidget* parent = nullptr);
    void setValue(const QString& value, const QString& shorter = {}, const QString& usd = {});

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void fit();

    QLabel* value_;
    QLabel* usd_;
    QString long_, short_;
};

/// StatsView sums up the wallet's lifetime in two columns: the amounts
/// received, sent and paid in fees with their worth in US dollars at the
/// left, the transactions, coins and the wallet's age at the right, with
/// separator lines between the rows and between the columns.
class StatsView : public Card {
    Q_OBJECT
public:
    explicit StatsView(AppModel& model, QWidget* parent = nullptr);
    void refresh();

private:
    AppModel& model_;
    StatRow *received_, *sent_, *fees_, *transactions_, *coins_, *lifetime_;
};

/// BalanceView shows the spendable balance, the incoming unconfirmed part of
/// it, and its worth in US dollars once a rate is known.
class BalanceView : public QWidget {
    Q_OBJECT
public:
    explicit BalanceView(AppModel& model, QWidget* parent = nullptr);
    void refresh();

private:
    AppModel& model_;
    QLabel* balance_;
    QLabel* pending_;
    QLabel* usd_;
};

/// HomeView is the wallet at a glance: the QR code of the current receive
/// address with the address under it and a copy button after it, the
/// balance, the Receive and Send buttons, and the wallet statistics at the
/// bottom. In a wide window the transactions list alongside it at the right.
class HomeView : public QWidget {
    Q_OBJECT
public:
    explicit HomeView(AppModel& model, QWidget* parent = nullptr);

    /// Whether the window is wide enough to list the transactions too.
    void setWide(bool wide);

private:
    void refreshAddress();

    AppModel& model_;
    QWidget* main_;
    Separator* divider_;
    TransactionsView* transactions_;
    QRCodeView* qr_;
    QLabel* address_;
    QPushButton* send_;
};
