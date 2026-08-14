set(PLATFORM_STORAGE_WAD_SOURCES
    src/platform_storage_wad.c
)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS
            src/platform_storage.c
            ${PLATFORM_STORAGE_WAD_SOURCES}
        INCLUDE_DIRS include
        PRIV_INCLUDE_DIRS src
        REQUIRES
            esp_driver_gpio
            esp_driver_sdmmc
            fatfs
            freertos
            mbedtls
            sdmmc
    )

    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall
        -Wextra
        -Wconversion
        -Wshadow
        -Werror
        # ESP-IDF 5.5.3's public RISC-V headers contain two signedness
        # conversions. Keep the other conversion diagnostics enabled without
        # promoting those pinned-SDK header warnings into component failures.
        -Wno-sign-conversion
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(platform_storage_wad VERSION 0.1.0 LANGUAGES C)

    add_library(platform_storage_wad STATIC ${PLATFORM_STORAGE_WAD_SOURCES})
    target_include_directories(platform_storage_wad
        PUBLIC include
        PRIVATE src
    )
    target_compile_features(platform_storage_wad PUBLIC c_std_11)

    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(platform_storage_wad PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
            -Werror
        )
    endif()

    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
