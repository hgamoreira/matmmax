get_filename_component(UPSTREAM "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/elf-loader-v2.1.0/elf_loader" ABSOLUTE)
if(NOT EXISTS "${UPSTREAM}/src/esp_elf.c")
    message(FATAL_ERROR "Run tools/setup_elf_loader.sh first")
endif()
# Compile pinned upstream sources unmodified. Its package_manager include only
# supplies version macros, so no component-manager/CMake utility dependency.
idf_component_register(SRCS "${UPSTREAM}/src/esp_elf.c"
    "${UPSTREAM}/src/esp_elf_validate.c" "${UPSTREAM}/src/esp_elf_symbol.c"
    "${UPSTREAM}/src/esp_elf_adapter.c" "${UPSTREAM}/src/arch/esp_elf_xtensa.c"
    INCLUDE_DIRS "${UPSTREAM}/include"
    PRIV_REQUIRES spi_flash esp_mm lwip
    LDFRAGMENTS "${UPSTREAM}/linker.lf")
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ELF_LOADER_VER_MAJOR=2 ELF_LOADER_VER_MINOR=1 ELF_LOADER_VER_PATCH=0)
