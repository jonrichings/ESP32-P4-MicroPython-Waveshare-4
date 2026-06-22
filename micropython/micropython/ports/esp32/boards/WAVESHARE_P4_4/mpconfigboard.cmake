set(IDF_TARGET "esp32p4")
set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    boards/sdkconfig.ble
    boards/WAVESHARE_P4_4/sdkconfig.board
)
set(MICROPY_HW_BOARD_NAME "WAVESHARE_P4_4")
set(MICROPY_PY_BLUETOOTH 1 CACHE INTERNAL "")
set(MICROPY_BLUETOOTH_NIMBLE 1 CACHE INTERNAL "")
set(MICROPY_PY_ESPNOW 0 CACHE INTERNAL "")
set(MICROPY_PY_NETWORK_WLAN 1 CACHE INTERNAL "")
message(STATUS "CHECKING BOARD CONFIG: BLUETOOTH=${MICROPY_PY_BLUETOOTH}")

list(APPEND IDF_COMPONENTS mdns)

# Add the official LVGL Bindings as a User C Module
# Using relative path to prevent local file path dependency
set(USER_C_MODULES "${MICROPY_DIR}/../../lv_binding_micropython/bindings.cmake")
set(MICROPY_HW_MCU_NAME "ESP32-P4")
set(MICROPY_PY_BTREE 1)
set(MICROPY_FROZEN_MANIFEST ${CMAKE_CURRENT_LIST_DIR}/manifest.py)
