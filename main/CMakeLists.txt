set(SPARK_ENGINE_SRCS
    "../sparkEngine/src/engine/spark_engine.c"
    "../sparkEngine/src/main.c"
    "../sparkEngine/src/modules/sys_system.c"
    "../sparkEngine/src/modules/sys_storage.c"
    "../sparkEngine/src/modules/sys_graphics_primitives.c"
    "../sparkEngine/src/modules/sys_graphics_state.c"
    "../sparkEngine/src/modules/sys_input.c"
    "../sparkEngine/src/modules/sys_graphics_sprites.c"
    "../sparkEngine/src/modules/sys_map.c"
    "../sparkEngine/src/wasm/wasm3_runtime.c"
    "../sparkEngine/src/parser/sprk_parser.c"
)

set(WASM3_SRCS
    "../sparkEngine/wasm3/src/m3_api_libc.c"
    "../sparkEngine/wasm3/src/m3_bind.c"
    "../sparkEngine/wasm3/src/m3_code.c"
    "../sparkEngine/wasm3/src/m3_compile.c"
    "../sparkEngine/wasm3/src/m3_core.c"
    "../sparkEngine/wasm3/src/m3_emit.c"
    "../sparkEngine/wasm3/src/m3_env.c"
    "../sparkEngine/wasm3/src/m3_exec.c"
    "../sparkEngine/wasm3/src/m3_function.c"
    "../sparkEngine/wasm3/src/m3_info.c"
    "../sparkEngine/wasm3/src/m3_module.c"
    "../sparkEngine/wasm3/src/m3_parse.c"
)

if (NOT CMAKE_SCRIPT_MODE_FILE)
    set(WASM3_COMPILE_FLAGS "-DM3_IN_IRAM -DESP32 -O3 -freorder-blocks")
    set_source_files_properties(${WASM3_SRCS} PROPERTIES COMPILE_FLAGS "${WASM3_COMPILE_FLAGS}")

    # Wasm3 triggers -Werror=maybe-uninitialized under GCC 14; keep it as a warning.
    set_source_files_properties(
        "../sparkEngine/wasm3/src/m3_parse.c"
        PROPERTIES COMPILE_FLAGS "${WASM3_COMPILE_FLAGS} -Wno-error=maybe-uninitialized"
    )
endif()

idf_component_register(
    SRCS
        "spark.c"
        "spark_cart.c"
        "spark_device.c"
        "spark_gui.c"
        "spark_utils.c"
        ${SPARK_ENGINE_SRCS}
        ${WASM3_SRCS}
    INCLUDE_DIRS
        "."
        "../sparkEngine/src"
        "../sparkEngine/src/engine"
        "../sparkEngine/src/wasm"
        "../sparkEngine/src/parser"
        "../sparkEngine/src/modules"
        "../sparkEngine/wasm3/src"
    REQUIRES esp_lcd driver fatfs sdmmc esp_driver_sdmmc esp_lcd_ili9341
)

if (NOT CMAKE_SCRIPT_MODE_FILE)
    # Use wasm3 fixed heap to avoid invalid frees when running from flash.
    target_compile_definitions(${COMPONENT_LIB} PRIVATE d_m3FixedHeap=1048576)
    # Increase wasm3 runtime stack to avoid stack height overflows.
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SPARK_STACK_SIZE=65536)
endif()
