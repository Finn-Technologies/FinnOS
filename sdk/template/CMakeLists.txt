cmake_minimum_required(VERSION 3.22)
project(FinnCounterApp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
find_package(FinnSDK REQUIRED)

qt_add_executable(finn-counter
    main.cpp
)

qt_add_qml_module(finn-counter
    URI FinnCounter
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        App.qml
)

target_link_libraries(finn-counter PRIVATE FinnSDK::FinnUIQml)
