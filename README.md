# Twotbit for Windows

A native Windows SPV Bitcoin wallet in C++ and Qt: BIP84 keys, a fully
validated block header chain, BIP157/158 compact block filters and a small
pool of P2P peers, reached directly.

It is the [macOS app](../macos) rebuilt for Windows. Its wallet engine is a
C++20 port of the Swift TwotbitKit, checked against the same bitfyn and btcd
vectors. The Qt Widgets window follows the SwiftUI one page by page. Tor and
I2P are not ported: peers are always dialed directly.

## Features

- **HD wallet**: a 12-word BIP39 recovery phrase and BIP84 native segwit
  (P2WPKH, bech32) receive and change addresses. A fresh address is shown
  once the current one is paid.
- **SPV header sync**: the header chain is downloaded and validated as
  Bitcoin Core validates headers — proof of work, the network's work limit,
  median time past, the future time limit and the exact difficulty of every
  header, testnet's 20-minute rule and testnet4's BIP94 included.
- **BIP157/158 compact filters**: filters are downloaded from up to three
  peers and cross-checked block by block. A disagreement is settled against
  the full block and the lying peer is dropped. The first filter header of
  the wallet's range is proven by a majority of up to ten peers. Matching
  blocks are downloaded and verified (merkle root, witness commitment), and
  filters are pruned after matching.
- **Mempool**: unconfirmed payments arrive through BIP37 bloom filters on
  peers that serve them. Otherwise one peer's relayed transactions are all
  checked.
- **Send**: coin selection or hand-picked coins, change, dust and fee
  handling, BIP69 ordering, RBF, RFC 6979 signatures with libsecp256k1, and
  rebroadcast until confirmed.
- **Peers**: a pool of three compact filter peers from DNS seeds and gossip
  (`addr` and `addrv2`), per-peer statistics, failover, and an optional
  pinned peer that headers and filters are always synced from.
- **Rates**: BTC/USD history since 2010 ships with the app. Fresh rates come
  from Coinbase, Kraken and Bitstamp.
