#pragma once

#include <QObject>
#include <QTest>

#include "fixtures.h"

namespace tb::test {

/// LinuxTests checks what is Linux's own: the recovery phrase's item in the
/// Secret Service keyring.
class LinuxTests : public QObject {
    Q_OBJECT
private slots:
    void keyring();
};

} // namespace tb::test
