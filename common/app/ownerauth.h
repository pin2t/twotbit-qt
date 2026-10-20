#pragma once

#include <QString>

class QWidget;

/// OwnerAuth asks the person at the computer to confirm they own this
/// account before the recovery phrase is shown, as the platform checks it:
/// windows/app/winauth.cpp on Windows, macos/app/macauth.mm on the Mac,
/// linux/app/polkitauth.cpp on Linux.
namespace OwnerAuth {

/// Whether the owner confirmed. Where the platform can check nothing, the
/// phrase is shown.
bool confirmOwner(QWidget* parent, const QString& reason);

} // namespace OwnerAuth
