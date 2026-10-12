#pragma once

#include <QObject>
#include <QTest>

#include "fixtures.h"

namespace tb::test {

class CryptoTests : public QObject {
    Q_OBJECT
private slots:
    void ripemd160();
    void sha256AndSha512();
    void sipHash();
    void murmur3();
    void pbkdf2AndHMAC();
    void secp256k1SignAndVerify();
    void base58();
    void bech32();
    void varInts();
    void uint256AndCompact();
};

class WalletTests : public QObject {
    Q_OBJECT
private slots:
    void wordlist();
    void mnemonics();
    void derivation();
    void bip84Vectors();
    void paths();
    void spends();
    void sizesAndDust();
    void addressScripts();
    void bip143Sighash();
};

class ChainTests : public QObject {
    Q_OBJECT
private slots:
    void networkParameters();
    void compactBits();
    void retargets();
    void realMainnetHeaders();
    void chainAdd();
    void powLimit();
    void bitsWithinPeriod();
    void testnetMinDifficulty();
    void testnetDifficultyAtLimit();
    void retargetBIP94();
    void firstHeightAtOrAfter();
    void heightIndex();
    void gcsVectors();
    void realBlockFilters();
    void emptyFilter();
    void bloomVectors();
    void blocks();
};

class WireTests : public QObject {
    Q_OBJECT
private slots:
    void roundTrips();
    void versionMessages();
    void remoteVersion();
    void requests();
    void addresses();
    void hostPorts();
    void ipAddresses();
    void overlayHosts();
    void tcpStream();
};

class StoreTests : public QObject {
    Q_OBJECT
private slots:
    void metaAndAddresses();
    void headersFiltersAndRewind();
    void transactionsAndPending();
    void rescansPeersSettingsRates();
    void walletLoader();
    void localNetworkPhrase();
    void bitfynDatabase();
    void realChains();
};

class FormatTests : public QObject {
    Q_OBJECT
private slots:
    void amounts();
    void balanceAndPending();
    void parseAmounts();
    void invoices();
    void usd();
    void netAmounts();
    void relativeTimes();
    void lifetimes();
    void fitAddresses();
    void stats();
    void peers();
    void probes();
    void rates();
    void qrCodes();
};

class AppTests : public QObject {
    Q_OBJECT
private slots:
    void launchOptions();
    void sendFlow();
};

class SyncTests : public QObject {
    Q_OBJECT
private slots:
    void spentAddresses();
    void historiesAndBalances();
    void majorityAnchors();
    void statusText();
    void fullSync();
    void crossCheckAndMismatch();
    void relayBroadcastConfirm();
    void reorgs();
    void filterAnchor();
    void watchAndRescan();
    void pinnedPeerAndServices();
    void stopIsPrompt();
    void demoWallet();
    void liveSync();
};

} // namespace tb::test
