get_filename_component(PORT "${CMAKE_CURRENT_LIST_DIR}/../../e01_mathsmith/main" ABSOLUTE)
get_filename_component(LAUNCH "${CMAKE_CURRENT_LIST_DIR}/../../e02_launcher/main" ABSOLUTE)
get_filename_component(ENGINE "${CMAKE_CURRENT_LIST_DIR}/../../../m01_mathsmith" ABSOLUTE)
get_filename_component(COMMON "${CMAKE_CURRENT_LIST_DIR}/../../../../common/workstation" ABSOLUTE)
get_filename_component(DEMO "${CMAKE_CURRENT_LIST_DIR}/../../../workstation/demo" ABSOLUTE)
idf_build_get_property(BUILD BUILD_DIR)
set(ENGINE_SOURCES "${ENGINE}/engine.c" "${ENGINE}/mat.c" "${ENGINE}/linalg.c"
                   "${ENGINE}/fft.c" "${ENGINE}/plot.c")
idf_component_register(SRCS "external_apps.c" "${BUILD}/sample_blob.c"
    "${PORT}/console.c" "${PORT}/platform.c" "${LAUNCH}/app_console.c"
    "${COMMON}/launcher.c" "${COMMON}/package.c" "${DEMO}/demo.c" ${ENGINE_SOURCES}
    INCLUDE_DIRS "." "${PORT}" "${LAUNCH}" "${ENGINE}" "${COMMON}" "${DEMO}"
    PRIV_REQUIRES esp_driver_uart esp_psram esp_system esp_hw_support heap
                  spi_flash vfs esp_timer ms_elf_loader)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MS_FFT MS_NO_AI
    MS_WORKSTATION_LAUNCHER MS_EXTERNAL_APPS)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
set_source_files_properties(${ENGINE_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-include;${PORT}/engine_port.h")
