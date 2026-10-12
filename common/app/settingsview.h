#pragma once

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QWidget>

#include "appmodel.h"
#include "widgets.h"

/// SettingsView is the pinned peer and the wallet: its network, data
/// folder, account key and recovery phrase.
class SettingsView : public QWidget {
    Q_OBJECT
public:
    explicit SettingsView(AppModel& model, QWidget* parent = nullptr);

    /// Fills in the values of the opened wallet.
    void refresh();

private:
    void refreshPeer();
    void networkPicked(int index);
    void reveal();

    AppModel& model_;
    QLineEdit* peer_;
    QLabel* peerStatus_;
    QVBoxLayout* notes_;
    QComboBox* network_;
    ElidedLabel* folder_;
    ElidedLabel* xpub_;
};
