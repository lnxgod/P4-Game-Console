set(DOOM_VIDEO_CONVERSION_SOURCE src/doom_video_convert.c)
option(P4_DOOM_INDEXED_PACKET_EXPERIMENT
       "Immutable indexed Doom frame packets (required on Tab5)" ON)

option(P4_DOOM_TAB5_FUSED_PRESCALE
       "Legacy completed-frame prescale (forbidden on Tab5)" OFF)

if(COMMAND idf_component_register)
    idf_build_get_property(P4_ACTIVE_BOARD_PROFILE P4_BOARD_PROFILE)
    if(P4_ACTIVE_BOARD_PROFILE STREQUAL "m5stack-tab5")
        if(P4_DOOM_TAB5_FUSED_PRESCALE OR NOT P4_DOOM_INDEXED_PACKET_EXPERIMENT)
            message(FATAL_ERROR "Tab5 requires native 768x480 indexed packets; disable fused prescale and enable indexed packets")
        endif()
        set(DOOM_VIDEO_ADAPTER_SOURCE src/doom_video_tab5_worker.c src/doom_video_metrics.c)
        if(P4_DOOM_INDEXED_PACKET_EXPERIMENT)
            list(APPEND DOOM_VIDEO_CONVERSION_SOURCE src/doom_video_indexed.c)
        endif()
    else()
        set(DOOM_VIDEO_ADAPTER_SOURCE src/doom_video_espidf.c)
    endif()
    idf_component_register(
        SRCS
            ${DOOM_VIDEO_CONVERSION_SOURCE}
            ${DOOM_VIDEO_ADAPTER_SOURCE}
        INCLUDE_DIRS include
        REQUIRES
            heap
            platform_display
            esp_timer
            freertos
        PRIV_REQUIRES
            doom_touch_input
    )

    if(P4_ACTIVE_BOARD_PROFILE STREQUAL "m5stack-tab5")
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            P4_DOOM_TAB5_FUSED_PRESCALE=$<BOOL:${P4_DOOM_TAB5_FUSED_PRESCALE}>)
        target_compile_definitions(${COMPONENT_LIB} PUBLIC
            P4_DOOM_INDEXED_PACKET_EXPERIMENT=1 DOOM_VIDEO_NATIVE_TAB5=1)
    endif()
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
