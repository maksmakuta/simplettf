cmake_minimum_required(VERSION 4.0)
project(simplettf VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(simplettf SHARED
    src/simplettf.cpp
        src/simplettf.cpp
        example/main.cpp
)

target_include_directories(simplettf PRIVATE include)
add_subdirectory(example)