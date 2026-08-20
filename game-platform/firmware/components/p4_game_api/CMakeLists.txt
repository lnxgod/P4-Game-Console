if(COMMAND idf_component_register)
    idf_component_register(
        INCLUDE_DIRS "include"
        REQUIRES p4_game_runtime_core
    )
else()
    if(NOT TARGET p4_game_runtime_core)
        add_subdirectory(
            "${CMAKE_CURRENT_SOURCE_DIR}/../p4_game_runtime_core"
            "${CMAKE_CURRENT_BINARY_DIR}/p4_game_runtime_core"
        )
    endif()
    add_library(p4_game_api INTERFACE)
    target_include_directories(p4_game_api INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(p4_game_api INTERFACE p4_game_runtime_core)
endif()
