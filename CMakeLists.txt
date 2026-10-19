# Twotbit: the Qt Widgets app over the wallet engine library. common/ holds
# the engine and the app every platform builds; windows/ and macos/ add what
# is each platform's own.
#
#   cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/mingw_64
#   cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=~/Qt/6.12.0/macos
#   cmake --build build
#   ctest --test-dir build
cmake_minimum_required(VERSION 3.21)

project(Twotbit VERSION 0.1.0 DESCRIPTION "SPV Bitcoin wallet" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# What this app shares with the Swift macOS one, used from its tree rather
# than copied: libsecp256k1 as vendored there, the shipped BTC/USD rate
# history, the test vectors generated from bitfyn and the icon images. It is
# looked for next to this directory as macos or twotbit-macos.
set(defaultMacosDir "${CMAKE_CURRENT_SOURCE_DIR}/../macos")
if(NOT EXISTS "${defaultMacosDir}" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../twotbit-macos")
    set(defaultMacosDir "${CMAKE_CURRENT_SOURCE_DIR}/../twotbit-macos")
endif()
set(TWOTBIT_MACOS_DIR "${defaultMacosDir}" CACHE PATH "The Swift macOS app's directory")
set(TWOTBIT_SECP256K1_DIR "${TWOTBIT_MACOS_DIR}/Core/Sources/CSecp256k1")
set(TWOTBIT_RATES_CSV "${TWOTBIT_MACOS_DIR}/Core/Sources/TwotbitKit/Resources/rates.csv")
set(TWOTBIT_FIXTURES_DIR "${TWOTBIT_MACOS_DIR}/Core/Tests/TwotbitKitTests/Fixtures")
foreach(shared IN ITEMS "${TWOTBIT_SECP256K1_DIR}/src/secp256k1.c" "${TWOTBIT_RATES_CSV}")
    if(NOT EXISTS "${shared}")
        message(FATAL_ERROR "${shared} is missing: TWOTBIT_MACOS_DIR must point at the Swift macOS app's directory")
    endif()
endforeach()

option(TWOTBIT_BUILD_TESTS "Build the wallet engine's tests" ON)
option(TWOTBIT_SNAPSHOT "Let the app take -snapshot <dir> to save its sections as PNG images" ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network)
qt_standard_project_setup()

add_subdirectory(third_party)
add_subdirectory(common/core)
add_subdirectory(common/app)
if(WIN32)
    add_subdirectory(windows)
elseif(APPLE)
    add_subdirectory(macos)
endif()

if(TWOTBIT_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Gui Test)
    enable_testing()
    add_subdirectory(tests)
endif()
