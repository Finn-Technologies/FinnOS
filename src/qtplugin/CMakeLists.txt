cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(FinnQtPlugins LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg GuiPrivate)

set(CMAKE_AUTOMOC ON)

# Query Qt6 plugin installation directory dynamically when qtpaths6 is available
find_program(FINN_QTPATHS_EXECUTABLE NAMES qtpaths6 qtpaths)
if(NOT FINN_QT_PLUGIN_DIR)
    if(FINN_QTPATHS_EXECUTABLE)
        execute_process(
            COMMAND ${FINN_QTPATHS_EXECUTABLE} --query QT_INSTALL_PLUGINS
            OUTPUT_VARIABLE _qt_plugins
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        set(FINN_QT_PLUGIN_DIR "${_qt_plugins}")
    else()
        set(FINN_QT_PLUGIN_DIR "/usr/lib/qt6/plugins")
    endif()
endif()

add_library(finnplatformtheme MODULE
    finn_platform_theme.cpp
    finn_platform_theme.hpp
    ../system/sys_files.cpp
)
target_include_directories(finnplatformtheme PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(finnplatformtheme PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::GuiPrivate
    Qt6::Svg
)
set_target_properties(finnplatformtheme PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "finnplatformtheme"
)

add_library(finnstyle MODULE
    finn_style.cpp
    finn_style.hpp
    ../system/sys_files.cpp
)
target_include_directories(finnstyle PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(finnstyle PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
set_target_properties(finnstyle PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "finnstyle"
)

install(TARGETS finnplatformtheme
    LIBRARY DESTINATION "${FINN_QT_PLUGIN_DIR}/platformthemes"
)
install(TARGETS finnstyle
    LIBRARY DESTINATION "${FINN_QT_PLUGIN_DIR}/styles"
)
