cmake_minimum_required(VERSION 3.21)

project(QtVideoGuard VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
find_package(OpenCV 4.5 REQUIRED COMPONENTS core imgproc video videoio)

qt_add_executable(QtVideoGuard
    src/main.cpp
    src/DetectionConfig.h
    src/MotionDetector.h
    src/MotionDetector.cpp
    src/VideoWorker.h
    src/VideoWorker.cpp
    src/VideoWidget.h
    src/VideoWidget.cpp
    src/MainWindow.h
    src/MainWindow.cpp
)

target_link_libraries(QtVideoGuard PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${OpenCV_LIBS}
)

target_include_directories(QtVideoGuard PRIVATE ${OpenCV_INCLUDE_DIRS})

if(MSVC)
    target_compile_options(QtVideoGuard PRIVATE /W4 /permissive-)
else()
    target_compile_options(QtVideoGuard PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS QtVideoGuard
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
