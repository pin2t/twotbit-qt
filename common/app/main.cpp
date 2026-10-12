#include <QApplication>
#include <QIcon>
#include <QStyleFactory>

#include "appmodel.h"
#include "mainwindow.h"
#include "options.h"
#include "theme.h"

#ifdef TWOTBIT_SNAPSHOT
#include "snapshot.h"
#endif

// Twotbit is an SPV Bitcoin wallet: BIP84 keys, a validated header chain,
// BIP157/158 compact block filters and a small pool of P2P peers, reached
// directly.
int main(int argc, char* argv[]) {
    QApplication::setApplicationName("Twotbit");
    QApplication::setApplicationVersion(TWOTBIT_VERSION);
    QApplication::setOrganizationName("pin2t");
    QApplication::setOrganizationDomain("pin2t.com");
    QApplication app(argc, argv);
    // The Windows 11 look, light and dark, on Windows 10 too.
    if (QStyleFactory::keys().contains("windows11", Qt::CaseInsensitive)) QApplication::setStyle("windows11");
    QIcon icon;
    for (int size : {16, 32, 64, 128, 256, 512}) icon.addFile(QString(":/icons/icon_%1.png").arg(size), QSize(size, size));
    app.setWindowIcon(icon);
    Theme::install(app);

    LaunchOptions options = LaunchOptions::load(app.arguments());
    AppModel model(options);
    MainWindow window(model);
    window.show();
    model.start();
#ifdef TWOTBIT_SNAPSHOT
    if (!options.snapshot.isEmpty()) Snapshot::run(window, model, options.snapshot);
#endif
    QObject::connect(&app, &QApplication::aboutToQuit, &model, [&model] { model.shutdown(); });
    return app.exec();
}
