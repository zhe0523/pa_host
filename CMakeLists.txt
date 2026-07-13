cmake_minimum_required(VERSION 3.16)

project(pa_host VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 优先使用 Qt6；Kylin V10 上如果只安装 Qt5，也能自动回退到 Qt5。
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets SerialPort)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets SerialPort)

add_executable(pa_host
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/ImageView.cpp
    src/ImageView.h
    src/PaProtocol.cpp
    src/PaProtocol.h
    src/SerialClient.cpp
    src/SerialClient.h
    src/TiRawImage.cpp
    src/TiRawImage.h
)

target_link_libraries(pa_host PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::SerialPort
)

if(MSVC)
    target_compile_options(pa_host PRIVATE /W4 /permissive-)
else()
    target_compile_options(pa_host PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS pa_host RUNTIME DESTINATION bin)
