get_filename_component(PORT "${CMAKE_CURRENT_LIST_DIR}/../../e01_mathsmith/main" ABSOLUTE)
get_filename_component(ENGINE "${CMAKE_CURRENT_LIST_DIR}/../../../m01_mathsmith" ABSOLUTE)
get_filename_component(COMMON "${CMAKE_CURRENT_LIST_DIR}/../../../../common/workstation" ABSOLUTE)
get_filename_component(DEMO "${CMAKE_CURRENT_LIST_DIR}/../../../workstation/demo" ABSOLUTE)
set(ENGINE_SOURCES "${ENGINE}/engine.c" "${ENGINE}/mat.c" "${ENGINE}/linalg.c"
                   "${ENGINE}/fft.c" "${ENGINE}/plot.c")
idf_component_register(SRCS "app_console.c" "${PORT}/console.c" "${PORT}/platform.c"
    "${COMMON}/launcher.c" "${DEMO}/demo.c" ${ENGINE_SOURCES}
    INCLUDE_DIRS "." "${PORT}" "${ENGINE}" "${COMMON}" "${DEMO}"
    PRIV_REQUIRES esp_driver_uart esp_psram esp_system esp_hw_support heap
                  spi_flash vfs esp_timer)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MS_FFT MS_NO_AI MS_WORKSTATION_LAUNCHER)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
set_source_files_properties(${ENGINE_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-include;${PORT}/engine_port.h")
