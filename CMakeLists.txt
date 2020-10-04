# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

set(COMPONENTS "bootloader bootloader_support efuse esp32 esp_common esp_rom esptool_py log main micro-ecc partition_table soc spi_flash xtensa esp_http_server mdns wifi led_strip json lwip")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lc-esp32)
