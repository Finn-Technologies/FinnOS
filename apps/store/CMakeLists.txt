cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(finn-store LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(FinnSDK REQUIRED)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2)
set(CMAKE_AUTOMOC ON)

qt_add_executable(finn-store
    main.cpp
    store_controller.hpp
    store_controller.cpp
    providers/store_types.hpp
    providers/protobuf_wire.hpp
    providers/protobuf_wire.cpp
    providers/aurora_play_provider.hpp
    providers/aurora_play_provider.cpp
    providers/flathub_provider.hpp
    providers/flathub_provider.cpp
    providers/winget_provider.hpp
    providers/winget_provider.cpp
    providers/curated_catalog.hpp
    providers/curated_catalog.cpp
    ../../src/session/app_registry.cpp
    ../../src/session/client.cpp
    ../../src/compat/appimage_runner.cpp
    ../../src/compat/desktop_entry.cpp
    ../../src/compat/finnapp_installer.cpp
    ../../src/compat/flatpak_installer.cpp
    ../../src/compat/linux_sandbox.cpp
    ../../src/compat/macos_runner.cpp
    ../../src/compat/ohos_runner.cpp
    ../../src/compat/pe_icon.cpp
    ../../src/compat/wine_runner.cpp
    ../../src/compat/wine_prefix.cpp
    ../../src/compat/msi_installer.cpp
    ../../src/compat/windows_registry.cpp
    ../../src/compat/android_runner.cpp
    ../../src/system/sys_files.cpp
)

set_source_files_properties(ui/StoreApp.qml PROPERTIES QT_RESOURCE_ALIAS StoreApp.qml)
qt_add_qml_module(finn-store
    URI FinnStore
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        ui/StoreApp.qml
)

target_include_directories(finn-store PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(finn-store PRIVATE
    FinnSDK::FinnUIQml
    Qt6::Network
)

include(GNUInstallDirs)
install(TARGETS finn-store RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
