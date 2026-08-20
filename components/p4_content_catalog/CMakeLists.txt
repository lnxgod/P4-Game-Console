# SPDX-License-Identifier: MIT

set(P4_CONTENT_CATALOG_SOURCES
    src/content_catalog.c
    src/sha256.c
)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${P4_CONTENT_CATALOG_SOURCES}
        INCLUDE_DIRS include
        PRIV_INCLUDE_DIRS src
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        cmake_minimum_required(VERSION 3.16)
        project(p4_content_catalog VERSION 0.1.0 LANGUAGES C)
    endif()
    add_library(p4_content_catalog STATIC ${P4_CONTENT_CATALOG_SOURCES})
    target_include_directories(p4_content_catalog
        PUBLIC include
        PRIVATE src
    )
    target_compile_features(p4_content_catalog PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(p4_content_catalog PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(p4_content_catalog PRIVATE
            -fsanitize=address,undefined
        )
    endif()
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        include(CTest)
        add_subdirectory(tests)
        add_executable(p4_content_inspect tools/inspect_content.c)
        target_link_libraries(p4_content_inspect PRIVATE p4_content_catalog)
        target_compile_features(p4_content_inspect PRIVATE c_std_11)
        if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(p4_content_inspect PRIVATE
                -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
                -fsanitize=address,undefined -fno-omit-frame-pointer
            )
            target_link_options(p4_content_inspect PRIVATE
                -fsanitize=address,undefined
            )
        endif()
    endif()
endif()
