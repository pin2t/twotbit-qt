#include "mainwindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QResizeEvent>
#include <QVBoxLayout>

#include "coinsview.h"
#include "homeview.h"
#include "receivedialog.h"
#include "senddialog.h"
#include "settingsview.h"
#include "storage/secrets.h"
#include "transactionsview.h"

namespace {

struct SectionInfo {
    AppModel::Section section;
    const char* title;
    Glyph glyph;
};

const SectionInfo sections[] = {
    {AppModel::Section::home, "Home", Glyph::home},
    {AppModel::Section::coins, "Coins", Glyph::coins},
    {AppModel::Section::transactions, "Transactions", Glyph::transactions},
    {AppModel::Section::settings, "Settings", Glyph::settings},
};

QWidget* loadingPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->addStretch(1);
    auto* text = new QLabel("Opening the wallet…");
    text->setAlignment(Qt::AlignCenter);
    layout->addWidget(text);
    auto* progress = new QProgressBar;
    progress->setRange(0, 0);
    progress->setTextVisible(false);
    progress->setFixedWidth(220);
    layout->addWidget(progress, 0, Qt::AlignHCenter);
    layout->addStretch(1);
    return page;
}

} // namespace

RestoreView::RestoreView(AppModel& model, QWidget* parent)
    : QWidget(parent), model_(model), phrase_(new QPlainTextEdit), problem_(new QLabel), restore_(new QPushButton("Restore")) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(40, 40, 40, 40);
    layout->setSpacing(16);
    layout->addStretch(1);
    auto* icon = new GlyphLabel(Glyph::key, 44);
    layout->addWidget(icon, 0, Qt::AlignHCenter);
    auto* title = new QLabel("Recovery phrase not found");
    title->setFont(Theme::font(1.45, QFont::Bold));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);
    auto* description = label(QString("The wallet's recovery phrase is not in the %1 any more. Enter its 12 words to restore access "
                                      "to this wallet.")
                                  .arg(tb::CredentialSecretStore::storeName()),
                              Theme::Tone::secondary);
    description->setWordWrap(true);
    description->setAlignment(Qt::AlignCenter);
    description->setFixedWidth(440);
    layout->addLayout(centered(description));
    phrase_->setFont(Theme::mono(1.08));
    phrase_->setFixedSize(440, 90);
    phrase_->setTabChangesFocus(true);
    layout->addWidget(phrase_, 0, Qt::AlignHCenter);
    Theme::setTone(problem_, Theme::Tone::danger);
    problem_->setWordWrap(true);
    problem_->setAlignment(Qt::AlignCenter);
    problem_->hide();
    layout->addWidget(problem_);
    restore_->setEnabled(false);
    restore_->setMinimumWidth(110);
    layout->addWidget(restore_, 0, Qt::AlignHCenter);
    layout->addStretch(1);
    connect(phrase_, &QPlainTextEdit::textChanged, this, [this] {
        restore_->setEnabled(phrase_->toPlainText().simplified().split(' ', Qt::SkipEmptyParts).size() >= 12);
        restore_->setDefault(restore_->isEnabled());
    });
    connect(restore_, &QPushButton::clicked, this, [this] {
        try {
            model_.restore(phrase_->toPlainText());
        } catch (const std::exception& e) {
            problem_->setText(e.what());
            problem_->show();
        }
    });
}

