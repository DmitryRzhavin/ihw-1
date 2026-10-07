cmake_minimum_required(VERSION 3.16)
project(restaurant_simulation C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(restaurant
    src/main.c
    src/config.c
    src/logger.c
    src/simulation.c
)

target_include_directories(restaurant PRIVATE include)
target_compile_definitions(restaurant PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(restaurant PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
)

enable_testing()
add_test(NAME demo
         COMMAND restaurant --config ${CMAKE_SOURCE_DIR}/configs/demo.conf --no-log)
add_test(NAME limited_queues
         COMMAND restaurant --config ${CMAKE_SOURCE_DIR}/configs/limited_queues.conf --no-log)
add_test(NAME impossible_order
         COMMAND restaurant --config ${CMAKE_SOURCE_DIR}/configs/impossible.conf --no-log)