- **Window**: Home (QR code, address, balance in BTC and USD, statistics),
  Coins (sortable table, multi-selection that Send starts from),
  Transactions (inputs → outputs, the wallet's own addresses in bold) and
  Settings. It uses the Windows 11 style with Bitcoin orange, in light and
  dark mode as Windows is set.

## Build and run

Requires Windows 10 or 11 and Qt 6.8 or later for MinGW, with CMake 3.21
or later and Ninja. The Qt online installer provides all of them; the app
was developed with Qt 6.12.0 and MinGW 13.1.

The build takes the files it shares with the macOS app from `../macos`:
libsecp256k1, the rate history, the test vectors and the icon images. Keep
both directories side by side, or point `-DTWOTBIT_MACOS_DIR` at the macOS
one.

In a Command Prompt, with the paths of your Qt installation:

```bat
set PATH=C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.12.0\mingw_64\bin;%PATH%
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/mingw_64
cmake --build build
build\Twotbit.exe -net signet
```

`cmake --build build --target deploy` collects a folder that runs on any
Windows 10 or 11 machine in `build\deploy`, about 40 MB: Twotbit.exe
without its debug information, and the Qt and MinGW libraries and plugins
that windeployqt finds it needs. Zip it or copy it as it is.

Options:

| option      | default                  | meaning |
|-------------|--------------------------|---------|
| `-net`      | `mainnet`                | `mainnet`, `testnet`, `testnet4`, `signet`, `regtest` or `simnet`; also chosen in Settings |
| `-datadir`  | `%LOCALAPPDATA%\Twotbit` | where the wallet databases and `twotbit.log` live |
| `-peer`     |                          | a `host:port` peer to pin the sync to, winning over the one saved in Settings |
| `-snapshot` |                          | saves every section and dialog as PNG images, light and dark, in the folder, and quits |

`-net`, `-datadir` and `-peer` can also be set as string values under the
registry key `HKEY_CURRENT_USER\Software\pin2t\Twotbit`, where Settings
saves the network. For example:

```bat
reg add HKCU\Software\pin2t\Twotbit /v net /d signet /f
```

Switching the network in Settings relaunches the app on the new network,
keeping a `-datadir` given on the command line. Only one Twotbit opens a
wallet at a time. A second one started on the same network and data folder
says that the wallet is already open and does not start.

### The recovery phrase and the Credential Manager

The recovery phrase is kept in the Windows Credential Manager, as the
generic credential `com.pin2t.twotbit:<account public key>` of your Windows
account. It is stored for this computer only and is not roamed to others.
The database holds only public data. Settings shows the phrase after
Windows Hello (face, fingerprint or PIN) or, where Hello is not set up,
after your Windows password. If the credential is ever lost, the app asks
for the 12 words to restore it. The local test networks, regtest and
simnet, keep their phrase in the database instead.

### Moving a wallet over

Twotbit uses bitfyn's database schema, as the macOS app does. To continue
with an unencrypted bitfyn wallet, quit both apps and copy its database to
the data folder under Twotbit's name: `bitfyn.db` to
`%LOCALAPPDATA%\Twotbit\twotbit.db`, `bitfyn-signet.db` to
`twotbit-signet.db`, and so on. On first open, the phrase moves from the
database into the Credential Manager. The synced headers, filters and
transactions carry over.

A database from the macOS app can be copied the same way. Its phrase
stayed in the Mac's Keychain, so on first open Twotbit asks for the 12
words.

## How it differs from the macOS app

- No Tor or I2P. Peers are dialed directly over TCP, onion and I2P
  addresses gossiped by peers are skipped, and there are no `-torproxy` and
  `-i2psam` options.
- The recovery phrase is in the Credential Manager instead of the Keychain,
  and Windows Hello or the account password take the place of Touch ID.
  Where Windows can check neither, as on an account without a password, the
  phrase is shown, as on a Mac without a password.
- The window is a Windows app: the sections in a sidebar, Receive and Send
  as dialogs, the connection status at the top right, and File, Wallet and
  Help menus. The shortcuts are Ctrl+, for Settings, Ctrl+Shift+R for
  Receive, Ctrl+Shift+S for Send and Ctrl+Shift+C to copy the address.
- Options persist in the registry instead of the user defaults.
- The wallet's database is locked to one running app. macOS keeps an app
  to one instance itself.
- SQLite is compiled in (the amalgamation in `third_party/sqlite`), where
  the Mac uses the system's.
- The engine runs its peers and syncs on threads that are cancelled through
  stop tokens, in place of Swift's structured concurrency, over Winsock.

## Layout

```
CMakeLists.txt          the build, taking the shared files from ../macos
common/                 what every platform builds
  core/                 the wallet engine, a C++20 port of TwotbitKit
    util                bytes, text, tasks and their cancellation, the log
    crypto              SHA-256/512, RIPEMD-160, HMAC, PBKDF2, SipHash, Murmur3, secp256k1
    encoding            Base58Check, Bech32/Bech32m
    bitcoin             networks, headers, transactions, blocks, scripts, addresses
    wallet              BIP39, BIP32, the BIP84 wallet, coin selection and signing
    spv                 the validated header chain, GCS and bloom filters
    p2p                 wire messages, peers, the TCP and DNS interface
    sync                the peer pool, header, filter and wallet sync, rescans
    storage             SQLite store, the secret store interface, wallet loading
    rates, format       BTC/USD rates; amounts, times, statistics, QR codes
  app/                  the Qt Widgets app: AppModel (the GUI logic), the views
                        and dialogs, the theme
windows/                what is Windows' own, added to the common targets
  core/p2p/tcp.cpp      TCP and DNS over Winsock
  core/storage          the Credential Manager secret store
  app/                  Windows Hello and the password before the phrase is shown
    resources           the icon, version information and manifest
  scripts/make-icon.ps1 packs the macOS app's icon images into twotbit.ico
tests/                  the QtTest suites, the fixture reader, a mock Bitcoin node
third_party/sqlite      SQLite 3.53.4, the amalgamation
```

## Tests

`ctest --test-dir build` runs 85 tests in about eight seconds:

- crypto, encoding, BIP39/BIP32/BIP84, coin selection and signing against
  vectors generated from bitfyn and btcd. Signed transactions match bitfyn
  byte for byte.
- network parameters, difficulty and retargets, the first 4100 mainnet
  headers, GCS filters of real blocks, bloom filters, block verification
  and every wire message btcd writes.
- the database, wallet loading, the move of bitfyn's phrase out of its
  database, and the Credential Manager.
- end-to-end syncs against in-process mock Bitcoin nodes over local TCP:
  payments and spends, a lying peer outvoted by the full block, relayed and
  broadcast transactions, reorganisations, the filter anchor majority,
  rescans and pinned peers.
- the app's model opening, syncing and paying from a wallet against a mock
  node.

`build\twotbit_tests.exe` takes the suites to run first, then QtTest's
options, such as `build\twotbit_tests.exe SyncTests fullSync`. QtTest's
plain text output stays empty when redirected on Windows, so add
`-o -,tap` to capture it.

Opt-in tests, set by environment variables:

```bat
rem validate whole stored chains, read-only, such as bitfyn's
set TWOTBIT_CHAIN_DB=mainnet=C:\wallets\bitfyn.db
build\twotbit_tests.exe StoreTests realChains
rem sync a fresh wallet from the real network
set TWOTBIT_LIVE=signet
build\twotbit_tests.exe SyncTests liveSync
rem build a demo regtest wallet to open with -net regtest -datadir <dir>
set TWOTBIT_DEMO_DIR=%TEMP%\twotbit-demo
build\twotbit_tests.exe SyncTests demoWallet
rem save QR codes of every version and mask as images, to check with a scanner
set TWOTBIT_QR_DIR=%TEMP%\twotbit-qr
build\twotbit_tests.exe FormatTests qrCodes
```

The log goes to `twotbit.log` in the data folder and to the debugger
output, where DebugView shows it. `TWOTBIT_LOG_STDERR=1` also echoes it to
standard error. With `-snapshot`, `TWOTBIT_SNAPSHOT_DELAY` sets how many
seconds the wallet syncs before the images are taken (2 by default).
Configuring with `-DTWOTBIT_SNAPSHOT=OFF` leaves `-snapshot` out of the
app.

## License

MIT, see [LICENSE](../LICENSE). libsecp256k1 is MIT licensed too, see
`macos/Core/Sources/CSecp256k1/COPYING`. SQLite is in the public domain.
