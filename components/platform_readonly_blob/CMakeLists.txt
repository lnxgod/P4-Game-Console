set(PLATFORM_READONLY_BLOB_CORE_SOURCES
    src/readonly_blob_core.c
)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS
            ${PLATFORM_READONLY_BLOB_CORE_SOURCES}
            src/platform_readonly_blob_vfs.c
        INCLUDE_DIRS include
        PRIV_INCLUDE_DIRS src
        REQUIRES
            freertos
            vfs
    )

    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall
        -Wextra
        -Wconversion
        -Wshadow
        -Werror
        -Wno-sign-conversion
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(platform_readonly_blob VERSION 0.1.0 LANGUAGES C)

    add_library(platform_readonly_blob STATIC ${PLATFORM_READONLY_BLOB_CORE_SOURCES})
    target_include_directories(platform_readonly_blob PRIVATE src)
    target_compile_features(platform_readonly_blob PUBLIC c_std_11)

    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(platform_readonly_blob PRIVATE
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
