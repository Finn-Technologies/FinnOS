cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(FinnSDK VERSION 0.2.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)
find_package(LayerShellQt REQUIRED)

set(CMAKE_AUTOMOC ON)

add_library(finnui SHARED
    finn/application.cpp
    finn/application.hpp
    finn/components.cpp
    finn/components.hpp
    finn/theme.cpp
    finn/theme.hpp
    finn/window.cpp
    finn/window.hpp
    finn/tokens.hpp
    finn/animation.hpp
    finn/spring.hpp
    finn/sdk.hpp
    generated_tokens.hpp
    ../system/sys_files.cpp
    ../session/client.cpp
)
add_library(FinnSDK::FinnUI ALIAS finnui)

target_include_directories(finnui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/finnos>
)

target_link_libraries(finnui
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Svg
        LayerShellQt::Interface
)

set_target_properties(finnui PROPERTIES
    EXPORT_NAME FinnUI
    VERSION 0.2.0
    SOVERSION 0
    OUTPUT_NAME finnui
)

install(TARGETS finnui
    EXPORT FinnSDKTargets
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

install(FILES
    finn/animation.hpp
    finn/application.hpp
    finn/components.hpp
    finn/sdk.hpp
    finn/spring.hpp
    finn/theme.hpp
    finn/tokens.hpp
    finn/window.hpp
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/finnos/ui/finn"
)
install(FILES generated_tokens.hpp
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/finnos/ui"
)
install(FILES ../common/paths.hpp
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/finnos/common"
)
install(FILES ../system/sys_files.hpp
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/finnos/system"
)
install(FILES ../session/client.hpp
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/finnos/session"
)
install(DIRECTORY FinnSDK/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/finnos/FinnSDK"
    FILES_MATCHING PATTERN "*.hpp"
)

configure_package_config_file(
    FinnSDKConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/FinnSDKConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/FinnSDK"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/FinnSDKConfigVersion.cmake"
    VERSION 0.2.0
    COMPATIBILITY SameMajorVersion
)
install(EXPORT FinnSDKTargets
    FILE FinnSDKTargets.cmake
    NAMESPACE FinnSDK::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/FinnSDK"
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/FinnSDKConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/FinnSDKConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/FinnSDK"
)

configure_file(finnsdk.pc.in "${CMAKE_CURRENT_BINARY_DIR}/finnsdk.pc" @ONLY)
configure_file(finnsdk.pc.in "${CMAKE_CURRENT_BINARY_DIR}/finnui.pc" @ONLY)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/finnsdk.pc"
    "${CMAKE_CURRENT_BINARY_DIR}/finnui.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
)
