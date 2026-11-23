cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(finn-comp VERSION 1.0.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(FinnSDK REQUIRED)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)
find_package(Qt6 COMPONENTS WaylandCompositor ShaderTools QUIET)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBSYSTEMD QUIET IMPORTED_TARGET libsystemd)
    pkg_check_modules(LIBINPUT QUIET IMPORTED_TARGET libinput)
    pkg_check_modules(LIBUDEV QUIET IMPORTED_TARGET libudev)
endif()

set(CMAKE_AUTOMOC ON)

# Pure C++20 compositor core library (no Qt Wayland dependency; unit-tested on host & target)
add_library(finncomp_core STATIC
    core/scene_stack.hpp
    core/workspace_model.cpp
    core/workspace_model.hpp
    core/input_router.cpp
    core/input_router.hpp
)
target_include_directories(finncomp_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

set_source_files_properties(qml/CompositorScene.qml PROPERTIES
    QT_RESOURCE_ALIAS "CompositorScene.qml"
)

set(FINNCOMP_QML_FILES
    qml/CompositorScene.qml
)
if(Qt6WaylandCompositor_FOUND)
    set_source_files_properties(qml/WaylandCompositorScene.qml PROPERTIES
        QT_RESOURCE_ALIAS "WaylandCompositorScene.qml"
    )
    list(APPEND FINNCOMP_QML_FILES qml/WaylandCompositorScene.qml)
endif()

qt_add_executable(finn-comp
    backdrop_item.cpp
    backdrop_item.hpp
    main.cpp
    server.cpp
    server.hpp
)

qt_add_qml_module(finn-comp
    URI FinnComp
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        ${FINNCOMP_QML_FILES}
)

if(Qt6ShaderTools_FOUND AND COMMAND qt6_add_shaders)
    qt6_add_shaders(finn-comp "finncomp_shaders"
        PREFIX "/finncomp/shaders"
        FILES
            shaders/dual_kawase_blur.frag
    )
endif()

target_include_directories(finn-comp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(finn-comp PRIVATE
    finncomp_core
    FinnSDK::FinnUIQml
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)

if(Qt6WaylandCompositor_FOUND)
    target_compile_definitions(finn-comp PRIVATE FINN_HAVE_QT_WAYLAND_COMPOSITOR=1)
    target_link_libraries(finn-comp PRIVATE Qt6::WaylandCompositor)
endif()

if(LIBSYSTEMD_FOUND)
    target_compile_definitions(finn-comp PRIVATE FINN_HAVE_SYSTEMD=1)
    target_link_libraries(finn-comp PRIVATE PkgConfig::LIBSYSTEMD)
endif()

if(LIBINPUT_FOUND AND LIBUDEV_FOUND)
    target_compile_definitions(finn-comp PRIVATE FINN_HAVE_LIBINPUT=1)
    target_link_libraries(finn-comp PRIVATE PkgConfig::LIBINPUT PkgConfig::LIBUDEV)
endif()

include(GNUInstallDirs)
install(TARGETS finn-comp RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
