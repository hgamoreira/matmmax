idf_component_register(SRCS "console.c"
    PRIV_REQUIRES esp_driver_uart esp_psram esp_system esp_hw_support
                  heap spi_flash vfs)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
