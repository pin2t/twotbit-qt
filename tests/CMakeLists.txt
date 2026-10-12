# The tests: crypto, wallet, chain, filters, wire, storage and end-to-end
# syncs against in-process mock nodes, checked against the vectors generated
# from bitfyn and btcd that the macOS app's tests use, and the app's model
# driven against a mock node.
set(app "${CMAKE_SOURCE_DIR}/src/app")
add_executable(twotbit_tests
    main.cpp
    fixtures.cpp
    mocknode.cpp
    tests.h
    test_app.cpp
    test_crypto.cpp
    test_format.cpp
    test_p2p.cpp
    test_spv.cpp
    test_store.cpp
    test_sync.cpp
    test_wallet.cpp
    "${app}/appmodel.cpp" "${app}/appmodel.h"
    "${app}/options.cpp" "${app}/options.h"
    "${app}/winauth.cpp" "${app}/winauth.h"
)
target_include_directories(twotbit_tests PRIVATE "${app}")
target_compile_definitions(twotbit_tests PRIVATE TWOTBIT_FIXTURES_DIR="${TWOTBIT_FIXTURES_DIR}")
target_compile_options(twotbit_tests PRIVATE -Wall -Wextra -Wno-missing-field-initializers -Wno-dangling-reference)
target_link_libraries(twotbit_tests PRIVATE twotbit_core Qt6::Gui Qt6::Widgets Qt6::Test ws2_32 credui runtimeobject)

# QtTest's plain text logger writes nothing to a redirected stdout on Windows;
# its TAP logger does.
add_test(NAME twotbit_tests COMMAND twotbit_tests -o -,tap)
set_tests_properties(twotbit_tests PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>\;$ENV{PATH}")
