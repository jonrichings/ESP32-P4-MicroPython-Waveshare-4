# Set location of base MicroPython directory.
message(STATUS "Processing MicroPython ESP32 port from: ${CMAKE_CURRENT_LIST_DIR}")

if(NOT MICROPY_DIR)
    get_filename_component(MICROPY_DIR ${CMAKE_CURRENT_LIST_DIR}/../.. ABSOLUTE)
endif()

# Set location of the ESP32 port directory.
if(NOT MICROPY_PORT_DIR)
    get_filename_component(MICROPY_PORT_DIR ${MICROPY_DIR}/ports/esp32 ABSOLUTE)
endif()

# Set MICROPY_MPYCROSS to existing binary to avoid rebuilding with 'make' on Windows
if(NOT DEFINED ENV{MICROPY_MPYCROSS})
    if(EXISTS "${MICROPY_DIR}/../mpy-cross/mpy-cross.exe")
        set(ENV{MICROPY_MPYCROSS} "${MICROPY_DIR}/../mpy-cross/mpy-cross.exe")
    else()
        set(ENV{MICROPY_MPYCROSS} "${MICROPY_DIR}/mpy-cross/build/mpy-cross.exe")
    endif()
endif()
set(MICROPY_MPYCROSS "$ENV{MICROPY_MPYCROSS}")

# Include core source components.
include(${MICROPY_DIR}/py/py.cmake)

# CMAKE_BUILD_EARLY_EXPANSION is set during the component-discovery phase of
# `idf.py build`, so none of the extmod/usermod (and in reality, most of the
# micropython) rules need to happen. Specifically, you cannot invoke add_library.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    # Enable extmod components that will be configured by extmod.cmake.
    # A board may also have enabled additional components.
    if(NOT DEFINED MICROPY_PY_BTREE)
        set(MICROPY_PY_BTREE ON)
    endif()
    
    # Force Bluetooth for P4 if not set
    if(NOT MICROPY_PY_BLUETOOTH)
        set(MICROPY_PY_BLUETOOTH ON)
    endif()
    if(NOT MICROPY_BLUETOOTH_NIMBLE)
        set(MICROPY_BLUETOOTH_NIMBLE ON)
    endif()

    # Force ESP-NOW to OFF for P4
    set(MICROPY_PY_ESPNOW OFF CACHE INTERNAL "")
    
    # Ensure USER_C_MODULES is set for LVGL bindings
    if(NOT USER_C_MODULES)
        set(USER_C_MODULES "${MICROPY_DIR}/../../lv_binding_micropython/bindings.cmake")
    endif()

    # Set a guard to prevent sub-modules (like LVGL) from calling idf_component_register
    # when they are being included as part of the MicroPython core component.
    set(MICROPY_REGISTERING_CORE TRUE)
    include(${MICROPY_DIR}/py/usermod.cmake)
    unset(MICROPY_REGISTERING_CORE)
    include(${MICROPY_DIR}/extmod/extmod.cmake)
    message(STATUS "CHECKING EXTMOD: BLUETOOTH=${MICROPY_PY_BLUETOOTH}")
    message(STATUS "FOUND USER MODULES: ${USER_C_MODULES}")
    message(STATUS "MICROPY_SOURCE_EXTMOD=${MICROPY_SOURCE_EXTMOD}")
endif()

# Note: MICROPY_SOURCE_QSTR and mkrules.cmake are now placed AFTER
# idf_component_register (see below) inside the NOT CMAKE_BUILD_EARLY_EXPANSION
# guard so that BT sources added by extmod.cmake are included in qstr.args.

list(APPEND MICROPY_QSTRDEFS_PORT
    ${MICROPY_PORT_DIR}/qstrdefsport.h
)

list(APPEND MICROPY_SOURCE_SHARED
    ${MICROPY_DIR}/shared/readline/readline.c
    ${MICROPY_DIR}/shared/netutils/netutils.c
    ${MICROPY_DIR}/shared/timeutils/timeutils.c
    ${MICROPY_DIR}/shared/runtime/interrupt_char.c
    ${MICROPY_DIR}/shared/runtime/stdout_helpers.c
    ${MICROPY_DIR}/shared/runtime/sys_stdio_mphal.c
    ${MICROPY_DIR}/shared/runtime/pyexec.c
)

