#include "ownerauth.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QEventLoop>
#include <QWidget>

#include <climits>

#include "util/log.h"

namespace polkit {

/// The subject of an authorization check, polkit's (sa{sv}): this process,
/// named by its connection to the system bus.
struct Subject {
    QString kind;
    QVariantMap details;
};

using Details = QMap<QString, QString>;

QDBusArgument& operator<<(QDBusArgument& argument, const Subject& subject) {
    argument.beginStructure();
    argument << subject.kind << subject.details;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, Subject& subject) {
    argument.beginStructure();
    argument >> subject.kind >> subject.details;
    argument.endStructure();
    return argument;
}

/// The action the policy in linux/app/resources lets the active user take
/// after entering their own password.
constexpr const char* action = "com.pin2t.twotbit.show-recovery-phrase";

/// CheckAuthorization's flag that lets polkit ask the user.
constexpr uint allowUserInteraction = 1;

} // namespace polkit

Q_DECLARE_METATYPE(polkit::Subject)

namespace OwnerAuth {

// polkit asks for the account's own password, or a fingerprint where one is
// enrolled, in the desktop's authentication dialog: GNOME Shell's on GNOME.
// The dialog shows the message of the installed action, not the reason,
// which only root may replace. Where polkit can check nothing, as when it
// is missing or the action was not installed, the phrase is shown, as on a
// Mac or Windows that cannot check the owner.
bool confirmOwner(QWidget* parent, const QString& reason) {
    Q_UNUSED(reason);
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        tb::Log::info("settings: owner check unavailable, no system bus: " + bus.lastError().message().toStdString());
        return true;
    }
    qDBusRegisterMetaType<polkit::Subject>();
    qDBusRegisterMetaType<polkit::Details>();
    QDBusMessage call = QDBusMessage::createMethodCall("org.freedesktop.PolicyKit1", "/org/freedesktop/PolicyKit1/Authority",
                                                       "org.freedesktop.PolicyKit1.Authority", "CheckAuthorization");
    polkit::Subject subject{"system-bus-name", {{"name", bus.baseService()}}};
    call << QVariant::fromValue(subject) << QString(polkit::action) << QVariant::fromValue(polkit::Details()) << polkit::allowUserInteraction
         << QString();

    QWidget* top = parent ? parent->window() : nullptr;
    if (top) top->setEnabled(false);
    // The answer comes once the dialog is closed, however long it stays
    // open; the window keeps painting meanwhile.
    QDBusPendingCallWatcher watcher(bus.asyncCall(call, INT_MAX));
    if (!watcher.isFinished()) {
        QEventLoop loop;
        QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    if (top) {
        top->setEnabled(true);
        top->activateWindow();
    }

    QDBusMessage reply = watcher.reply();
    if (reply.type() == QDBusMessage::ErrorMessage) {
        tb::Log::info("settings: owner check unavailable, " + reply.errorName().toStdString() + ": " + reply.errorMessage().toStdString());
        return true;
    }
    if (reply.arguments().isEmpty()) return false;
    // The result, (bba{ss}): whether the user is authorized, whether they
    // could be after authenticating, and details.
    bool authorized = false;
    bool challenge = false;
    polkit::Details details;
    const QDBusArgument result = reply.arguments().first().value<QDBusArgument>();
    result.beginStructure();
    result >> authorized >> challenge >> details;
    result.endStructure();
    if (!authorized) tb::Log::info(details.value("polkit.dismissed") == "true" ? "settings: owner check dismissed" : "settings: owner check refused");
    return authorized;
}

} // namespace OwnerAuth
