cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(recovery-mode LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(FinnSDK REQUIRED)
endif()

set(CMAKE_AUTOMOC ON)

add_executable(recovery-mode
    main.cpp
    finn_terminal_widget.hpp
    finn_terminal_widget.cpp
    ../../src/recovery/recovery_engine.cpp
    ../../src/system/sys_files.cpp
)
target_include_directories(recovery-mode PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
target_link_libraries(recovery-mode PRIVATE FinnSDK::FinnUI ${CMAKE_DL_LIBS})
if(UNIX AND NOT APPLE)
    target_link_libraries(recovery-mode PRIVATE util)
endif()

include(GNUInstallDirs)
install(TARGETS recovery-mode RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(CODE "execute_process(COMMAND \${CMAKE_COMMAND} -E create_symlink recovery-mode \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/finn-recovery\")")
