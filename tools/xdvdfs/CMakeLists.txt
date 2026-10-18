cmake_minimum_required(VERSION 3.20)

project(xdvdfs_tool)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(xdvdfs_tool
    main.cpp
    memory_mapped_file.cpp
)

target_compile_options(xdvdfs_tool PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
