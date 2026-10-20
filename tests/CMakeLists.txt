# The tests: crypto, wallet, chain, filters, wire, storage and end-to-end
# syncs against in-process mock nodes, checked against the vectors generated
# from bitfyn and btcd that the macOS app's tests use, and the app's model
# driven against a mock node.
set(app "${CMAKE_SOURCE_DIR}/common/app")
add_executable(twotbit_tests
    main.cpp
    fixtures.cpp
    mocknode.cpp
    sockets.h
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
)
target_include_directories(twotbit_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${app}")
target_compile_definitions(twotbit_tests PRIVATE TWOTBIT_FIXTURES_DIR="${TWOTBIT_FIXTURES_DIR}")
target_compile_options(twotbit_tests PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(twotbit_tests PRIVATE -Wno-dangling-reference)
endif()
target_link_libraries(twotbit_tests PRIVATE twotbit_core Qt6::Gui Qt6::Widgets Qt6::Test)

# The platform's directory adds its own suites and the local sockets the
# mock nodes listen on.
if(WIN32)
    set(windows "${CMAKE_SOURCE_DIR}/windows/tests")
    target_sources(twotbit_tests PRIVATE
        "${windows}/sockets.cpp"
        "${windows}/test_windows.cpp" "${windows}/test_windows.h"
    )
    target_include_directories(twotbit_tests PRIVATE "${windows}")
    target_link_libraries(twotbit_tests PRIVATE twotbit_windows ws2_32)
elseif(APPLE)
    set(macos "${CMAKE_SOURCE_DIR}/macos/tests")
    target_sources(twotbit_tests PRIVATE
        "${macos}/sockets.cpp"
        "${macos}/test_macos.cpp" "${macos}/test_macos.h"
    )
    target_include_directories(twotbit_tests PRIVATE "${macos}")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(linux "${CMAKE_SOURCE_DIR}/linux/tests")
    target_sources(twotbit_tests PRIVATE
        "${linux}/sockets.cpp"
        "${linux}/test_linux.cpp" "${linux}/test_linux.h"
    )
    target_include_directories(twotbit_tests PRIVATE "${linux}")
endif()

# QtTest's plain text logger writes nothing to a redirected stdout on Windows;
# its TAP logger does.
add_test(NAME twotbit_tests COMMAND twotbit_tests -o -,tap)
set_tests_properties(twotbit_tests PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>\;$ENV{PATH}")
