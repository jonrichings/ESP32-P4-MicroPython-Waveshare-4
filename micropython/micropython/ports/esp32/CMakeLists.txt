# Top-level cmake file for building MicroPython on ESP32.

cmake_minimum_required(VERSION 3.12)

# Define MicroPython root directory early so board config files can resolve relative paths
if(NOT MICROPY_DIR)
    get_filename_component(MICROPY_DIR ${CMAKE_CURRENT_LIST_DIR}/../.. ABSOLUTE)
endif()

# Retrieve IDF version
include($ENV{IDF_PATH}/tools/cmake/version.cmake)
set(IDF_VERSION "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}")

# Set the board if it's not already set.
if(NOT MICROPY_BOARD)
    if(BOARD)
        set(MICROPY_BOARD ${BOARD})
    else()
        set(MICROPY_BOARD WAVESHARE_P4_8)
    endif()
endif()

# Set the board directory and check that it exists.
if(NOT MICROPY_BOARD_DIR)
    set(MICROPY_BOARD_DIR ${CMAKE_CURRENT_LIST_DIR}/boards/${MICROPY_BOARD})
endif()
if(NOT EXISTS ${MICROPY_BOARD_DIR}/mpconfigboard.cmake)
    message(FATAL_ERROR "Invalid MICROPY_BOARD specified: ${MICROPY_BOARD}")
endif()

# Define the output sdkconfig so it goes in the build directory.
set(SDKCONFIG ${CMAKE_BINARY_DIR}/sdkconfig)

# Save the manifest file set from the cmake command line.
set(MICROPY_USER_FROZEN_MANIFEST ${MICROPY_FROZEN_MANIFEST})

# Specific options for IDF v5.2 and later
set(SDKCONFIG_IDF_VERSION_SPECIFIC "")
if (IDF_VERSION VERSION_GREATER_EQUAL "5.2.0")
    set(SDKCONFIG_IDF_VERSION_SPECIFIC boards/sdkconfig.idf52)
endif()

# Include board config; this is expected to set (among other options):
# - SDKCONFIG_DEFAULTS
# - IDF_TARGET
include(${MICROPY_BOARD_DIR}/mpconfigboard.cmake)

# Set the frozen manifest file. Note if MICROPY_FROZEN_MANIFEST is set from the cmake
# command line, then it will override the default and any manifest set by the board.
if (MICROPY_USER_FROZEN_MANIFEST)
    set(MICROPY_FROZEN_MANIFEST ${MICROPY_USER_FROZEN_MANIFEST})
elseif (NOT MICROPY_FROZEN_MANIFEST)
    set(MICROPY_FROZEN_MANIFEST ${CMAKE_CURRENT_LIST_DIR}/boards/manifest.py)
endif()

# Concatenate all sdkconfig files into a combined one for the IDF to use.
file(WRITE ${CMAKE_BINARY_DIR}/sdkconfig.combined.in "")
foreach(SDKCONFIG_DEFAULT ${SDKCONFIG_DEFAULTS})
    file(READ ${SDKCONFIG_DEFAULT} CONTENTS)
    file(APPEND ${CMAKE_BINARY_DIR}/sdkconfig.combined.in "${CONTENTS}")
endforeach()
configure_file(${CMAKE_BINARY_DIR}/sdkconfig.combined.in ${CMAKE_BINARY_DIR}/sdkconfig.combined COPYONLY)
set(SDKCONFIG_DEFAULTS ${CMAKE_BINARY_DIR}/sdkconfig.combined)

# Set the location of the main component for the project (one per target).
list(APPEND EXTRA_COMPONENT_DIRS main_${IDF_TARGET})
message(STATUS "EXTRA_COMPONENT_DIRS IS: ${EXTRA_COMPONENT_DIRS}")

# Include main IDF cmake file.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Enable the panic handler wrapper
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=esp_panic_handler" APPEND)

# Suppress new ESP-IDF v5 I2C driver conflict panic
idf_build_set_property(COMPILE_OPTIONS "-DCONFIG_I2C_SKIP_LEGACY_CONFLICT_CHECK=1" APPEND)
add_compile_definitions(BSP_CONFIG_NO_GRAPHIC_LIB=1)

# Define the project.
project(micropython)

# Suppress GCC 13 array-bounds warnings as errors globally
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=array-bounds" APPEND)
