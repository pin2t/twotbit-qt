#include "coinsview.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

constexpr int copySize = 14;

} // namespace

CoinsModel::CoinsModel(AppModel& model, QObject* parent) : QAbstractTableModel(parent), model_(model) { reload(); }

int CoinsModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : int(coins_.size()); }

int CoinsModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : columns; }

QVariant CoinsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= int(coins_.size())) return {};
    const tb::Coin& c = coins_[size_t(index.row())];
    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case time: return q(tb::Format::relativeTime(c.time, model_.now()));
        case address: return q(c.address);
        case amount: return q(tb::Format::amount(c.value));
        }
        break;
    case Qt::ToolTipRole:
        if (index.column() == time) return dateText(c.time);
        if (index.column() == address) return q(c.address);
        break;
    case Qt::ForegroundRole:
        if (!c.confirmed) return Theme::secondaryText(QApplication::palette());
        break;
    case Qt::TextAlignmentRole:
        if (index.column() == amount) return int(Qt::AlignRight | Qt::AlignVCenter);
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    case Qt::FontRole:
        if (index.column() == address) return Theme::mono();
        break;
    }
    return {};
}

QVariant CoinsModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal) return {};
    if (role == Qt::DisplayRole) {
        switch (section) {
        case time: return "Time";
        case address: return "Address";
        case amount: return "Amount";
        }
    }
    if (role == Qt::TextAlignmentRole) return int((section == amount ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
    return {};
}

void CoinsModel::sort(int column, Qt::SortOrder order) {
    if (column == address) return;
    emit layoutAboutToBeChanged();
    column_ = column;
    order_ = order;
    QModelIndexList before = persistentIndexList();
    std::vector<tb::OutPoint> ids;
    for (const auto& i : before) ids.push_back(coins_[size_t(i.row())].outPoint);
    this->order();
    QModelIndexList after;
    for (size_t k = 0; k < ids.size(); ++k) after.push_back(index(rowOf(ids[k]), before[int(k)].column()));
    changePersistentIndexList(before, after);
    emit layoutChanged();
}

void CoinsModel::order() {
    bool ascending = order_ == Qt::AscendingOrder;
    int column = column_;
    std::stable_sort(coins_.begin(), coins_.end(), [&](const tb::Coin& a, const tb::Coin& b) {
        int64_t ka = column == amount ? a.value : a.time, kb = column == amount ? b.value : b.time;
        if (ka != kb) return ascending ? ka < kb : ka > kb;
        return a.outPoint.description() < b.outPoint.description();
    });
}

int CoinsModel::rowOf(const tb::OutPoint& outPoint) const {
    for (size_t i = 0; i < coins_.size(); ++i)
        if (coins_[i].outPoint == outPoint) return int(i);
    return -1;
}

void CoinsModel::reload() {
    beginResetModel();
    coins_ = model_.coins();
    order();
    endResetModel();
}

void CoinsModel::refreshTimes() {
    if (!coins_.empty()) emit dataChanged(index(0, time), index(int(coins_.size()) - 1, time), {Qt::DisplayRole, Qt::ToolTipRole});
}

AddressDelegate::AddressDelegate(AppModel& model, CoinsModel& coins, QObject* parent) : QStyledItemDelegate(parent), model_(model), coins_(coins) {}

namespace {

/// Where the address and its copy button go in the cell: the address
/// shortened to the room left, the button right after it.
struct AddressLayout {
    QRect text;
    QRect copy;
    QString shown;
};

AddressLayout layoutAddress(const QRect& cell, const QString& address) {
    QFontMetrics fm(Theme::mono());
    QRect room = cell.adjusted(6, 0, -(copySize + 16), 0);
    AddressLayout l;
    l.shown = fm.elidedText(address, Qt::ElideMiddle, room.width());
    l.text = QRect(room.left(), room.top(), std::min(room.width(), fm.horizontalAdvance(l.shown)), room.height());
    l.copy = QRect(l.text.right() + 6, cell.center().y() - copySize / 2 - 1, copySize + 4, copySize + 2);
    return l;
}

} // namespace

void AddressDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    AddressLayout l = layoutAddress(option.rect, opt.text);
    opt.text.clear();
    QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
    painter->save();
    painter->setFont(Theme::mono());
    QColor color = index.data(Qt::ForegroundRole).isValid() ? index.data(Qt::ForegroundRole).value<QColor>() : opt.palette.color(QPalette::Text);
    painter->setPen(color);
    painter->drawText(l.text, Qt::AlignLeft | Qt::AlignVCenter, l.shown);
    Theme::icon(Glyph::copy, Theme::secondaryText(opt.palette), copySize).paint(painter, l.copy.adjusted(2, 1, -2, -1));
    painter->restore();
}

