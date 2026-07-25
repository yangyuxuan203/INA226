cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

project(STM32F407_Bootloader C ASM)

get_filename_component(ARM_TOOLCHAIN_BIN_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
find_program(ARM_OBJCOPY_EXECUTABLE arm-none-eabi-objcopy
    HINTS "${ARM_TOOLCHAIN_BIN_DIR}" REQUIRED)
find_program(ARM_SIZE_EXECUTABLE arm-none-eabi-size
    HINTS "${ARM_TOOLCHAIN_BIN_DIR}" REQUIRED)
set(CMAKE_OBJCOPY "${ARM_OBJCOPY_EXECUTABLE}")
set(CMAKE_SIZE "${ARM_SIZE_EXECUTABLE}")

set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(HAL_DIR ${ROOT_DIR}/Drivers/STM32F4xx_HAL_Driver)
set(MONOCYPHER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/monocypher)

add_executable(${PROJECT_NAME}
    Src/main.c
    Src/bl_board.c
    Src/bl_crypto.c
    Src/bl_flash.c
    Src/bl_esp8266.c
    Src/stm32f4xx_it.c
    ${MONOCYPHER_DIR}/monocypher.c
    ${MONOCYPHER_DIR}/optional/monocypher-ed25519.c
    ${ROOT_DIR}/Core/Src/ota_update.c
    ${ROOT_DIR}/Core/Src/system_stm32f4xx.c
    ${ROOT_DIR}/startup_stm32f407xx.s
    ${HAL_DIR}/Src/stm32f4xx_hal.c
    ${HAL_DIR}/Src/stm32f4xx_hal_cortex.c
    ${HAL_DIR}/Src/stm32f4xx_hal_flash.c
    ${HAL_DIR}/Src/stm32f4xx_hal_flash_ex.c
    ${HAL_DIR}/Src/stm32f4xx_hal_gpio.c
    ${HAL_DIR}/Src/stm32f4xx_hal_pwr.c
    ${HAL_DIR}/Src/stm32f4xx_hal_pwr_ex.c
    ${HAL_DIR}/Src/stm32f4xx_hal_rcc.c
    ${HAL_DIR}/Src/stm32f4xx_hal_rcc_ex.c
    ${HAL_DIR}/Src/stm32f4xx_hal_uart.c
)

# Unoptimized Monocypher exceeds the fixed 48 KiB boot partition. Keep the
# bootloader itself debuggable while compiling the audited third-party crypto
# implementation for size in every configuration.
set_source_files_properties(
    ${MONOCYPHER_DIR}/monocypher.c
    ${MONOCYPHER_DIR}/optional/monocypher-ed25519.c
    PROPERTIES COMPILE_OPTIONS "-Os"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    Inc
    ${MONOCYPHER_DIR}
    ${MONOCYPHER_DIR}/optional
    ${ROOT_DIR}/Core/Inc
    ${HAL_DIR}/Inc
    ${HAL_DIR}/Inc/Legacy
    ${ROOT_DIR}/Drivers/CMSIS/Device/ST/STM32F4xx/Include
    ${ROOT_DIR}/Drivers/CMSIS/Include
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    USE_HAL_DRIVER
    STM32F407xx
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -mcpu=cortex-m4
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
    -Wall
    -Wextra
    -fdata-sections
    -ffunction-sections
    $<$<CONFIG:Debug>:-O0;-g3>
    $<$<CONFIG:Release>:-Os;-g0>
)

target_link_options(${PROJECT_NAME} PRIVATE
    -mcpu=cortex-m4
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
    -T${CMAKE_CURRENT_SOURCE_DIR}/STM32F407_BOOT.ld
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,-Map=${PROJECT_NAME}.map
    -Wl,--gc-sections
    -Wl,--print-memory-usage
)

target_link_libraries(${PROJECT_NAME} m)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:${PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}>
    COMMENT "Generating bootloader binary"
)
