cmake_minimum_required(VERSION 3.16)

project(pa_host VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 优先使用 Qt6；Kylin V10 上如果只安装 Qt5，也能自动回退到 Qt5。
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets SerialPort)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets SerialPort)

add_library(pa_core STATIC
    src/ImageAlgorithms.cpp
    src/ImageAlgorithms.h
    src/ImageSession.cpp
    src/ImageSession.h
    src/ImageSource.cpp
    src/ImageSource.h
    src/MtfAnalysis.cpp
    src/MtfAnalysis.h
    src/PaProtocol.cpp
    src/PaProtocol.h
    src/TiRawImage.cpp
    src/TiRawImage.h
)

target_include_directories(pa_core PUBLIC src)
target_link_libraries(pa_core PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
)

add_library(pa_transport STATIC
    src/SerialClient.cpp
    src/SerialClient.h
)

target_include_directories(pa_transport PUBLIC src)
target_link_libraries(pa_transport PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::SerialPort
)

add_executable(pa_host
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/ImageView.cpp
    src/ImageView.h
)

target_link_libraries(pa_host PRIVATE
    pa_core
    pa_transport
    Qt${QT_VERSION_MAJOR}::Widgets
)

function(pa_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

pa_enable_warnings(pa_core)
pa_enable_warnings(pa_transport)
pa_enable_warnings(pa_host)

install(TARGETS pa_host RUNTIME DESTINATION bin)

include(CTest)

if(BUILD_TESTING)
    add_executable(pa_host_tests
        tests/pa_host_tests.cpp
    )

    target_link_libraries(pa_host_tests PRIVATE
        pa_core
    )

    pa_enable_warnings(pa_host_tests)

    add_test(NAME pa_host_tests COMMAND pa_host_tests)

    add_executable(pa_image_benchmark
        tools/benchmark_tiraw.cpp
    )

    target_link_libraries(pa_image_benchmark PRIVATE
        pa_core
    )

    pa_enable_warnings(pa_image_benchmark)
endif()
