cmake_minimum_required(VERSION 3.13)

# Include Pico SDK import
include(pico_sdk_import.cmake)

# Project name
project(bme680_hil C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()

# Add executable
add_executable(bme680_hil
    main.c
    src/drivers/bme680.c
    src/storage/flash_store.c
)

# Include directories
target_include_directories(bme680_hil PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/storage
)

# Link libraries
target_link_libraries(bme680_hil
    pico_stdlib
    hardware_i2c
    hardware_gpio
    hardware_flash
    hardware_sync
)

# Enable UART output (not USB)
pico_enable_stdio_uart(bme680_hil 1)
pico_enable_stdio_usb(bme680_hil 0)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(bme680_hil)

# Diagnostic: I2C address scanner (USB CDC stdio for direct readback)
add_executable(i2c_scan tools/i2c_scan.c)
target_link_libraries(i2c_scan
    pico_stdlib
    hardware_i2c
    hardware_gpio
)
pico_enable_stdio_uart(i2c_scan 1)
pico_enable_stdio_usb(i2c_scan 1)
pico_add_extra_outputs(i2c_scan)

# Diagnostic: BME680 calibration register dump
add_executable(calib_dump tools/calib_dump.c)
target_link_libraries(calib_dump
    pico_stdlib
    hardware_i2c
    hardware_gpio
)
pico_enable_stdio_uart(calib_dump 1)
pico_enable_stdio_usb(calib_dump 0)
pico_add_extra_outputs(calib_dump)
