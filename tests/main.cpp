#include <QCoreApplication>
#include <QTest>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "tests.h"

#ifdef _WIN32
#include "test_windows.h"
#elif defined(__APPLE__)
#include "test_macos.h"
#elif defined(__linux__)
#include "test_linux.h"
#endif

// Runs every suite, or the ones named on the command line before the QtTest
// options, as `twotbit_tests SyncTests fullSync -o -,tap`.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::vector<std::unique_ptr<QObject>> suites;
    suites.push_back(std::make_unique<tb::test::CryptoTests>());
    suites.push_back(std::make_unique<tb::test::WalletTests>());
    suites.push_back(std::make_unique<tb::test::ChainTests>());
    suites.push_back(std::make_unique<tb::test::WireTests>());
    suites.push_back(std::make_unique<tb::test::StoreTests>());
    suites.push_back(std::make_unique<tb::test::FormatTests>());
    suites.push_back(std::make_unique<tb::test::SyncTests>());
    suites.push_back(std::make_unique<tb::test::AppTests>());
#ifdef _WIN32
    suites.push_back(std::make_unique<tb::test::WindowsTests>());
#elif defined(__APPLE__)
    suites.push_back(std::make_unique<tb::test::MacTests>());
#elif defined(__linux__)
    suites.push_back(std::make_unique<tb::test::LinuxTests>());
#endif

    std::vector<char*> args{argv[0]};
    std::vector<std::string> only;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-' && args.size() == 1 && std::string(argv[i]).ends_with("Tests"))
            only.emplace_back(argv[i]);
        else
            args.push_back(argv[i]);
    }
    int failed = 0;
    for (auto& suite : suites) {
        std::string name = suite->metaObject()->className();
        name = name.substr(name.rfind(':') + 1);
        if (!only.empty() && std::find(only.begin(), only.end(), name) == only.end()) continue;
        failed += QTest::qExec(suite.get(), int(args.size()), args.data());
    }
    return failed;
}
