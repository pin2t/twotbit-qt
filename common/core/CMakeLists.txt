# twotbit_core is the wallet engine: BIP39/BIP84 keys, the validated SPV
# header chain, BIP157/158 compact filters, the P2P peer pool and the wallet
# sync, storage and rates. The app only adds the Qt Widgets interface. The
# platform's directory adds the TCP and DNS of the P2P layer and the
# credential store of the recovery phrase.
add_library(twotbit_core STATIC
    util/bytes.cpp
    util/log.cpp
    util/task.cpp
    crypto/hashes.cpp
    crypto/secp256k1.cpp
    encoding/encodings.cpp
    bitcoin/network.cpp
    bitcoin/primitives.cpp
    bitcoin/script.cpp
    bitcoin/uint256.cpp
    wallet/bip39.cpp
    wallet/bip39words.cpp
    wallet/hdwallet.cpp
    wallet/spend.cpp
    spv/filters.cpp
    spv/headerchain.cpp
    p2p/hostport.cpp
    p2p/messages.cpp
    p2p/peer.cpp
    storage/secrets.cpp
    storage/sqlite.cpp
    storage/store.cpp
    sync/engine.cpp
    sync/filtersync.cpp
    sync/headers.cpp
    sync/pool.cpp
    sync/probe.cpp
    sync/requests.cpp
    sync/rescan.cpp
    sync/walletsync.cpp
    rates/rates.cpp
    format/format.cpp
    format/qrcode.cpp
)

target_include_directories(twotbit_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(twotbit_core PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
target_link_libraries(twotbit_core
    PUBLIC Qt6::Core Qt6::Network
    PRIVATE secp256k1 sqlite3
)

# The BTC/USD rate history since 2010, shared with the macOS app.
qt_add_resources(twotbit_core "rates"
    PREFIX "/"
    BASE "${TWOTBIT_MACOS_DIR}/Core/Sources/TwotbitKit/Resources"
    FILES "${TWOTBIT_RATES_CSV}"
)
