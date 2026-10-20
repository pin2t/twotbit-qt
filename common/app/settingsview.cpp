#include "settingsview.h"

#include <QDir>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>

#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDesktopServices>
#include <QUrl>
#endif

#include "phrasedialog.h"
#include "ownerauth.h"

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

/// The networks the wallet can switch to.
const std::vector<const tb::Network*>& switchable() {
    static const std::vector<const tb::Network*> networks = {&tb::Network::mainnet(), &tb::Network::testnet3(), &tb::Network::testnet4(),
                                                             &tb::Network::signet(), &tb::Network::regtest()};
    return networks;
}

QLabel* sectionTitle(const QString& text) {
    auto* l = new QLabel(text);
    l->setFont(Theme::font(1.08, QFont::DemiBold));
    l->setContentsMargins(4, 0, 0, 0);
    return l;
}

/// One row of a settings card: the name at the left and the control at the
/// right.
QHBoxLayout* row(const QString& name, QWidget* control) {
    auto* layout = new QHBoxLayout;
    layout->setSpacing(12);
    auto* l = new QLabel(name);
    l->setMinimumWidth(150);
    layout->addWidget(l);
    layout->addWidget(control, 1);
    return layout;
}

} // namespace

SettingsView::SettingsView(AppModel& model, QWidget* parent)
    : QWidget(parent), model_(model), peer_(new QLineEdit), peerStatus_(new QLabel), notes_(new QVBoxLayout), network_(new QComboBox),
      folder_(new ElidedLabel), xpub_(new ElidedLabel) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    scroll->setWidget(content);
    scroll->viewport()->setAutoFillBackground(false);
    content->setAutoFillBackground(false);
    outer->addWidget(scroll);

    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 20, 24, 24);
    layout->setSpacing(8);

    layout->addWidget(sectionTitle("Pinned peer"));
    auto* peerCard = new Card;
    auto* peerLayout = new QVBoxLayout(peerCard);
    peerLayout->setContentsMargins(16, 14, 16, 14);
    peerLayout->setSpacing(6);
    peer_->setPlaceholderText("IP address or IP:port");
    peer_->setClearButtonEnabled(true);
    peerLayout->addLayout(row("Pinned peer", peer_));
    peerLayout->addWidget(peerStatus_);
    notes_->setSpacing(2);
    peerLayout->addLayout(notes_);
    layout->addWidget(peerCard);
    layout->addSpacing(16);

    layout->addWidget(sectionTitle("Wallet"));
    auto* walletCard = new Card;
    auto* walletLayout = new QVBoxLayout(walletCard);
    walletLayout->setContentsMargins(16, 10, 16, 10);
    walletLayout->setSpacing(0);
    for (const tb::Network* n : switchable()) network_->addItem(q(n->displayName()));
    network_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    auto* networkBox = new QWidget;
    auto* networkLayout = new QHBoxLayout(networkBox);
    networkLayout->setContentsMargins(0, 0, 0, 0);
    networkLayout->addWidget(network_);
    networkLayout->addStretch(1);
    auto addRow = [&](QHBoxLayout* r, bool last) {
        auto* holder = new QWidget;
        holder->setLayout(r);
        r->setContentsMargins(0, 8, 0, 8);
        walletLayout->addWidget(holder);
        if (!last) walletLayout->addWidget(new Separator(Qt::Horizontal));
    };
    addRow(row("Network", networkBox), false);

    auto* folderBox = new QWidget;
    auto* folderLayout = new QHBoxLayout(folderBox);
    folderLayout->setContentsMargins(0, 0, 0, 0);
    folder_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    folderLayout->addWidget(folder_, 1);
#ifdef Q_OS_MACOS
    auto* explorer = new QPushButton("Show in Finder");
#elif defined(Q_OS_LINUX)
    auto* explorer = new QPushButton("Show in Files");
#else
    auto* explorer = new QPushButton("Show in Explorer");
