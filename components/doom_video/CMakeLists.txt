set(DOOM_VIDEO_CONVERSION_SOURCE src/doom_video_convert.c)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS
            ${DOOM_VIDEO_CONVERSION_SOURCE}
            src/doom_video_espidf.c
        INCLUDE_DIRS include
        REQUIRES
            heap
            platform_display
    )

    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall
        -Wextra
        -Wconversion
        -Wshadow
        -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(doom_video VERSION 0.1.0 LANGUAGES C)

    add_library(doom_video STATIC ${DOOM_VIDEO_CONVERSION_SOURCE})
    target_include_directories(doom_video PUBLIC include)
    target_compile_features(doom_video PUBLIC c_std_11)

    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(doom_video PRIVATE
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
