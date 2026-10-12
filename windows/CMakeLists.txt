# What makes the common engine and app a Windows app: TCP and DNS over
# Winsock, the recovery phrase in the Credential Manager, Windows Hello and
# the account password before it is shown, and the executable's icon,
# version information and manifest.
target_sources(twotbit_core PRIVATE
    core/p2p/tcp.cpp
    core/storage/credentials.cpp
)
target_compile_definitions(twotbit_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_link_libraries(twotbit_core PRIVATE ws2_32 bcrypt advapi32)

# The owner check, a library as the tests use it too.
add_library(twotbit_windows STATIC
    app/winauth.cpp app/winauth.h
)
target_include_directories(twotbit_windows PUBLIC app PRIVATE "${CMAKE_SOURCE_DIR}/common/app")
target_compile_options(twotbit_windows PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
target_link_libraries(twotbit_windows PUBLIC twotbit_core Qt6::Widgets credui runtimeobject)

set_target_properties(Twotbit PROPERTIES WIN32_EXECUTABLE ON)
target_sources(Twotbit PRIVATE app/resources/app.rc)
target_link_libraries(Twotbit PRIVATE twotbit_windows)

# The icon and manifest sit next to the resource script, and the version of
# its version resource comes from the project.
configure_file(app/resources/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/app/resources/version.h" @ONLY)
set_property(SOURCE app/resources/app.rc APPEND PROPERTY INCLUDE_DIRECTORIES
    "${CMAKE_CURRENT_SOURCE_DIR}/app/resources" "${CMAKE_CURRENT_BINARY_DIR}/app/resources")

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
