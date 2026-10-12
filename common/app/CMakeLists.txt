# Twotbit: the Qt Widgets window over the wallet engine. The platform's
# directory adds the owner check before the recovery phrase is shown and
# what the executable needs to be an app there.
qt_add_executable(Twotbit
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
    ownerauth.h
)

if(TWOTBIT_SNAPSHOT)
    target_sources(Twotbit PRIVATE snapshot.cpp snapshot.h)
    target_compile_definitions(Twotbit PRIVATE TWOTBIT_SNAPSHOT)
endif()

target_compile_definitions(Twotbit PRIVATE TWOTBIT_VERSION="${PROJECT_VERSION}")
target_compile_options(Twotbit PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
target_link_libraries(Twotbit PRIVATE twotbit_core Qt6::Widgets)

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
