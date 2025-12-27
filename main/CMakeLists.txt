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
    "../sparkEngine/wasm3/source/m3_api_libc.c"
    "../sparkEngine/wasm3/source/m3_api_wasi.c"
    "../sparkEngine/wasm3/source/m3_api_uvwasi.c"
    "../sparkEngine/wasm3/source/m3_api_meta_wasi.c"
    "../sparkEngine/wasm3/source/m3_api_tracer.c"
    "../sparkEngine/wasm3/source/m3_bind.c"
    "../sparkEngine/wasm3/source/m3_code.c"
    "../sparkEngine/wasm3/source/m3_compile.c"
    "../sparkEngine/wasm3/source/m3_core.c"
    "../sparkEngine/wasm3/source/m3_env.c"
    "../sparkEngine/wasm3/source/m3_exec.c"
    "../sparkEngine/wasm3/source/m3_function.c"
    "../sparkEngine/wasm3/source/m3_info.c"
    "../sparkEngine/wasm3/source/m3_module.c"
    "../sparkEngine/wasm3/source/m3_parse.c"
)

idf_component_register(
    SRCS "spark_pico.c" ${SPARK_ENGINE_SRCS} ${WASM3_SRCS}
    INCLUDE_DIRS
        "."
        "../sparkEngine/src"
        "../sparkEngine/src/engine"
        "../sparkEngine/src/wasm"
        "../sparkEngine/src/parser"
        "../sparkEngine/src/modules"
        "../sparkEngine/wasm3/source"
    REQUIRES esp_lcd driver fatfs sdmmc esp_driver_sdspi esp_lcd_ili9341
)
