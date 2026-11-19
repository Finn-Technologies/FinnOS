cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(finn-shell LANGUAGES CXX C)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(FinnSDK REQUIRED)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus Network)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner REQUIRED)

set(CMAKE_AUTOMOC ON)

set(FOREIGN_XML "${CMAKE_CURRENT_SOURCE_DIR}/../protocol/wlr-foreign-toplevel-management-unstable-v1.xml")
set(FOREIGN_HDR "${CMAKE_CURRENT_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-client-protocol.h")
set(FOREIGN_SRC "${CMAKE_CURRENT_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-protocol.c")

add_custom_command(
    OUTPUT "${FOREIGN_HDR}"
    COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header "${FOREIGN_XML}" "${FOREIGN_HDR}"
    DEPENDS "${FOREIGN_XML}"
    COMMENT "Generating wlr-foreign-toplevel client header"
)
add_custom_command(
    OUTPUT "${FOREIGN_SRC}"
    COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code "${FOREIGN_XML}" "${FOREIGN_SRC}"
    DEPENDS "${FOREIGN_XML}"
    COMMENT "Generating wlr-foreign-toplevel protocol glue"
)

add_executable(finn-shell
    main.cpp
    dock.cpp
    dock.hpp
    popovers.cpp
    popovers.hpp
    notifications.cpp
    notifications.hpp
    foreign_windows.cpp
    foreign_windows.hpp
    search.cpp
    search.hpp
    ../session/app_registry.cpp
    ../compat/android_runner.cpp
    ../compat/appimage_runner.cpp
    ../compat/desktop_entry.cpp
    ../compat/linux_sandbox.cpp
    ../compat/macos_runner.cpp
    ../compat/ohos_runner.cpp
    ../compat/wine_prefix.cpp
    ../compat/wine_runner.cpp
    ../compat/windows_registry.cpp
    ../system/desktop_services.cpp
    ../system/desktop_services.hpp
    "${FOREIGN_HDR}"
    "${FOREIGN_SRC}"
)

target_include_directories(finn-shell PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_BINARY_DIR}
)

# src/compat/android_runner.cpp and appimage_runner.cpp are new shared
# sources; their headers use aggregate initializers that trip
# -Wmissing-field-initializers under this target's -Werror. Suppress only
# that warning here until the compat owner fills in all fields.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(finn-shell PRIVATE -Wno-missing-field-initializers)
endif()

target_link_libraries(finn-shell PRIVATE
    FinnSDK::FinnUI
    Qt6::DBus
    Qt6::Network
    PkgConfig::WAYLAND_CLIENT
)

include(GNUInstallDirs)
install(TARGETS finn-shell RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
