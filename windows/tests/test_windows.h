#pragma once

#include <QObject>
#include <QTest>

#include "fixtures.h"

namespace tb::test {

/// WindowsTests checks what is Windows' own: the Credential Manager and
/// the WinRT interface IDs Windows Hello is reached through.
class WindowsTests : public QObject {
    Q_OBJECT
private slots:
    void credentialManager();
    void pinterfaceIID();
};

} // namespace tb::test
