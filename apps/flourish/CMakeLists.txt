cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(finn-flourish VERSION 0.1.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(FinnSDK REQUIRED)
endif()

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS WebEngineWidgets)

add_executable(finn-flourish
    main.cpp
    browser_window.cpp
    browser_window.hpp
    sidebar_panel.cpp
    sidebar_panel.hpp
    command_bar.cpp
    command_bar.hpp
    intro_overlay.cpp
    intro_overlay.hpp
    tab_model.cpp
    tab_model.hpp
    start_page.hpp
)
target_link_libraries(finn-flourish PRIVATE
    FinnSDK::FinnUI
    Qt6::WebEngineWidgets
)

include(GNUInstallDirs)
install(TARGETS finn-flourish RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES flourish.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
