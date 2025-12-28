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
    "../sparkEngine/src/wasm/wamr_runtime.c"
    "../sparkEngine/src/parser/sprk_parser.c"
)

idf_component_register(
    SRCS
        "spark.c"
        "spark_cart.c"
        "spark_device.c"
        "spark_gui.c"
        "spark_utils.c"
        ${SPARK_ENGINE_SRCS}
    INCLUDE_DIRS
        "."
        "../sparkEngine/src"
        "../sparkEngine/src/engine"
        "../sparkEngine/src/wasm"
        "../sparkEngine/src/parser"
        "../sparkEngine/src/modules"
    REQUIRES esp_lcd driver fatfs sdmmc esp_driver_sdmmc esp_lcd_ili9341 wamr
)

if (NOT CMAKE_SCRIPT_MODE_FILE)
    # Increase WAMR runtime stack to avoid stack height overflows.
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SPARK_WASM_STACK_DEFAULT=65536)
endif()