list(APPEND MICROPY_SOURCE_LIB
    ${MICROPY_DIR}/lib/littlefs/lfs1.c
    ${MICROPY_DIR}/lib/littlefs/lfs1_util.c
    ${MICROPY_DIR}/lib/littlefs/lfs2.c
    ${MICROPY_DIR}/lib/littlefs/lfs2_util.c
    ${MICROPY_DIR}/lib/mbedtls_errors/esp32_mbedtls_errors.c
    ${MICROPY_DIR}/lib/oofatfs/ff.c
    ${MICROPY_DIR}/lib/oofatfs/ffunicode.c
)

list(APPEND MICROPY_SOURCE_DRIVERS
    ${MICROPY_DIR}/drivers/bus/softspi.c
    ${MICROPY_DIR}/drivers/dht/dht.c
)

list(APPEND MICROPY_SOURCE_PORT
    panichandler.c
    adc.c
    main.c
    ppp_set_auth.c
    uart.c
    usb.c
    usb_serial_jtag.c
    gccollect.c
    mphalport.c
    fatfs_port.c
    help.c
    machine_bitstream.c
    machine_timer.c
    machine_pin.c
    machine_touchpad.c
    machine_dac.c
    machine_i2c.c
    network_common.c
    network_lan.c
    network_ppp.c
    network_wlan.c
    mpnimbleport.c
    modsocket.c
    modesp.c
    esp32_nvs.c
    esp32_partition.c
    esp32_rmt.c
    esp32_ulp.c
    modesp32.c
    machine_hw_spi.c
    mpthreadport.c
    machine_rtc.c
    machine_sdcard.c
)

if(MICROPY_BOARD STREQUAL "ELECROW_P4_ADVANCED")
    list(APPEND MICROPY_SOURCE_PORT mod_elecrow.c)
elseif(MICROPY_BOARD STREQUAL "WAVESHARE_P4_8")
    list(APPEND MICROPY_SOURCE_PORT mod_waveshare.c)
elseif(MICROPY_BOARD STREQUAL "WAVESHARE_P4_4")
    list(APPEND MICROPY_SOURCE_PORT mod_waveshare_4.c)
endif()

if(IDF_TARGET STREQUAL "esp32p4")
    list(APPEND MICROPY_SOURCE_PORT mod_camera.c)
endif()



if(MICROPY_PY_ESPNOW)
    list(APPEND MICROPY_SOURCE_PORT modespnow.c)
endif()

list(TRANSFORM MICROPY_SOURCE_PORT PREPEND ${MICROPY_PORT_DIR}/)
list(APPEND MICROPY_SOURCE_PORT ${CMAKE_BINARY_DIR}/pins.c)

# LVGL Fonts (Add after prepend)
set(LVGL_SRC_DIR ${MICROPY_DIR}/../../lv_binding_micropython/lvgl/src)
list(APPEND MICROPY_SOURCE_PORT
    ${LVGL_SRC_DIR}/font/lv_font_montserrat_8.c
    ${LVGL_SRC_DIR}/font/lv_font_montserrat_10.c
    ${LVGL_SRC_DIR}/font/lv_font_montserrat_12.c
    ${LVGL_SRC_DIR}/font/lv_font_montserrat_14.c
    ${LVGL_SRC_DIR}/font/lv_font_montserrat_16.c
    ${LVGL_SRC_DIR}/font/lv_font_montserrat_18.c
    ${LVGL_SRC_DIR}/font/lv_font_montserrat_20.c
    ${LVGL_SRC_DIR}/font/lv_font_montserrat_22.c
    ${LVGL_SRC_DIR}/font/lv_font_montserrat_24.c
    ${LVGL_SRC_DIR}/font/lv_font_montserrat_26.c
    ${LVGL_SRC_DIR}/font/lv_font_montserrat_28.c
    ${LVGL_SRC_DIR}/font/lv_font_montserrat_30.c
    ${LVGL_SRC_DIR}/font/lv_font_montserrat_32.c
    ${LVGL_SRC_DIR}/font/lv_font_montserrat_34.c
    ${LVGL_SRC_DIR}/font/lv_font_montserrat_36.c
    ${LVGL_SRC_DIR}/font/lv_font_montserrat_38.c
    ${LVGL_SRC_DIR}/font/lv_font_montserrat_40.c
    ${LVGL_SRC_DIR}/font/lv_font_montserrat_42.c
    ${LVGL_SRC_DIR}/font/lv_font_montserrat_44.c
    ${LVGL_SRC_DIR}/font/lv_font_montserrat_46.c
    ${LVGL_SRC_DIR}/font/lv_font_montserrat_48.c
)

