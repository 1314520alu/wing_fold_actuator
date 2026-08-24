cmake_minimum_required(VERSION 3.22)
project(control_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(test_control
    test_control.c
    ../../App/control.c
)

target_include_directories(test_control PRIVATE
    ../../App
)

add_executable(test_encoder
    test_encoder.c
    ../../App/encoder.c
)

target_include_directories(test_encoder PRIVATE
    fakes
    ../../App
)
