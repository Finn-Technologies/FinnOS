cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(finn-flourish VERSION 1.0.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(FinnSDK REQUIRED)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Svg WebEngineQuick)
set(CMAKE_AUTOMOC ON)

qt_add_executable(finn-flourish
    main.cpp
)

set_source_files_properties(ui/FlourishApp.qml PROPERTIES QT_RESOURCE_ALIAS FlourishApp.qml)
qt_add_qml_module(finn-flourish
    URI FinnFlourish
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        ui/FlourishApp.qml
)

target_include_directories(finn-flourish PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(finn-flourish PRIVATE
    FinnSDK::FinnUIQml
    Qt6::Svg
    Qt6::WebEngineQuick
)

include(GNUInstallDirs)
install(TARGETS finn-flourish RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES flourish.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
