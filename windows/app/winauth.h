#pragma once

#include <QString>

class QWidget;

/// WinAuth asks the person at the computer to confirm they own this Windows
/// account before the recovery phrase is shown: with Windows Hello, its
/// face, fingerprint or PIN, where it is set up, and with the account's
/// password otherwise.
namespace WinAuth {

/// Whether the owner confirmed. When Windows can check neither, as on an
/// account without a password, the phrase is shown.
bool confirmOwner(QWidget* parent, const QString& reason);

/// The IID of the WinRT parameterized interface with the signature, as
/// WinRT derives it: a version 5 UUID in the pinterface namespace.
QString pinterfaceIID(const QString& signature);

} // namespace WinAuth
