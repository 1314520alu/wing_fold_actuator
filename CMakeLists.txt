cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_PROJECT_NAME wing_fold_actuator)
include("cmake/gcc-arm-none-eabi.cmake")

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
project(${CMAKE_PROJECT_NAME})
enable_language(C ASM)

add_executable(${CMAKE_PROJECT_NAME})
add_subdirectory(cmake/stm32cubemx)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Inc
    App
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    App/encoder.c
    App/pwm_in.c
    App/servo_bus.c
)

target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
)

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE_NAME:${CMAKE_PROJECT_NAME}>
            ${CMAKE_PROJECT_NAME}.hex
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating ${CMAKE_PROJECT_NAME}.hex"
)
