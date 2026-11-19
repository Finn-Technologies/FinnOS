cmake_minimum_required(VERSION 3.22)

project(FinnOS VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FINN_BUILD_SHELL "Build finn-shell LayerShell desktop" ON)
option(FINN_BUILD_APPS "Build native FinnOS Qt6 applications" ON)
option(FINN_BUILD_QT_PLUGIN "Build FinnOS Qt6 platform theme & style plugins" ON)
option(FINN_BUILD_TOOLS "Build finn-open CLI and SDK helpers" ON)
option(FINN_STRICT_WARNINGS "Treat C++ compiler warnings as errors during build" ON)

if(FINN_STRICT_WARNINGS)
    add_compile_options(-Wall -Wextra)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Werror>)
    endif()
endif()

include(GNUInstallDirs)

add_subdirectory(src/ui)

if(FINN_BUILD_QT_PLUGIN)
    add_subdirectory(src/qtplugin)
endif()

if(FINN_BUILD_SHELL)
    add_subdirectory(src/shell)
endif()

if(FINN_BUILD_APPS)
    add_subdirectory(apps)
endif()

if(FINN_BUILD_TOOLS)
    add_subdirectory(tools)
    add_subdirectory(src/darwin)
endif()

install(FILES assets/apps.registry
    DESTINATION "${CMAKE_INSTALL_DATADIR}/finn"
)
install(FILES VERSION
    DESTINATION "${CMAKE_INSTALL_DATADIR}/finn"
    RENAME version
)
install(DIRECTORY sdk/template/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/finn/sdk/template"
)
