cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pico)
include(pico_sdk_import.cmake)
project(photopainter C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_subdirectory(lib/FatFs_SPI)

add_executable(photopainter
    src/main.c src/board.c src/epd.c src/pcf85063.c src/usb.c src/hw_config.c
    src/convert.c src/ui.c src/config.c src/library.c src/collage.c
    lib/tjpgd/tjpgd.c lib/fonts/font24.c lib/fonts/font16.c)

target_include_directories(photopainter PRIVATE src lib/tjpgd lib/fonts)
target_compile_options(photopainter PRIVATE -Wall -Wno-unused-function -Wno-format-truncation)

# We drive TinyUSB ourselves (CDC + mass storage); keep picotool's reset interface
# and the 1200-baud reset-to-BOOTSEL so the frame can be flashed without buttons.
target_compile_definitions(photopainter PRIVATE
    PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=1
    PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_RESET_TO_BOOTSEL=1
    PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE=1
    PICO_STDIO_ENABLE_CRLF_SUPPORT=1)

target_link_libraries(photopainter
    pico_stdlib pico_unique_id pico_rand
    hardware_spi hardware_i2c hardware_adc hardware_watchdog
    tinyusb_device FatFs_SPI)

pico_enable_stdio_usb(photopainter 1)
pico_enable_stdio_uart(photopainter 0)
pico_set_program_name(photopainter "PhotoPainter custom firmware")
pico_add_extra_outputs(photopainter)
