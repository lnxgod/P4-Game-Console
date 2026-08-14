set(PLATFORM_AUDIO_FACTORY_POLICY_SOURCES
    src/platform_audio_factory_policy.c
)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS
            src/platform_audio_factory.c
            ${PLATFORM_AUDIO_FACTORY_POLICY_SOURCES}
        INCLUDE_DIRS include
        PRIV_INCLUDE_DIRS src
        REQUIRES
            esp_driver_gpio
            esp_driver_i2s
            esp_timer
            freertos
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
    project(platform_audio_factory VERSION 0.1.0 LANGUAGES C)

    add_library(platform_audio_factory_policy STATIC
        ${PLATFORM_AUDIO_FACTORY_POLICY_SOURCES}
    )
    target_include_directories(platform_audio_factory_policy
        PUBLIC include
        PRIVATE src
    )
    target_compile_features(platform_audio_factory_policy PUBLIC c_std_11)

    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(platform_audio_factory_policy PRIVATE
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
