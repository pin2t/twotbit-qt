#pragma once

#include <QString>
#include <QStringList>

#include "bitcoin/network.h"

/// LaunchOptions are the command line options, as bitfyn's flags: -net,
/// -datadir and -peer, each also read from the saved settings, where -net
/// also persists the network chosen in the settings: the registry key
/// HKEY_CURRENT_USER\Software\pin2t\Twotbit. -snapshot saves the window's
/// sections as images and quits.
struct LaunchOptions {
    const tb::Network* network = &tb::Network::mainnet();
    QString dataDirectory;
    QString peer;
    QString snapshot;
    /// Whether -datadir was given on the command line, which a relaunch on
    /// another network keeps.
    bool dataDirectoryFromFlag = false;

    static LaunchOptions load(const QStringList& arguments);

    /// The default data directory: Twotbit in the local application data
    /// folder.
    static QString defaultDataDirectory();

    /// The arguments to relaunch the app with on another network: the data
    /// directory when it was given, the network itself coming from the saved
    /// settings.
    QStringList relaunchArguments() const;
};
