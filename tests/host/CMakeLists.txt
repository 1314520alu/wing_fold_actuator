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

add_executable(test_nvm
    test_nvm.c
    ../../App/nvm.c
)

target_compile_definitions(test_nvm PRIVATE NVM_HOST_TEST=1)

target_include_directories(test_nvm PRIVATE
    fakes
    ../../App
)

add_executable(test_cli
    test_cli.c
    ../../App/cli.c
)

target_include_directories(test_cli PRIVATE
    fakes
    ../../App
)

add_executable(test_led_status
    test_led_status.c
    ../../App/led_status.c
)

target_include_directories(test_led_status PRIVATE
    fakes
    ../../App
)

add_executable(test_app
    test_app.c
    ../../App/app.c
    ../../App/control.c
)

target_include_directories(test_app PRIVATE
    fakes
    ../../App
)

add_executable(test_pwm_in
    test_pwm_in.c
    ../../App/pwm_in.c
)

target_include_directories(test_pwm_in PRIVATE
    fakes
    ../../App
)

add_executable(test_mavlink_nvf
    test_mavlink_nvf.c
    ../../App/mavlink_nvf.c
)

target_include_directories(test_mavlink_nvf PRIVATE
    ../../App
)
