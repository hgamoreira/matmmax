get_filename_component(ENGINE "${CMAKE_CURRENT_LIST_DIR}/../../../m01_mathsmith" ABSOLUTE)
set(ENGINE_SOURCES "${ENGINE}/engine.c" "${ENGINE}/mat.c" "${ENGINE}/linalg.c"
                   "${ENGINE}/fft.c" "${ENGINE}/plot.c")
idf_component_register(SRCS "console.c" "platform.c" ${ENGINE_SOURCES}
    INCLUDE_DIRS "." "${ENGINE}"
    PRIV_REQUIRES esp_driver_uart esp_psram esp_system esp_hw_support heap
                  spi_flash vfs esp_timer)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MS_FFT MS_NO_AI)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
# Adapt only this build of the original sources; do not change the DOS/Linux engine.
set_source_files_properties(${ENGINE_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_LIST_DIR}/engine_port.h")