MainWindow::MainWindow(AppModel& model, QWidget* parent)
    : QMainWindow(parent), model_(model), phases_(new QStackedWidget), failure_(new WrappingText), toast_(nullptr) {
    setWindowTitle("Twotbit");
    setMinimumSize(760, 760);
    resize(900, 860);
    setCentralWidget(phases_);

    phases_->addWidget(loadingPage());
    auto* failurePage = new QWidget;
    auto* failureLayout = new QVBoxLayout(failurePage);
    failureLayout->addStretch(1);
    failureLayout->addWidget(new GlyphLabel(Glyph::warning, 44), 0, Qt::AlignHCenter);
    auto* failureTitle = new QLabel("Twotbit failed to start");
    failureTitle->setFont(Theme::font(1.45, QFont::Bold));
    failureTitle->setAlignment(Qt::AlignCenter);
    failureLayout->addWidget(failureTitle);
    failure_->setFont(Theme::mono());
    failure_->setMaximumWidth(620);
    Theme::setSecondary(failure_);
    failureLayout->addLayout(centered(failure_));
    failureLayout->addStretch(1);
    phases_->addWidget(failurePage);
    phases_->addWidget(new RestoreView(model));

    toast_ = new Toast(phases_);
    buildMenus();
    connect(&model, &AppModel::phaseChanged, this, &MainWindow::phaseChanged);
    connect(&model, &AppModel::toast, toast_, &Toast::showMessage);
    connect(&model, &AppModel::receiveRequested, this, &MainWindow::openReceive);
    connect(&model, &AppModel::sendRequested, this, &MainWindow::openSend);
    connect(Theme::notifier(), &Theme::Notifier::changed, this, &MainWindow::refreshSidebarIcons);
    phaseChanged();
}

void MainWindow::buildMenus() {
    // On the Mac, Settings and Quit move to the application menu, which
    // leaves no File menu.
#ifdef Q_OS_MACOS
    QMenu* wallet = menuBar()->addMenu("&Wallet");
    QMenu* file = wallet;
#else
    QMenu* file = menuBar()->addMenu("&File");
#endif
    QAction* settings = file->addAction("&Settings…", this, [this] { model_.select(AppModel::Section::settings); });
    settings->setShortcut(QKeySequence("Ctrl+,"));
    settings->setMenuRole(QAction::PreferencesRole);
#ifndef Q_OS_MACOS
    file->addSeparator();
#endif
    QAction* exit = file->addAction("E&xit", this, &QWidget::close);
    exit->setShortcut(QKeySequence::Quit);
    exit->setMenuRole(QAction::QuitRole);
#ifndef Q_OS_MACOS
    QMenu* wallet = menuBar()->addMenu("&Wallet");
#endif
    QAction* receive = wallet->addAction("&Receive…", this, [this] { model_.openReceive(); });
    receive->setShortcut(QKeySequence("Ctrl+Shift+R"));
    QAction* send = wallet->addAction("&Send…", this, [this] { model_.openSend(); });
    send->setShortcut(QKeySequence("Ctrl+Shift+S"));
    wallet->addSeparator();
    copyAddress_ = wallet->addAction("&Copy Address", this, [this] { model_.copy(model_.address(), "Address"); });
    copyAddress_->setShortcut(QKeySequence("Ctrl+Shift+C"));
    walletActions_ = {settings, receive, send};
    connect(&model_, &AppModel::addressChanged, this, [this] { copyAddress_->setEnabled(!model_.address().isEmpty()); });
    copyAddress_->setEnabled(false);
    QMenu* help = menuBar()->addMenu("&Help");
    QAction* about = help->addAction("&About Twotbit", this, [this] {
        QMessageBox::about(this, "About Twotbit",
                           "<b>Twotbit " + QApplication::applicationVersion() +
                               "</b><p>An SPV Bitcoin wallet: BIP84 keys, a validated header chain, BIP157/158 compact block filters and a "
                               "small pool of peers.</p><p>Copyright © 2026 Ilya Pokolev. MIT License.</p>");
    });
    about->setMenuRole(QAction::AboutRole);
}