bool AddressDelegate::editorEvent(QEvent* event, QAbstractItemModel*, const QStyleOptionViewItem& option, const QModelIndex& index) {
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        QRect copy = layoutAddress(option.rect, index.data().toString()).copy;
        if (mouse->button() == Qt::LeftButton && copy.contains(mouse->position().toPoint())) {
            model_.copy(q(coins_.coin(index.row()).address), "Address");
            return true;
        }
    }
    return false;
}

QSize AddressDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    QSize s = QStyledItemDelegate::sizeHint(option, index);
    return {QFontMetrics(Theme::mono()).horizontalAdvance(index.data().toString()) + copySize + 24, std::max(s.height(), 30)};
}

CoinsView::CoinsView(AppModel& model, QWidget* parent)
    : QWidget(parent), model_(model), coins_(new CoinsModel(model, this)), stack_(new QStackedWidget), table_(new QTableView), summary_(new QLabel) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    stack_->addWidget(new Placeholder(Glyph::coins, "No coins yet", "Coins appear here once the wallet receives a payment."));

    table_->setModel(coins_);
    table_->setItemDelegateForColumn(CoinsModel::address, new AddressDelegate(model, *coins_, table_));
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setSortingEnabled(true);
    table_->sortByColumn(CoinsModel::time, Qt::DescendingOrder);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(false);
    table_->setWordWrap(false);
    table_->setFrameShape(QFrame::NoFrame);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->setMouseTracking(true);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(32);
    QHeaderView* header = table_->horizontalHeader();
    header->setSectionResizeMode(CoinsModel::time, QHeaderView::Interactive);
    header->setSectionResizeMode(CoinsModel::address, QHeaderView::Stretch);
    header->setSectionResizeMode(CoinsModel::amount, QHeaderView::Interactive);
    header->resizeSection(CoinsModel::time, 130);
    header->resizeSection(CoinsModel::amount, 160);
    header->setHighlightSections(false);
    // Only time and amount sort; a click on the address keeps the order.
    connect(header, &QHeaderView::sortIndicatorChanged, this, [this, header](int column, Qt::SortOrder) {
        if (column == CoinsModel::address) {
            QSignalBlocker block(header);
            header->setSortIndicator(property("sortColumn").toInt(), Qt::SortOrder(property("sortOrder").toInt()));
            return;
        }
        setProperty("sortColumn", column);
        setProperty("sortOrder", int(header->sortIndicatorOrder()));
    });
    setProperty("sortColumn", int(CoinsModel::time));
    setProperty("sortOrder", int(Qt::DescendingOrder));
    stack_->addWidget(table_);
    layout->addWidget(stack_, 1);
    layout->addWidget(new Separator(Qt::Horizontal));

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(12, 12, 12, 12);
    bar->setSpacing(10);
    Theme::setSecondary(summary_);
    bar->addWidget(summary_);
    bar->addStretch(1);
    auto* merge = glyphButton(Glyph::merge, "Merge");
    merge->setEnabled(false);
    merge->setToolTip("Merge the selected coins to one address — coming in a later version");
    auto* split = glyphButton(Glyph::split, "Split");
    split->setEnabled(false);
    split->setToolTip("Split one coin to many — coming in a later version");
    send_ = glyphButton(Glyph::send, "Send");
    for (QPushButton* b : {merge, split, send_}) {
        b->setMinimumWidth(96);
        bar->addWidget(b);
    }
    // Merge and Split explain themselves even while disabled.
    for (QPushButton* b : {merge, split}) b->setAttribute(Qt::WA_AlwaysShowToolTips);
    layout->addLayout(bar);

    connect(send_, &QPushButton::clicked, this, [this] { model_.openSend(model_.selectedCoins()); });
    auto* enter = new QShortcut(QKeySequence(Qt::Key_Return), table_, nullptr, nullptr, Qt::WidgetShortcut);
    connect(enter, &QShortcut::activated, this, [this] {
        if (!model_.coins().empty()) model_.openSend(model_.selectedCoins());
    });
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &CoinsView::selectionChanged);
    connect(table_, &QTableView::customContextMenuRequested, this, &CoinsView::showMenu);
    connect(&model, &AppModel::walletChanged, this, &CoinsView::reload);
    connect(&model, &AppModel::nowChanged, coins_, &CoinsModel::refreshTimes);
    connect(&model, &AppModel::selectedCoinsChanged, this, [this] {
        if (selected() != model_.selectedCoins()) restoreSelection();
        refreshSummary();
    });
    connect(Theme::notifier(), &Theme::Notifier::changed, table_->viewport(), [this] { table_->viewport()->update(); });
    reload();
}

