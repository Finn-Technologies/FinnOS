cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(FinnSDK VERSION 1.0.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)
find_package(Qt6 COMPONENTS Qml Quick QuickControls2 QUIET)
find_package(LayerShellQt QUIET)

set(CMAKE_AUTOMOC ON)

add_library(finnui SHARED
    finn/application.cpp
    finn/application.hpp
    finn/components.cpp
    finn/components.hpp
    finn/form_factor.hpp
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
)

if(LayerShellQt_FOUND)
    target_compile_definitions(finnui PUBLIC FINN_HAVE_LAYERSHELLQT=1)
    target_link_libraries(finnui PUBLIC LayerShellQt::Interface)
endif()

set_target_properties(finnui PROPERTIES
    EXPORT_NAME FinnUI
    VERSION 1.0.0
    SOVERSION 1
    OUTPUT_NAME finnui
)

# ---- Native Qt 6 Quick / QML FinnUI Framework (Kirigami-inspired) ----
set(FINN_EXPORT_TARGETS finnui)

if(Qt6Qml_FOUND AND Qt6Quick_FOUND)
    if(COMMAND qt_policy)
        qt_policy(SET QTP0004 NEW)
    endif()

    set(FINN_QML_CONTROLS
        qml/controls/FinnIcon.qml
        qml/controls/FinnDivider.qml
        qml/controls/FinnFocusRing.qml
        qml/controls/FinnGlassPanel.qml
        qml/controls/FinnCard.qml
        qml/controls/FinnButton.qml
        qml/controls/FinnIconButton.qml
        qml/controls/FinnToggle.qml
        qml/controls/FinnSlider.qml
        qml/controls/FinnSearchField.qml
        qml/controls/FinnWindowControls.qml
        qml/controls/FinnStatCard.qml
        qml/controls/FinnHeroCard.qml
        qml/controls/FinnSettingRow.qml
        qml/controls/FinnNavItem.qml
        qml/controls/FinnBadge.qml
        qml/controls/FinnEmptyState.qml
        qml/controls/FinnToast.qml
        qml/controls/FinnApplicationWindow.qml
    )
    foreach(_qml ${FINN_QML_CONTROLS})
        get_filename_component(_alias "${_qml}" NAME)
        set_source_files_properties("${_qml}" PROPERTIES QT_RESOURCE_ALIAS "${_alias}")
    endforeach()

    qt_add_library(finnui_qml SHARED
        qml/finn_qml_types.cpp
        qml/finn_qml_types.hpp
        qml/finn_qml_app.cpp
        qml/finn_qml_app.hpp
    )
    add_library(FinnSDK::FinnUIQml ALIAS finnui_qml)

    qt_add_qml_module(finnui_qml
        URI FinnUI
        VERSION 1.0
        RESOURCE_PREFIX /qt/qml
        OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/FinnUI"
        NO_PLUGIN
        QML_FILES
            ${FINN_QML_CONTROLS}
    )

    target_include_directories(finnui_qml
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/finnos>
    )

    target_link_libraries(finnui_qml
        PUBLIC
            FinnSDK::FinnUI
            Qt6::Qml
            Qt6::Quick
    )
    if(Qt6QuickControls2_FOUND)
        target_link_libraries(finnui_qml PUBLIC Qt6::QuickControls2)
    endif()

    set_target_properties(finnui_qml PROPERTIES
        EXPORT_NAME FinnUIQml
        VERSION 1.0.0
        SOVERSION 1
        OUTPUT_NAME finnui_qml
    )
    list(APPEND FINN_EXPORT_TARGETS finnui_qml)
endif()

install(TARGETS ${FINN_EXPORT_TARGETS}
    EXPORT FinnSDKTargets
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

install(FILES
    finn/animation.hpp
    finn/application.hpp
    finn/components.hpp
    finn/form_factor.hpp
    finn/sdk.hpp
    finn/spring.hpp
    finn/theme.hpp
    finn/tokens.hpp
    finn/window.hpp
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/finnos/ui/finn"
)
install(FILES
    qml/finn_qml_types.hpp
    qml/finn_qml_app.hpp
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/finnos/ui/qml"
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
    VERSION 1.0.0
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
