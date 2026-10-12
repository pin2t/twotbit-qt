# Twotbit.exe: the Qt Widgets window over the wallet engine.
qt_add_executable(Twotbit WIN32
    main.cpp
    appmodel.cpp appmodel.h
    coinsview.cpp coinsview.h
    homeview.cpp homeview.h
    mainwindow.cpp mainwindow.h
    options.cpp options.h
    phrasedialog.cpp phrasedialog.h
    receivedialog.cpp receivedialog.h
    senddialog.cpp senddialog.h
    settingsview.cpp settingsview.h
    theme.cpp theme.h
    transactionsview.cpp transactionsview.h
    widgets.cpp widgets.h
    winauth.cpp winauth.h
    resources/app.rc
)

if(TWOTBIT_SNAPSHOT)
    target_sources(Twotbit PRIVATE snapshot.cpp snapshot.h)
    target_compile_definitions(Twotbit PRIVATE TWOTBIT_SNAPSHOT)
endif()

target_compile_definitions(Twotbit PRIVATE TWOTBIT_VERSION="${PROJECT_VERSION}")
target_compile_options(Twotbit PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
target_link_libraries(Twotbit PRIVATE twotbit_core Qt6::Widgets credui runtimeobject)

# The icon and manifest sit next to the resource script, and the version of
# its version resource comes from the project.
configure_file(resources/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/resources/version.h" @ONLY)
set_property(SOURCE resources/app.rc APPEND PROPERTY INCLUDE_DIRECTORIES
    "${CMAKE_CURRENT_SOURCE_DIR}/resources" "${CMAKE_CURRENT_BINARY_DIR}/resources")

# The window icon in every size the macOS app ships.
set(icons "${TWOTBIT_MACOS_DIR}/App/Assets.xcassets/AppIcon.appiconset")
set_source_files_properties("${icons}/icon_16x16.png" PROPERTIES QT_RESOURCE_ALIAS icons/icon_16.png)
set_source_files_properties("${icons}/icon_32x32.png" PROPERTIES QT_RESOURCE_ALIAS icons/icon_32.png)
set_source_files_properties("${icons}/icon_32x32@2x.png" PROPERTIES QT_RESOURCE_ALIAS icons/icon_64.png)
set_source_files_properties("${icons}/icon_128x128.png" PROPERTIES QT_RESOURCE_ALIAS icons/icon_128.png)
set_source_files_properties("${icons}/icon_256x256.png" PROPERTIES QT_RESOURCE_ALIAS icons/icon_256.png)
set_source_files_properties("${icons}/icon_512x512.png" PROPERTIES QT_RESOURCE_ALIAS icons/icon_512.png)
qt_add_resources(Twotbit "icons"
    PREFIX "/"
    FILES
        "${icons}/icon_16x16.png"
        "${icons}/icon_32x32.png"
        "${icons}/icon_32x32@2x.png"
        "${icons}/icon_128x128.png"
        "${icons}/icon_256x256.png"
        "${icons}/icon_512x512.png"
)

# A folder that runs on any Windows 10 or 11 machine: the executable with the
# Qt and MinGW libraries and plugins it loads, copied by windeployqt.
find_program(WINDEPLOYQT windeployqt HINTS "${QT6_INSTALL_PREFIX}/bin")
set(TWOTBIT_DEPLOY_DIR "${CMAKE_BINARY_DIR}/deploy" CACHE PATH "Where the deploy target puts the runnable app")
# The copy leaves out the debug information, most of a RelWithDebInfo build.
if(CMAKE_STRIP)
    set(strip COMMAND "${CMAKE_STRIP}" --strip-all "${TWOTBIT_DEPLOY_DIR}/Twotbit.exe")
endif()
# The app loads no images but PNG, which Qt reads itself, so the image format
# plugins stay out.
add_custom_target(deploy
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TWOTBIT_DEPLOY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:Twotbit>" "${TWOTBIT_DEPLOY_DIR}"
    ${strip}
    COMMAND "${WINDEPLOYQT}" --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-quick-import
            --skip-plugin-types generic,networkinformation,iconengines,imageformats --dir "${TWOTBIT_DEPLOY_DIR}" "${TWOTBIT_DEPLOY_DIR}/Twotbit.exe"
    DEPENDS Twotbit
    COMMENT "Collecting Twotbit and the libraries it needs in ${TWOTBIT_DEPLOY_DIR}"
)
