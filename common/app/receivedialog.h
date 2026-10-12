#pragma once

#include <QDialog>
#include <QLabel>
#include <QLineEdit>

#include "appmodel.h"
#include "widgets.h"

/// ReceiveDialog is the invoice for the current address: an optional amount
/// and the BIP21 payment request as a QR code and as text.
class ReceiveDialog : public QDialog {
    Q_OBJECT
public:
    explicit ReceiveDialog(AppModel& model, QWidget* parent = nullptr);

private:
    void refresh();

    AppModel& model_;
    QLineEdit* amount_;
    SegmentedControl* unit_;
    QLabel* problem_;
    QRCodeView* qr_;
    WrappingText* uri_;
};
