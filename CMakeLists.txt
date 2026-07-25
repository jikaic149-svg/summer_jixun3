cmake_minimum_required(VERSION 3.21)

project(stm32f407ve_lcd VERSION 1.0.0 LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CPU_FLAGS
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
)

set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/linker/STM32F407VETX_FLASH.ld")

add_executable(${PROJECT_NAME}
    startup/startup_stm32f407xx.s
    Src/system_stm32f4xx.c
    Src/lcd_demo_main.c
    Src/lcd_demo_app.c
    Src/control_port.c
    bsp/bsp_board_f407ve.c
    bsp/bsp_can1.c
    bsp/bsp_cannon_relay.c
    bsp/bsp_uart_bridge.c
    bsp/bsp_lcd_fsmc.c
    bsp/bsp_xpt2046_io.c
    Device/ili9341/ili9341.c
    Device/esp8266/esp8266.c
    Device/qd4310/qd4310.c
    Device/xpt2046/xpt2046.c
    Application/control/cannon_aim_model.c
    Application/control/pitch_hold_controller.c
    Application/control/yaw_scan_controller.c
    Application/protocol/gimbal_link_protocol.c
    Application/ui/app_command_bus.c
    Application/ui/target_ui.c
    Application/ui/tft_theme_assets.c
    Application/ui/ui_font.c
)
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".elf")

target_include_directories(${PROJECT_NAME} PRIVATE
    Inc
    Src
    bsp
    Device/ili9341
    Device/esp8266
    Device/qd4310
    Device/xpt2046
    Application/control
    Application/protocol
    Application/ui
    Drivers/CMSIS/Include
    Drivers/CMSIS/Device/ST/STM32F4xx/Include
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    STM32F407xx
    HSE_VALUE=25000000U
)

target_compile_options(${PROJECT_NAME} PRIVATE
    ${CPU_FLAGS}
    -ffunction-sections
    -fdata-sections
    -fno-common
    -fstack-usage
    -finput-charset=UTF-8
    -fexec-charset=UTF-8
    -Wall
    -Wextra
    -Wshadow
    -Wformat=2
    -Wundef
    -Wdouble-promotion
    $<$<CONFIG:Debug>:-Og;-g3;-gdwarf-4>
    $<$<CONFIG:Release>:-Os;-g1>
)

target_link_options(${PROJECT_NAME} PRIVATE
    ${CPU_FLAGS}
    -T${LINKER_SCRIPT}
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,--cref
    -Wl,--print-memory-usage
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.map
)

target_link_libraries(${PROJECT_NAME} PRIVATE m c)
set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS "${LINKER_SCRIPT}")

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}>
    COMMENT "Generating HEX/BIN and reporting image size"
)
