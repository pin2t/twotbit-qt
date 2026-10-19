# What makes the common engine and app a Mac app: TCP and DNS over BSD
# sockets, the recovery phrase in the login keychain, Touch ID or the
# account password before it is shown, and the app bundle with its icon and
# Info.plist.
enable_language(OBJCXX)

target_sources(twotbit_core PRIVATE
    core/p2p/tcp.cpp
    core/storage/keychain.cpp
)
target_link_libraries(twotbit_core PRIVATE "-framework Security" "-framework CoreFoundation")

# The owner check and the SF Symbols the icons are drawn from, a library as
# the windows/ one is.
add_library(twotbit_macos STATIC
    app/macauth.mm
    app/symbols.mm
)
target_include_directories(twotbit_macos PRIVATE "${CMAKE_SOURCE_DIR}/common/app")
target_compile_options(twotbit_macos PRIVATE -Wall -Wextra -fobjc-arc)
target_link_libraries(twotbit_macos PUBLIC twotbit_core Qt6::Widgets "-framework Foundation" "-framework AppKit" "-framework LocalAuthentication")

target_link_libraries(Twotbit PRIVATE twotbit_macos)

# The app icon, packed by iconutil from the images the Swift app ships.
set(iconset "${CMAKE_CURRENT_BINARY_DIR}/Twotbit.iconset")
set(icns "${CMAKE_CURRENT_BINARY_DIR}/Twotbit.icns")
set(icons "${TWOTBIT_MACOS_DIR}/App/Assets.xcassets/AppIcon.appiconset")
set(iconImages)
foreach(size IN ITEMS 16 32 128 256 512)
    list(APPEND iconImages "${icons}/icon_${size}x${size}.png" "${icons}/icon_${size}x${size}@2x.png")
endforeach()
add_custom_command(OUTPUT "${icns}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${iconset}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${iconset}"
    COMMAND ${CMAKE_COMMAND} -E copy ${iconImages} "${iconset}"
    COMMAND iconutil --convert icns --output "${icns}" "${iconset}"
    DEPENDS ${iconImages}
    COMMENT "Packing the app icon into Twotbit.icns"
    VERBATIM
)
target_sources(Twotbit PRIVATE "${icns}")
set_source_files_properties("${icns}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

set_target_properties(Twotbit PROPERTIES
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/app/resources/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "Twotbit"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.pin2t.twotbit.qt"
    MACOSX_BUNDLE_ICON_FILE "Twotbit.icns"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "Copyright © 2026 Ilya Pokolev. MIT License."
)

# A Twotbit.app that runs on any Mac with the system Qt was built for: the
# bundle with the Qt frameworks and plugins it loads copied in by
# macdeployqt, signed ad hoc, and a disk image of it.
find_program(MACDEPLOYQT macdeployqt HINTS "${QT6_INSTALL_PREFIX}/bin")
set(TWOTBIT_DEPLOY_DIR "${CMAKE_BINARY_DIR}/deploy" CACHE PATH "Where the deploy target puts the runnable app")
add_custom_target(deploy
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${TWOTBIT_DEPLOY_DIR}/Twotbit.app" "${TWOTBIT_DEPLOY_DIR}/Twotbit.dmg"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TWOTBIT_DEPLOY_DIR}"
    COMMAND ditto "$<TARGET_BUNDLE_DIR:Twotbit>" "${TWOTBIT_DEPLOY_DIR}/Twotbit.app"
    COMMAND "${MACDEPLOYQT}" "${TWOTBIT_DEPLOY_DIR}/Twotbit.app" -dmg -codesign=-
    DEPENDS Twotbit
    COMMENT "Collecting Twotbit.app and the frameworks it needs in ${TWOTBIT_DEPLOY_DIR}"
    VERBATIM
)