void CoinsView::reload() {
    coins_->reload();
    restoreSelection();
    stack_->setCurrentIndex(model_.coins().empty() ? 0 : 1);
    send_->setEnabled(!model_.coins().empty());
    // Prominent only while it can be used: a disabled default button keeps
    // its accent border.
    send_->setDefault(send_->isEnabled());
    refreshSummary();
}

CoinSet CoinsView::selected() const {
    CoinSet out;
    for (const QModelIndex& i : table_->selectionModel()->selectedRows()) out.insert(coins_->coin(i.row()).outPoint);
    return out;
}

void CoinsView::selectionChanged() {
    if (restoring_) return;
    model_.setSelectedCoins(selected());
    refreshSummary();
}

void CoinsView::restoreSelection() {
    restoring_ = true;
    QItemSelection selection;
    for (const tb::OutPoint& op : model_.selectedCoins()) {
        int row = coins_->rowOf(op);
        if (row >= 0) selection.select(coins_->index(row, 0), coins_->index(row, CoinsModel::columns - 1));
    }
    table_->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    restoring_ = false;
}

void CoinsView::refreshSummary() {
    const auto& coins = model_.coins();
    const CoinSet& chosen = model_.selectedCoins();
    int64_t total = 0;
    size_t count = 0;
    for (const auto& c : coins) {
        if (!chosen.empty() && !chosen.count(c.outPoint)) continue;
        total += c.value;
        ++count;
    }
    if (chosen.empty())
        summary_->setText(QString("%1 coin%2, %3").arg(coins.size()).arg(coins.size() == 1 ? "" : "s").arg(q(tb::Format::amount(total))));
    else
        summary_->setText(QString("%1 selected, %2").arg(count).arg(q(tb::Format::amount(total))));
}

void CoinsView::showMenu(const QPoint& at) {
    QModelIndex index = table_->indexAt(at);
    if (index.isValid() && !table_->selectionModel()->isRowSelected(index.row(), {}))
        table_->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    CoinSet chosen = selected();
    QMenu menu(this);
    QAction* send = menu.addAction("Send Selected…", this, [this, chosen] { model_.openSend(chosen); });
    send->setEnabled(!chosen.empty());
    if (chosen.size() == 1) {
        int row = coins_->rowOf(*chosen.begin());
        const tb::Coin& coin = coins_->coin(row);
        QString address = q(coin.address), outpoint = q(coin.outPoint.description());
        menu.addAction("Copy Address", this, [this, address] { model_.copy(address, "Address"); });
        menu.addAction("Copy Outpoint", this, [this, outpoint] { model_.copy(outpoint, "Outpoint"); });
    }
    menu.exec(table_->viewport()->mapToGlobal(at));
}
