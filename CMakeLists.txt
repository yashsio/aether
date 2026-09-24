cmake_minimum_required(VERSION 3.16)
project(aether LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Prefer Qt6, fall back to Qt5.
find_package(Qt6 QUIET COMPONENTS Widgets)
if(NOT Qt6_FOUND)
    find_package(Qt5 REQUIRED COMPONENTS Widgets)
endif()

add_executable(aether src/main.cpp)

if(Qt6_FOUND)
    target_link_libraries(aether PRIVATE Qt6::Widgets)
else()
    target_link_libraries(aether PRIVATE Qt5::Widgets)
endif()
