cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32s3" CACHE STRING "Target MCU")
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" PROJECT_VER LIMIT_COUNT 1)
project(esp_mouse)

idf_build_get_property(sdk_version IDF_VER)
if(NOT sdk_version MATCHES "^v?5\\.3\\.1($|-)")
    message(WARNING "Expected ESP-IDF 5.3.1; found ${sdk_version}")
endif()

# Apply additional diagnostics to application components, never SDK sources.
foreach(component board_hal mpu6050 mouse_motion mouse_input ble_mouse ota_policy ota_update main)
    idf_component_get_property(component_library ${component} COMPONENT_LIB)
    target_compile_options(${component_library} PRIVATE
        -Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wundef
        -Wdouble-promotion -Wcast-qual -Wvla)
endforeach()
