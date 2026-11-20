cmake_minimum_required(VERSION 3.22)

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
find_package(Qt6 COMPONENTS Widgets QUIET)

add_executable(finn-ohos
    main.cpp
    zip_reader.cpp
    arkui_declarative.cpp
    arkui_bridge.cpp
    abc_runtime.cpp
    ohos_installer.cpp
)

set_target_properties(finn-ohos PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    ENABLE_EXPORTS ON
)

target_include_directories(finn-ohos PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(finn-ohos PRIVATE
    ZLIB::ZLIB
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

if(Qt6Widgets_FOUND)
    target_compile_definitions(finn-ohos PRIVATE FINN_OHOS_QT6)
    target_link_libraries(finn-ohos PRIVATE Qt6::Widgets FinnSDK::FinnUI)
endif()

include(GNUInstallDirs)
install(TARGETS finn-ohos RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
