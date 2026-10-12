#pragma once

#include <QAbstractTableModel>
#include <QLabel>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QWidget>

#include "appmodel.h"
#include "widgets.h"

/// CoinsModel is the table of coins, sorted by time or amount; ties keep the
/// outpoint order so rows stay put across updates.
class CoinsModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { time, address, amount, columns };

    explicit CoinsModel(AppModel& model, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void sort(int column, Qt::SortOrder order) override;

    const tb::Coin& coin(int row) const { return coins_[size_t(row)]; }
    int rowOf(const tb::OutPoint& outPoint) const;
    void reload();
    void refreshTimes();

private:
    void order();

    AppModel& model_;
    std::vector<tb::Coin> coins_;
    int column_ = time;
    Qt::SortOrder order_ = Qt::DescendingOrder;
};

/// AddressDelegate draws a coin's address in the monospaced font, shortened
/// in the middle, with a copy button at its end.
class AddressDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    AddressDelegate(AppModel& model, CoinsModel& coins, QObject* parent = nullptr);
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    AppModel& model_;
    CoinsModel& coins_;
};

/// CoinsView lists the spendable coins: when each appeared, its address
/// with a copy button, and its value, unconfirmed ones in grey, sortable by
/// time and amount, the newest first. Selected coins are what Send spends;
/// Merge and Split will merge coins into one and split one into many.
class CoinsView : public QWidget {
    Q_OBJECT
public:
    explicit CoinsView(AppModel& model, QWidget* parent = nullptr);

private:
    void reload();
    void selectionChanged();
    void restoreSelection();
    void refreshSummary();
    void showMenu(const QPoint& at);
    CoinSet selected() const;

    AppModel& model_;
    CoinsModel* coins_;
    QStackedWidget* stack_;
    QTableView* table_;
    QLabel* summary_;
    QPushButton* send_;
    bool restoring_ = false;
};
