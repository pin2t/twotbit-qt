# What makes the common engine and app a Linux app: TCP and DNS over
# sockets, the recovery phrase in the Secret Service keyring that GNOME
# Keyring keeps, polkit's check of the account's password before it is
# shown, and the desktop entry and icons that list it among the GNOME
# applications.
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET GLOBAL libsecret-1)
find_package(Qt6 REQUIRED COMPONENTS DBus)
include(GNUInstallDirs)

target_sources(twotbit_core PRIVATE
    core/p2p/tcp.cpp
    core/storage/secretservice.cpp
)
target_link_libraries(twotbit_core PRIVATE PkgConfig::LIBSECRET)

# The owner check, a library as the windows/ and macos/ ones are.
add_library(twotbit_linux STATIC
    app/polkitauth.cpp
)
target_include_directories(twotbit_linux PRIVATE "${CMAKE_SOURCE_DIR}/common/app")
target_compile_options(twotbit_linux PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
target_link_libraries(twotbit_linux PUBLIC twotbit_core Qt6::Widgets Qt6::DBus)

target_link_libraries(Twotbit PRIVATE twotbit_linux)

# A lower case command, as Linux names them. The Qt libraries are looked for
# next to it first, where the deploy target puts them, then where it was
# linked against them.
set_target_properties(Twotbit PROPERTIES
    OUTPUT_NAME twotbit
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
    INSTALL_RPATH_USE_LINK_PATH ON
)

# `cmake --install build` puts the app where the desktop finds it: the
# command, its desktop entry, its icon in every size the macOS app ships,
# and the polkit action of the owner check. polkit reads actions only from
# /usr/share/polkit-1/actions, so the prefix must be /usr for it to take
# effect, or the file is copied there.
install(TARGETS Twotbit RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES app/resources/com.pin2t.Twotbit.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES app/resources/com.pin2t.twotbit.policy DESTINATION "${CMAKE_INSTALL_DATADIR}/polkit-1/actions")
set(icons "${TWOTBIT_MACOS_DIR}/App/Assets.xcassets/AppIcon.appiconset")
foreach(icon IN ITEMS 16x16:16x16 32x32:32x32 32x32@2x:64x64 128x128:128x128 256x256:256x256 512x512:512x512)
    string(REPLACE ":" ";" icon "${icon}")
    list(GET icon 0 image)
    list(GET icon 1 size)
    install(FILES "${icons}/icon_${image}.png" DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}/apps" RENAME com.pin2t.Twotbit.png)
endforeach()

# A folder that runs on any recent x86-64 Linux desktop, and a tarball of
# it: the app installed as above, with the Qt libraries and plugins it loads
# copied in by Qt's deployment script, and install.sh, which adds it to the
# user's applications. Only the deploy target installs the "bundle" part.
qt_generate_deploy_app_script(TARGET Twotbit OUTPUT_SCRIPT deployScript NO_TRANSLATIONS NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT "${deployScript}" COMPONENT bundle EXCLUDE_FROM_ALL)
# The script copies only the default platform plugin, xcb's. Wayland's runs
# the window natively in GNOME's Wayland session, with the Wayland client
# library the script copies for the shell plugins.
if(TARGET Qt6::QWaylandIntegrationPlugin)
    install(FILES "$<TARGET_FILE:Qt6::QWaylandIntegrationPlugin>" DESTINATION plugins/platforms COMPONENT bundle EXCLUDE_FROM_ALL)
endif()
install(PROGRAMS scripts/install.sh DESTINATION . COMPONENT bundle EXCLUDE_FROM_ALL)

set(TWOTBIT_DEPLOY_DIR "${CMAKE_BINARY_DIR}/deploy" CACHE PATH "Where the deploy target puts the runnable app")
set(bundle "${TWOTBIT_DEPLOY_DIR}/twotbit")
set(tarball "twotbit-${PROJECT_VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR}.tar.gz")
add_custom_target(deploy
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${bundle}" "${TWOTBIT_DEPLOY_DIR}/${tarball}"
    COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}" --prefix "${bundle}" --strip
    COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}" --prefix "${bundle}" --component bundle
    COMMAND ${CMAKE_COMMAND} -E chdir "${TWOTBIT_DEPLOY_DIR}" ${CMAKE_COMMAND} -E tar czf "${tarball}" twotbit
    DEPENDS Twotbit
    COMMENT "Collecting Twotbit and the libraries it needs in ${bundle}"
    VERBATIM
)
