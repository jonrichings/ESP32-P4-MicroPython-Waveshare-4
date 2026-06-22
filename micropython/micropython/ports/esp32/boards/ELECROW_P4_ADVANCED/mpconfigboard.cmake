set(IDF_TARGET "esp32p4")
set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    boards/sdkconfig.ble
    boards/ELECROW_P4_ADVANCED/sdkconfig.board
)
set(MICROPY_HW_BOARD_NAME "ELECROW_P4_ADVANCED")
set(MICROPY_PY_BLUETOOTH 1 CACHE INTERNAL "")
set(MICROPY_BLUETOOTH_NIMBLE 1 CACHE INTERNAL "")
set(MICROPY_PY_ESPNOW 0 CACHE INTERNAL "")
set(MICROPY_PY_NETWORK_WLAN 1 CACHE INTERNAL "")
message(STATUS "CHECKING BOARD CONFIG: BLUETOOTH=${MICROPY_PY_BLUETOOTH}")

list(APPEND IDF_COMPONENTS esp32_p4_function_ev_board mdns)
list(APPEND MICROPY_INC_CORE "${CMAKE_CURRENT_LIST_DIR}/../../components/esp32_p4_function_ev_board")
list(APPEND MICROPY_CPP_INC_EXTRA "${CMAKE_CURRENT_LIST_DIR}/../../components/esp32_p4_function_ev_board")
list(APPEND MICROPY_CPP_INC_EXTRA "${CMAKE_CURRENT_LIST_DIR}/../../components/esp32_p4_function_ev_board/include")

# Add the official LVGL Bindings as a User C Module
# Using hardcoded absolute path to avoid usermod.cmake context issues
set(USER_C_MODULES "c:/Users/jonri/Elecrow-P4-MPY/lv_binding_micropython/bindings.cmake")
set(MICROPY_HW_MCU_NAME "ESP32-P4")
set(MICROPY_PY_BTREE 1)
set(MICROPY_FROZEN_MANIFEST ${CMAKE_CURRENT_LIST_DIR}/manifest.py)
