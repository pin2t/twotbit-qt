# libsecp256k1 v0.6.0, vendored unchanged in the macOS tree: the ECDSA
# signing and key arithmetic Bitcoin Core uses.
add_library(secp256k1 STATIC
    "${TWOTBIT_SECP256K1_DIR}/src/secp256k1.c"
    "${TWOTBIT_SECP256K1_DIR}/src/precomputed_ecmult.c"
    "${TWOTBIT_SECP256K1_DIR}/src/precomputed_ecmult_gen.c"
)
target_include_directories(secp256k1 PUBLIC "${TWOTBIT_SECP256K1_DIR}/include" PRIVATE "${TWOTBIT_SECP256K1_DIR}/src")
# Consumers of the static library on Windows must say so, or the header
# declares the functions as DLL imports.
target_compile_definitions(secp256k1 PUBLIC SECP256K1_STATIC)
target_compile_options(secp256k1 PRIVATE -w)

# SQLite 3.53.4, the public domain amalgamation from sqlite.org. The wallet
# database is shared between threads behind the store's lock, which the
# connections of QtSql do not allow.
add_library(sqlite3 STATIC sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC sqlite)
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_DQS=0
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_OMIT_DEPRECATED
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_OMIT_SHARED_CACHE
    SQLITE_LIKE_DOESNT_MATCH_BLOBS
)
target_compile_options(sqlite3 PRIVATE -w)
