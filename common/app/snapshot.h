#pragma once

#include <QString>

class AppModel;
class MainWindow;

/// Snapshot renders the window's sections and dialogs to PNG files, in
/// light and dark, and quits, when the app is launched with -snapshot
/// <directory>: a quick look at the interface.
namespace Snapshot {

void run(MainWindow& window, AppModel& model, const QString& directory);

} // namespace Snapshot
