cmake_minimum_required(VERSION 3.22)

enable_language(ASM)
find_package(ZLIB REQUIRED)
find_package(Qt6 COMPONENTS Widgets QUIET)

add_executable(finn-darwin
    main.cpp
    plist.cpp
    framework_coverage.cpp
    icns.cpp
    installer.cpp
    objc_runtime.cpp
    objc_msgsend.S
    libsystem.cpp
    foundation.cpp
    coregraphics.cpp
    swift_runtime.cpp
    compat_symbols.cpp
    appkit_bridge.cpp
    macho_loader.cpp
    carbon_compat.cpp
    swiftui/abi_metadata.cpp
    swiftui/swift_runtime_ext.cpp
    swiftui/views_and_scene.cpp
    swiftui/state_and_combine.cpp
    swiftui/registry.cpp
    ../compat/desktop_entry.cpp
)

target_include_directories(finn-darwin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(finn-darwin PRIVATE
    ZLIB::ZLIB
    ${CMAKE_DL_LIBS}
)

if(Qt6Widgets_FOUND)
    target_compile_definitions(finn-darwin PRIVATE FINN_DARWIN_QT6)
    target_link_libraries(finn-darwin PRIVATE Qt6::Widgets FinnSDK::FinnUI)
endif()

include(GNUInstallDirs)
install(TARGETS finn-darwin RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
