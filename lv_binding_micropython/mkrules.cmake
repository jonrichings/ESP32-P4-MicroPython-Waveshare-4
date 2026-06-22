
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(AWK awk mawk gawk)

set(LV_BINDINGS_DIR ${CMAKE_CURRENT_LIST_DIR})

# Common function for creating LV bindings

function(lv_bindings)
    set(_options)
    set(_one_value_args OUTPUT)
    set(_multi_value_args INPUT DEPENDS COMPILE_OPTIONS PP_OPTIONS GEN_OPTIONS FILTER)
    cmake_parse_arguments(
        PARSE_ARGV 0 LV
        "${_options}"
        "${_one_value_args}"
        "${_multi_value_args}"
    )

    set(LV_PP ${LV_OUTPUT}.pp)
    set(LV_MPY_METADATA ${LV_OUTPUT}.json)

    add_custom_command(
        OUTPUT 
            ${LV_PP}
        COMMAND
        ${CMAKE_C_COMPILER} -E -DPYCPARSER ${LV_COMPILE_OPTIONS} ${LV_PP_OPTIONS} "${LV_CFLAGS}" -I ${LV_BINDINGS_DIR}/pycparser/utils/fake_libc_include ${MICROPY_CPP_FLAGS} ${LV_INPUT} > ${LV_PP}
        DEPENDS
            ${LV_INPUT}
            ${LV_DEPENDS}
            ${LV_BINDINGS_DIR}/pycparser/utils/fake_libc_include
        IMPLICIT_DEPENDS
            C ${LV_INPUT}
        VERBATIM
        COMMAND_EXPAND_LISTS
    )

    if(ESP_PLATFORM AND COMPONENT_LIB)
        if(LV_COMPILE_OPTIONS)
            target_compile_options(${COMPONENT_LIB} PRIVATE ${LV_COMPILE_OPTIONS})
        endif()
    else()
        if(LV_COMPILE_OPTIONS)
            target_compile_options(usermod_lv_bindings INTERFACE ${LV_COMPILE_OPTIONS})
        endif()
    endif()

    if (DEFINED LV_FILTER)

        set(LV_PP_FILTERED ${LV_PP}.filtered)
        set(LV_AWK_CONDITION)
        foreach(_f ${LV_FILTER})
            string(APPEND LV_AWK_CONDITION "\$3!~\"${_f}\" && ")
        endforeach()
        string(APPEND LV_AWK_COMMAND "\$1==\"#\"{p=(${LV_AWK_CONDITION} 1)} p{print}")

        # message("AWK COMMAND: ${LV_AWK_COMMAND}")

        add_custom_command(
            OUTPUT
                ${LV_PP_FILTERED}
            COMMAND
                ${AWK} ${LV_AWK_COMMAND} ${LV_PP} > ${LV_PP_FILTERED}
            DEPENDS
                ${LV_PP}
            VERBATIM
            COMMAND_EXPAND_LISTS
        )
    else()
        set(LV_PP_FILTERED ${LV_PP})
    endif()

    set(LV_JSON ${CMAKE_BINARY_DIR}/lvgl_all.json)

    if (EXISTS ${LVGL_DIR}/scripts/gen_json/gen_json.py)
        set(LVGL_ALL_H ${CMAKE_BINARY_DIR}/lvgl_all.h)
        add_custom_command(
            OUTPUT
                ${LVGL_ALL_H}
            COMMAND ${CMAKE_COMMAND} -E echo "#include \"${LVGL_DIR}/lvgl.h\"" > ${LVGL_ALL_H}
            COMMAND ${CMAKE_COMMAND} -E echo "#include \"${LVGL_DIR}/src/lvgl_private.h\"" >> ${LVGL_ALL_H}
            COMMAND_EXPAND_LISTS
        )
        add_custom_command(
            OUTPUT
                ${LV_JSON}
            COMMAND
                ${Python3_EXECUTABLE} ${LVGL_DIR}/scripts/gen_json/gen_json.py --lvgl-config ${CMAKE_CURRENT_LIST_DIR}/lv_conf.h --no-docstrings --filter-private --target-header ${LVGL_ALL_H} > ${LV_JSON}
            DEPENDS
                ${LVGL_DIR}/scripts/gen_json/gen_json.py
                ${LVGL_ALL_H}
                ${CMAKE_CURRENT_LIST_DIR}/lv_conf.h
            COMMAND_EXPAND_LISTS
        )
    else()
        add_custom_command(
            OUTPUT
                ${LV_JSON}
            COMMAND
                echo "{}" > ${LV_JSON}
            COMMAND_EXPAND_LISTS
        )
    endif()

    add_custom_command(
        OUTPUT
            ${LV_OUTPUT}
        COMMAND
            ${Python3_EXECUTABLE} ${LV_BINDINGS_DIR}/gen/gen_mpy.py ${LV_GEN_OPTIONS} -MD ${LV_MPY_METADATA} -E ${LV_PP_FILTERED} -J ${LV_JSON} ${LV_INPUT} > ${LV_OUTPUT} || (rm -f ${LV_OUTPUT} && /bin/false)
        DEPENDS
            ${LV_BINDINGS_DIR}/gen/gen_mpy.py
            ${LV_PP_FILTERED}
            ${LV_JSON}
        COMMAND_EXPAND_LISTS
    )

    # Tell CMake this file will be generated during the build
    set_property(SOURCE ${LV_OUTPUT} PROPERTY GENERATED 1)

    # Force the build system to generate this file ASAP
    # Create a unique target name based on the output file
    get_filename_component(_target_name ${LV_OUTPUT} NAME_WE)
    add_custom_target(generate_${_target_name} DEPENDS ${LV_OUTPUT})
    
    # Ensure the usermod library generation waits for this file
    if(TARGET usermod_lv_bindings)
        add_dependencies(usermod_lv_bindings generate_${_target_name})
    endif()

endfunction()

# Definitions for specific bindings

set(LVGL_DIR ${LV_BINDINGS_DIR}/lvgl)

set(LV_MP ${CMAKE_BINARY_DIR}/lv_mp.c)
if(ESP_PLATFORM)
    set(LV_ESPIDF ${CMAKE_BINARY_DIR}/lv_espidf.c)
endif()

# Function for creating all specific bindings

function(all_lv_bindings)

    # LVGL bindings

    file(GLOB_RECURSE LVGL_HEADERS ${LVGL_DIR}/src/*.h ${LV_BINDINGS_DIR}/lv_conf.h)
    lv_bindings(
        OUTPUT
            ${LV_MP}
        INPUT
            ${LVGL_DIR}/lvgl.h
        DEPENDS
            ${LVGL_HEADERS}
        GEN_OPTIONS
            -M lvgl -MP lv
    )
        
    # ESPIDF bindings - DISABLED FOR ESP-IDF v5.5 (Incompatible headers)
    #if(ESP_PLATFORM)
    #    ... (Disabled because it tries to parse all ESP-IDF v4 headers which fails on v5.5)
    #endif(ESP_PLATFORM)

endfunction()

# Add includes to CMake component

set(LV_INCLUDE
    ${LV_BINDINGS_DIR}
)

# Add sources to CMake component

set(LV_SRC
    ${LV_MP}
)

if(ESP_PLATFORM)
    # LIST(APPEND LV_SRC
    #     ${LV_BINDINGS_DIR}/driver/esp32/espidf.c
    #     ${LV_BINDINGS_DIR}/driver/esp32/modrtch.c
    #     ${LV_BINDINGS_DIR}/driver/esp32/sh2lib.c
    #     ${LV_ESPIDF}
    # )
endif(ESP_PLATFORM)
