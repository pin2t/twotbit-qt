#pragma once

#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QStackedWidget>

#include "appmodel.h"
#include "widgets.h"

class CoinsView;
class HomeView;
class ReceiveDialog;
class SendDialog;
class SettingsView;
class TransactionsView;

/// RestoreView asks for the recovery phrase of a wallet whose phrase is no
/// longer in the Credential Manager or the Keychain.
class RestoreView : public QWidget {
    Q_OBJECT
public:
    explicit RestoreView(AppModel& model, QWidget* parent = nullptr);

private:
    AppModel& model_;
    QPlainTextEdit* phrase_;
    QLabel* problem_;
    QPushButton* restore_;
};

/// MainWindow is the window: the sections in a sidebar, the connectivity at
/// the top right, the Receive and Send dialogs, and notes at the bottom.
/// Before the wallet is open it shows the progress, why it failed, or asks
/// for the recovery phrase.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    /// The window width past which Home lists the transactions too.
    static constexpr int wideWidth = 1200;

    explicit MainWindow(AppModel& model, QWidget* parent = nullptr);

    void openReceive();
    void openSend(const CoinSet& coins);
    ReceiveDialog* receiveDialog() const;
    SendDialog* sendDialog() const;

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildMenus();
    void buildReady();
    void phaseChanged();
    void selectionChanged();
    void refreshSidebarIcons();

    AppModel& model_;
    QStackedWidget* phases_;
    WrappingText* failure_;
    QWidget* ready_ = nullptr;
    QListWidget* sidebar_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* subtitle_ = nullptr;
    ConnectionStatus* connection_ = nullptr;
    QStackedWidget* sections_ = nullptr;
    HomeView* home_ = nullptr;
    SettingsView* settings_ = nullptr;
    Toast* toast_;
    QPointer<ReceiveDialog> receive_;
    QPointer<SendDialog> send_;
    QList<QAction*> walletActions_;
    QAction* copyAddress_ = nullptr;
};
