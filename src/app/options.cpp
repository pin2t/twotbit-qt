#include "options.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace {

/// The value of the option in the arguments: "-name value", "--name value"
/// or "-name=value", the last one given winning.
QString argument(const QStringList& arguments, const QString& name, bool* found) {
    QString value;
    *found = false;
    for (int i = 1; i < arguments.size(); ++i) {
        QString a = arguments[i];
        if (a.startsWith("--")) a.remove(0, 1);
        if (a == "-" + name && i + 1 < arguments.size()) {
            value = arguments[++i];
            *found = true;
        } else if (a.startsWith("-" + name + "=")) {
            value = a.mid(name.size() + 2);
            *found = true;
        }
    }
    return value;
}

QString expandHome(const QString& path) {
    if (path == "~" || path.startsWith("~/") || path.startsWith("~\\")) return QDir::homePath() + path.mid(1);
    return path;
}

} // namespace

QString LaunchOptions::defaultDataDirectory() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath("Twotbit");
}

LaunchOptions LaunchOptions::load(const QStringList& arguments) {
    QSettings settings;
    LaunchOptions o;
    bool found;
    QString net = argument(arguments, "net", &found);
    if (!found) net = settings.value("net").toString();
    if (const tb::Network* n = tb::Network::named(net.toStdString())) o.network = n;
    QString dir = argument(arguments, "datadir", &found);
    o.dataDirectoryFromFlag = found && !dir.isEmpty();
    if (!found) dir = settings.value("datadir").toString();
    o.dataDirectory = QDir::cleanPath(dir.isEmpty() ? defaultDataDirectory() : QDir(expandHome(dir)).absolutePath());
    o.peer = argument(arguments, "peer", &found);
    if (!found) o.peer = settings.value("peer").toString();
    o.peer = o.peer.trimmed();
    o.snapshot = argument(arguments, "snapshot", &found);
    return o;
}

QStringList LaunchOptions::relaunchArguments() const {
    if (dataDirectoryFromFlag) return {"-datadir", QDir::toNativeSeparators(dataDirectory)};
    return {};
}
