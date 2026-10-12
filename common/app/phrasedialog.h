#pragma once

#include <QDialog>
#include <QStringList>

/// PhraseDialog shows the 12 words that restore the wallet.
class PhraseDialog : public QDialog {
    Q_OBJECT
public:
    PhraseDialog(const QStringList& words, QWidget* parent = nullptr);
};
