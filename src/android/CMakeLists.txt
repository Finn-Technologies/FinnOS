cmake_minimum_required(VERSION 3.22)

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
find_package(Qt6 COMPONENTS Widgets QUIET)

add_executable(finn-android
    main.cpp
    zip_reader.cpp
    android_format.cpp
    apk_format.cpp
    dex_runtime.cpp
    art_bridge.cpp
    ui_bridge.cpp
    android_installer.cpp
)

set_target_properties(finn-android PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    ENABLE_EXPORTS ON
)

target_include_directories(finn-android PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(finn-android PRIVATE
    ZLIB::ZLIB
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

if(Qt6Widgets_FOUND AND TARGET FinnSDK::FinnUI)
    target_compile_definitions(finn-android PRIVATE FINN_ANDROID_QT6)
    target_link_libraries(finn-android PRIVATE Qt6::Widgets FinnSDK::FinnUI)
endif()

include(GNUInstallDirs)
install(TARGETS finn-android RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES ../../assets/finn-android.desktop
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
