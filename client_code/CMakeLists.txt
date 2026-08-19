# Minimum required version of CMake
cmake_minimum_required(VERSION 3.10)

# Project name
project(MiniGameProject)

# Use C++17
set(CMAKE_CXX_STANDARD 20)

# Add header include folder
include_directories(include)

# Create executable
add_executable(main
    src/main.cpp
    src/glad.c
    src/stb_image.cpp
)

# Tell CMake where GLFW is installed
#
# This path comes from MSYS2 UCRT64
link_directories(C:/msys64/ucrt64/lib)

# Link required libraries
target_link_libraries(main
    glfw3
    opengl32
    ws2_32
)