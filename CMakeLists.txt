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
    App/app.c
    App/cli.c
    App/control.c
    App/encoder.c
    App/fc_link.c
    App/led_status.c
    App/mavlink_nvf.c
    App/nvm.c
    App/pwm_in.c
    App/servo_bus.c
)

option(USART3_SMOKE_TEST "Build USART3 hello/echo wiring check" OFF)
if(USART3_SMOKE_TEST)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE USART3_SMOKE_TEST=1)
endif()

option(FC_MAVLINK "USART3 = FC MAVLink NAMED_VALUE_FLOAT (no ASCII CLI on USART3)" OFF)
if(FC_MAVLINK)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE FC_MAVLINK=1)
endif()

option(USB_CDC_DEBUG "CLI/telem on USB CDC (PA11/PA12)" OFF)
if(USB_CDC_DEBUG)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE USB_CDC_DEBUG=1)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/USB_DEVICE/App
        ${CMAKE_SOURCE_DIR}/USB_DEVICE/Target
        ${CMAKE_SOURCE_DIR}/Middlewares/ST/STM32_USB_Device_Library/Core/Inc
        ${CMAKE_SOURCE_DIR}/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc
    )
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        USB_DEVICE/App/usb_device.c
        USB_DEVICE/App/usbd_desc.c
        USB_DEVICE/App/usbd_cdc_if.c
        USB_DEVICE/Target/usbd_conf.c
        Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c
        Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c
        Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c
        Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Src/usbd_cdc.c
    )
endif()

target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
)

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE_NAME:${CMAKE_PROJECT_NAME}>
            ${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_COMMAND}
            -DHEX_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DHEX_BASE=${CMAKE_PROJECT_NAME}
            -P ${CMAKE_SOURCE_DIR}/cmake/stamp_hex.cmake
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating ${CMAKE_PROJECT_NAME}.hex (+ timestamped copy)"
)
