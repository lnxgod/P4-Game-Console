set(GAMEPAD_CORE_SOURCES
    src/gamepad.c
    src/hid_gamepad.c
)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${GAMEPAD_CORE_SOURCES}
        INCLUDE_DIRS include
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
else()
    cmake_minimum_required(VERSION 3.16)
    project(gamepad_core VERSION 0.1.0 LANGUAGES C)

    add_library(gamepad_core STATIC ${GAMEPAD_CORE_SOURCES})
    target_include_directories(gamepad_core PUBLIC include)
    target_compile_features(gamepad_core PUBLIC c_std_11)

    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(gamepad_core PRIVATE
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