#endif
    folderLayout->addWidget(explorer);
    addRow(row("Data folder", folderBox), false);

    auto* xpubBox = new QWidget;
    auto* xpubLayout = new QHBoxLayout(xpubBox);
    xpubLayout->setContentsMargins(0, 0, 0, 0);
    xpubLayout->setSpacing(4);
    xpub_->setFont(Theme::mono(0.95));
    xpub_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    xpubLayout->addWidget(xpub_, 1);
    auto* copy = new CopyButton("Copy the account public key, for watching the wallet elsewhere");
    xpubLayout->addWidget(copy);
    addRow(row("Account public key", xpubBox), false);

    auto* phraseBox = new QWidget;
    auto* phraseLayout = new QHBoxLayout(phraseBox);
    phraseLayout->setContentsMargins(0, 0, 0, 0);
    auto* show = new QPushButton("Show…");
    phraseLayout->addWidget(show);
    phraseLayout->addStretch(1);
    addRow(row("Recovery phrase", phraseBox), true);
    layout->addWidget(walletCard);
    layout->addStretch(1);

    connect(peer_, &QLineEdit::textEdited, this, [this](const QString& text) { model_.peerEdited(text); });
    connect(&model, &AppModel::peerChanged, this, &SettingsView::refreshPeer);
    connect(network_, &QComboBox::activated, this, &SettingsView::networkPicked);
    connect(explorer, &QPushButton::clicked, this, [this] {
#ifdef Q_OS_MACOS
        QProcess::startDetached("/usr/bin/open", {"-R", model_.options().dataDirectory});
#elif defined(Q_OS_LINUX)
        // The folder selected in the file manager's window, as GNOME Files
        // and the others show it through FileManager1, or the folder opened
        // where none answers.
        QUrl folder = QUrl::fromLocalFile(model_.options().dataDirectory);
        QDBusMessage call = QDBusMessage::createMethodCall("org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
                                                           "org.freedesktop.FileManager1", "ShowItems");
        call << QStringList{folder.toString()} << QString();
        auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [folder](QDBusPendingCallWatcher* w) {
            if (w->isError()) QDesktopServices::openUrl(folder);
            w->deleteLater();
        });
#else
        QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(model_.options().dataDirectory)});
#endif
    });
    connect(copy, &QToolButton::clicked, this, [this] { model_.copy(model_.accountXPub(), "Account public key"); });
    connect(show, &QPushButton::clicked, this, &SettingsView::reveal);
    refresh();
}

void SettingsView::refresh() {
    for (size_t i = 0; i < switchable().size(); ++i)
        if (*switchable()[i] == model_.network()) network_->setCurrentIndex(int(i));
    folder_->setFullText(QDir::toNativeSeparators(model_.options().dataDirectory));
    xpub_->setFullText(model_.accountXPub());
    refreshPeer();
}

void SettingsView::refreshPeer() {
    if (peer_->text() != model_.peerText()) peer_->setText(model_.peerText());
    peerStatus_->setText(model_.peerStatus());
    peerStatus_->setVisible(!model_.peerStatus().isEmpty());
    Theme::setTone(peerStatus_, model_.peerConnected() ? Theme::Tone::success : Theme::Tone::secondary);
    while (QLayoutItem* item = notes_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const QString& line : model_.peerNote()) {
        auto* note = label(line, Theme::Tone::secondary);
        note->setFont(Theme::font(0.9));
        notes_->addWidget(note);
    }
}

void SettingsView::networkPicked(int index) {
    const tb::Network& next = *switchable()[size_t(index)];
    if (next == model_.network()) return;
    QMessageBox box(QMessageBox::Question, "Twotbit", "Switch to " + q(next.displayName()) + "?", QMessageBox::NoButton, window());
    box.setInformativeText("Twotbit relaunches on " + q(next.displayName()) + ". Every network has its own wallet; this one stays as it is.");
    QPushButton* relaunch = box.addButton("Switch and Relaunch", QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(relaunch);
    box.exec();
    if (box.clickedButton() == relaunch) {
        model_.switchNetwork(next);
        return;
    }
    refresh();
}

void SettingsView::reveal() {
    if (!OwnerAuth::confirmOwner(window(), "Twotbit wants to show the wallet's recovery phrase.")) return;
    PhraseDialog dialog(model_.phraseWords(), window());
    dialog.exec();
}