# MICROPY_SOURCE_QSTR is populated below, after idf_component_register,
# inside the if(NOT CMAKE_BUILD_EARLY_EXPANSION) block.

list(APPEND IDF_COMPONENTS
    app_update
    bootloader_support
    bt
    driver
    esp_adc
    esp_app_format
    esp_common
    esp_eth
    esp_event
    esp_hw_support
    esp_netif
    esp_partition
    esp_pm
    esp_psram
    esp_ringbuf
    esp_rom
    esp_system
    esp_timer
    esp_wifi
    freertos
    hal
    heap
    log
    lwip
    mbedtls
    newlib
    nvs_flash
    sdmmc
    soc
    spi_flash
    ulp
    vfs
    espressif__esp_tinyusb
)

if(IDF_TARGET STREQUAL "esp32p4")
    list(APPEND IDF_COMPONENTS esp_driver_cam esp_driver_isp)
endif()

# Register the main IDF component.
set(_target_name "__idf_${COMPONENT_NAME}")
if(NOT TARGET ${_target_name})
    message(STATUS "DEBUG: INITIAL REGISTERING MicroPython component: ${COMPONENT_NAME}")
    idf_component_register(
        SRCS
            ${MICROPY_SOURCE_PY}
            ${MICROPY_SOURCE_EXTMOD}
            ${MICROPY_SOURCE_SHARED}
            ${MICROPY_SOURCE_LIB}
            ${MICROPY_SOURCE_DRIVERS}
            ${MICROPY_SOURCE_PORT}
            ${MICROPY_SOURCE_BOARD}
        INCLUDE_DIRS
            ${MICROPY_INC_CORE}
            ${MICROPY_PORT_DIR}
            ${MICROPY_BOARD_DIR}
            ${CMAKE_BINARY_DIR}
        LDFRAGMENTS
            linker.lf
        REQUIRES
            ${IDF_COMPONENTS}
    )
else()
    set(COMPONENT_TARGET ${_target_name})
endif()

# Always ensure the latest sources and includes are added, especially in the second pass
if(TARGET ${_target_name} AND NOT CMAKE_BUILD_EARLY_EXPANSION)
    message(STATUS "DEBUG: ADDING CORE AND USERMOD SOURCES TO: ${_target_name}")
    target_sources(${_target_name} PRIVATE 
        ${MICROPY_SOURCE_PY}
        ${MICROPY_SOURCE_EXTMOD}
        ${MICROPY_SOURCE_SHARED}
        ${MICROPY_SOURCE_LIB}
        ${MICROPY_SOURCE_DRIVERS}
        ${MICROPY_SOURCE_PORT}
        ${MICROPY_SOURCE_BOARD}
        ${MICROPY_SOURCE_USERMOD}
    )
    target_include_directories(${_target_name} PUBLIC 
        ${MICROPY_INC_CORE}
        ${MICROPY_INC_USERMOD}
        ${MICROPY_PORT_DIR}
        ${MICROPY_BOARD_DIR}
        ${CMAKE_BINARY_DIR}
    )
endif()

# Set the MicroPython target as the current (main) IDF component target.
set(MICROPY_TARGET ${COMPONENT_TARGET})

# Define mpy-cross flags, for use with frozen code.
if(NOT IDF_TARGET STREQUAL "esp32c3")
set(MICROPY_CROSS_FLAGS -march=xtensawin)
endif()

# Set compile options for this port.
target_compile_definitions(${MICROPY_TARGET} PUBLIC
    ${MICROPY_DEF_CORE}
    ${MICROPY_DEF_BOARD}
    MICROPY_ESP_IDF_4=1
    MICROPY_VFS_FAT=1
    MICROPY_VFS_LFS2=1
    FFCONF_H=\"${MICROPY_OOFATFS_DIR}/ffconf.h\"
    LFS1_NO_MALLOC LFS1_NO_DEBUG LFS1_NO_WARN LFS1_NO_ERROR LFS1_NO_ASSERT
    LFS2_NO_MALLOC LFS2_NO_DEBUG LFS2_NO_WARN LFS2_NO_ERROR LFS2_NO_ASSERT
)

