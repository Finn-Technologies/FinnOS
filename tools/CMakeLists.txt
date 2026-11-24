cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(finn-tools LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Svg)

add_executable(finn-open
    finn_open.cpp
    ../src/session/client.cpp
)
target_include_directories(finn-open PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

add_executable(finn-cursors
    finn_cursors.cpp
)
target_link_libraries(finn-cursors PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Svg
)

include(GNUInstallDirs)
install(TARGETS finn-open finn-cursors RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(PROGRAMS finn-display finn-guest-tools finn-sdk DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(PROGRAMS finn-oobe-apply DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}")
