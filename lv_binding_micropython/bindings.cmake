# This file is to be given as "make USER_C_MODULES=..." when building Micropython port

include(${CMAKE_CURRENT_LIST_DIR}/mkrules.cmake)

# lvgl bindings depend on lvgl itself, pull it in
include(${LVGL_DIR}/CMakeLists.txt)

# lvgl bindings target (the mpy module)
add_library(usermod_lv_bindings INTERFACE)


# create targets for generated files and set GENERATED property
all_lv_bindings()

# Create a dummy file on disk so CMake doesn't crash from directory scoping bugs during configuration
# Wrap it in NOT EXISTS so it doesn't corrupt Ninja timestamps during subsequent reconfigures
if(NOT EXISTS "${CMAKE_BINARY_DIR}/lv_mp.c")
    file(WRITE "${CMAKE_BINARY_DIR}/lv_mp.c" "// Dummy file to satisfy CMake\n")
endif()

set_source_files_properties(${LV_SRC} PROPERTIES GENERATED TRUE)

target_sources(usermod_lv_bindings INTERFACE ${LV_SRC})
target_include_directories(usermod_lv_bindings INTERFACE ${LV_INCLUDE})
target_compile_definitions(usermod_lv_bindings INTERFACE 
    LV_CONF_PATH="C:/Users/jonri/Elecrow-P4-MPY/lv_binding_micropython/lv_conf.h"
    LV_CONF_INCLUDE_SIMPLE=1
)


# make usermod link to bindings
target_link_libraries(usermod INTERFACE usermod_lv_bindings)