# Disable some warnings to keep the build output clean.
target_compile_options(${MICROPY_TARGET} PUBLIC
    -Wno-clobbered
    -Wno-deprecated-declarations
    -Wno-missing-field-initializers
)

# Additional include directories needed for private NimBLE headers.
target_include_directories(${MICROPY_TARGET} PUBLIC
    ${IDF_PATH}/components/bt/host/nimble/nimble
    ${IDF_PATH}/components/bt/host/nimble/esp-hci/include
    ${MICROPY_DIR}/../../lv_binding_micropython
    ${MICROPY_DIR}/../../lv_binding_micropython/lvgl
)

# Add additional extmod and usermod components.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    if(MICROPY_PY_BTREE)
        target_link_libraries(${MICROPY_TARGET} micropy_extmod_btree)
    endif()
    target_link_libraries(${MICROPY_TARGET} usermod)
endif()

    message(STATUS "DEBUG: Gathering properties for components in __COMPONENT_NAMES_RESOLVED")
    foreach(comp ${__COMPONENT_NAMES_RESOLVED})
        micropy_gather_target_properties(__idf_${comp})
        micropy_gather_target_properties(${comp})
    endforeach()
    
    # Ensure MicroPython core, port, and board includes are explicitly present in the QSTR arguments
    list(APPEND MICROPY_CPP_INC_EXTRA "${MICROPY_INC_CORE}" "${MICROPY_PORT_DIR}" "${MICROPY_BOARD_DIR}")
    list(REMOVE_DUPLICATES MICROPY_CPP_INC_EXTRA)
    list(REMOVE_ITEM MICROPY_CPP_INC_EXTRA "")
    
    message(STATUS "DEBUG: Gathered MICROPY_CPP_INC_EXTRA: ${MICROPY_CPP_INC_EXTRA}")

# Include the main MicroPython cmake rules. This generates qstr.args which
# drives the QSTR/root-pointer scanner. Keep this inside the NOT-EARLY-EXPANSION
# guard so that BT sources (added by extmod.cmake) are present in
# MICROPY_SOURCE_QSTR when file(GENERATE) writes qstr.args.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    list(APPEND MICROPY_SOURCE_QSTR
        ${MICROPY_SOURCE_PY}
        ${MICROPY_SOURCE_EXTMOD}
        ${MICROPY_SOURCE_USERMOD}
        ${MICROPY_SOURCE_SHARED}
        ${MICROPY_SOURCE_LIB}
        ${MICROPY_SOURCE_PORT}
        ${MICROPY_SOURCE_BOARD}
    )
    include(${MICROPY_DIR}/py/mkrules.cmake)

    # Generate source files for named pins (requires mkrules.cmake for MICROPY_GENHDR_DIR).
    set(GEN_PINS_PREFIX "${MICROPY_PORT_DIR}/boards/pins_prefix.c")
    set(GEN_PINS_MKPINS "${MICROPY_PORT_DIR}/boards/make-pins.py")
    set(GEN_PINS_SRC "${CMAKE_BINARY_DIR}/pins.c")
    set(GEN_PINS_HDR "${MICROPY_GENHDR_DIR}/pins.h")

    if(EXISTS "${MICROPY_BOARD_DIR}/pins.csv")
        set(GEN_PINS_BOARD_CSV "${MICROPY_BOARD_DIR}/pins.csv")
        set(GEN_PINS_BOARD_CSV_ARG --board-csv "${GEN_PINS_BOARD_CSV}")
    endif()

    target_sources(${MICROPY_TARGET} PRIVATE ${GEN_PINS_HDR})

    add_custom_command(
        OUTPUT ${GEN_PINS_SRC} ${GEN_PINS_HDR}
        COMMAND ${Python3_EXECUTABLE} ${GEN_PINS_MKPINS} ${GEN_PINS_BOARD_CSV_ARG}
            --prefix ${GEN_PINS_PREFIX} --output-source ${GEN_PINS_SRC} --output-header ${GEN_PINS_HDR}
        DEPENDS
            ${MICROPY_MPVERSION}
            ${GEN_PINS_MKPINS}
            ${GEN_PINS_BOARD_CSV}
            ${GEN_PINS_PREFIX}
        VERBATIM
        COMMAND_EXPAND_LISTS
    )
endif()
