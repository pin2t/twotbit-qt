#pragma once

#include <QObject>
#include <QTest>

#include "fixtures.h"

namespace tb::test {

/// MacTests checks what is the Mac's own: the recovery phrase's generic
/// password in the login keychain.
class MacTests : public QObject {
    Q_OBJECT
private slots:
    void keychain();
};

} // namespace tb::test
