#include "snapshot.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QLineEdit>
#include <QStyleHints>
#include <QTimer>

#include "mainwindow.h"
#include "receivedialog.h"
#include "senddialog.h"

namespace Snapshot {

namespace {

/// Runs the event loop for a while, as the window settles.
void settle(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void save(QWidget* widget, const QString& path) {
    if (widget) widget->grab().save(path);
}

} // namespace

void run(MainWindow& window, AppModel& model, const QString& directory) {
    QDir().mkpath(directory);
    // TWOTBIT_SNAPSHOT_DELAY sets how many seconds the wallet syncs first.
    int delay = qEnvironmentVariableIsSet("TWOTBIT_SNAPSHOT_DELAY") ? qEnvironmentVariableIntValue("TWOTBIT_SNAPSHOT_DELAY") * 1000 : 2000;
    QTimer::singleShot(delay, &window, [&window, &model, directory] {
        const AppModel::Section all[] = {AppModel::Section::home, AppModel::Section::coins, AppModel::Section::transactions, AppModel::Section::settings};
        const char* names[] = {"home", "coins", "transactions", "settings"};
        for (Qt::ColorScheme scheme : {Qt::ColorScheme::Light, Qt::ColorScheme::Dark}) {
            QGuiApplication::styleHints()->setColorScheme(scheme);
            settle(600);
            QString suffix = scheme == Qt::ColorScheme::Dark ? "-dark" : "";
            for (int i = 0; i < 4; ++i) {
                model.select(all[i]);
                settle(900);
                save(&window, QDir(directory).filePath(QString(names[i]) + suffix + ".png"));
            }
            model.select(AppModel::Section::home);
            QSize normal = window.size();
            window.resize(1320, normal.height());
            settle(900);
            save(&window, QDir(directory).filePath("home-wide" + suffix + ".png"));
            window.resize(normal);
            model.show("Address copied");
            settle(700);
            save(&window, QDir(directory).filePath("toast" + suffix + ".png"));
            model.openReceive();
            settle(900);
            save(window.receiveDialog(), QDir(directory).filePath("receive" + suffix + ".png"));
            if (auto* amount = window.receiveDialog() ? window.receiveDialog()->findChild<QLineEdit*>() : nullptr) {
                amount->setText("12345");
                settle(600);
                save(window.receiveDialog(), QDir(directory).filePath("receive-amount" + suffix + ".png"));
            }
            if (window.receiveDialog()) window.receiveDialog()->close();
            settle(600);
            model.openSend();
            settle(900);
            save(window.sendDialog(), QDir(directory).filePath("send" + suffix + ".png"));
            if (window.sendDialog()) window.sendDialog()->close();
            settle(600);
        }
        window.close();
    });
}

} // namespace Snapshot