void MainWindow::buildReady() {
    ready_ = new QWidget;
    auto* layout = new QHBoxLayout(ready_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    sidebar_ = new QListWidget;
    sidebar_->setFixedWidth(200);
    sidebar_->setFrameShape(QFrame::NoFrame);
    sidebar_->setIconSize(QSize(18, 18));
    sidebar_->setSpacing(1);
    sidebar_->setFont(Theme::font(1.05));
    sidebar_->viewport()->setAutoFillBackground(false);
    sidebar_->setAutoFillBackground(false);
    sidebar_->setContentsMargins(6, 8, 6, 8);
    for (const auto& s : sections) {
        auto* item = new QListWidgetItem(s.title, sidebar_);
        item->setSizeHint(QSize(180, 36));
    }
    refreshSidebarIcons();
    auto* side = new QWidget;
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(6, 10, 0, 10);
    sideLayout->addWidget(sidebar_);
    layout->addWidget(side);
    layout->addWidget(new Separator(Qt::Vertical));

    auto* content = new QWidget;
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    auto* header = new QHBoxLayout;
    header->setContentsMargins(24, 12, 16, 12);
    header->setSpacing(10);
    title_ = new QLabel;
    title_->setFont(Theme::font(1.5, QFont::DemiBold));
    header->addWidget(title_, 0, Qt::AlignVCenter);
    subtitle_ = label(model_.network().kind == tb::Network::Kind::mainnet ? QString() : "net: " + QString::fromStdString(model_.network().displayName()),
                      Theme::Tone::secondary);
    header->addWidget(subtitle_, 0, Qt::AlignVCenter);
    header->addStretch(1);
    connection_ = new ConnectionStatus;
    header->addWidget(connection_, 0, Qt::AlignVCenter);
    contentLayout->addLayout(header);
    contentLayout->addWidget(new Separator(Qt::Horizontal));

    sections_ = new QStackedWidget;
    home_ = new HomeView(model_);
    settings_ = new SettingsView(model_);
    sections_->addWidget(home_);
    sections_->addWidget(new CoinsView(model_));
    sections_->addWidget(new TransactionsView(model_));
    sections_->addWidget(settings_);
    contentLayout->addWidget(sections_, 1);
    layout->addWidget(content, 1);

    connect(sidebar_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) model_.select(sections[row].section);
    });
    connect(&model_, &AppModel::selectionChanged, this, &MainWindow::selectionChanged);
    connect(&model_, &AppModel::statusChanged, this, [this] { connection_->setStatus(model_.status()); });
    connection_->setStatus(model_.status());
    phases_->addWidget(ready_);
    toast_->raise();
    selectionChanged();
    home_->setWide(width() > wideWidth);
}

void MainWindow::refreshSidebarIcons() {
    if (!sidebar_) return;
    for (int i = 0; i < sidebar_->count(); ++i) sidebar_->item(i)->setIcon(Theme::icon(sections[i].glyph, QColor(), 18));
}

void MainWindow::phaseChanged() {
    bool ready = model_.phase() == AppModel::Phase::ready;
    for (QAction* a : walletActions_) a->setEnabled(ready);
    switch (model_.phase()) {
    case AppModel::Phase::loading: phases_->setCurrentIndex(0); break;
    case AppModel::Phase::failed:
        failure_->setText(model_.failure());
        phases_->setCurrentIndex(1);
        break;
    case AppModel::Phase::missingPhrase: phases_->setCurrentIndex(2); break;
    case AppModel::Phase::ready:
        if (!ready_) buildReady();
        settings_->refresh();
        phases_->setCurrentWidget(ready_);
        break;
    }
    copyAddress_->setEnabled(!model_.address().isEmpty());
}

void MainWindow::selectionChanged() {
    int row = 0;
    for (int i = 0; i < 4; ++i)
        if (sections[i].section == model_.selection()) row = i;
    sidebar_->setCurrentRow(row);
    sections_->setCurrentIndex(row);
    title_->setText(sections[row].title);
    if (model_.selection() == AppModel::Section::settings) model_.recheckPeer();
}

void MainWindow::openReceive() {
    if (model_.phase() != AppModel::Phase::ready) return;
    if (receive_) {
        receive_->raise();
        receive_->activateWindow();
        return;
    }
    receive_ = new ReceiveDialog(model_, this);
    receive_->setAttribute(Qt::WA_DeleteOnClose);
    receive_->open();
}

void MainWindow::openSend(const CoinSet& coins) {
    if (model_.phase() != AppModel::Phase::ready) return;
    if (send_) {
        send_->raise();
        send_->activateWindow();
        return;
    }
    send_ = new SendDialog(model_, coins, this);
    send_->setAttribute(Qt::WA_DeleteOnClose);
    send_->open();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (home_) home_->setWide(event->size().width() > wideWidth);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // The wallet shuts down before the app quits: the sync stops and the
    // database closes.
    if (!model_.isShutDown()) {
        hide();
        model_.shutdown();
    }
    event->accept();
    QApplication::quit();
}

ReceiveDialog* MainWindow::receiveDialog() const { return receive_; }

SendDialog* MainWindow::sendDialog() const { return send_; }
