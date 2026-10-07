cmake_minimum_required(VERSION 3.16)

project(restaurant_simulation C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(restaurant
    src/main.c
    src/config.c
    src/simulation.c
)

target_include_directories(restaurant PRIVATE include)

target_compile_options(restaurant PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
