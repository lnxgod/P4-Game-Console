if(COMMAND idf_component_register)
    idf_component_register(INCLUDE_DIRS "include")
else()
    add_library(p4_game_api INTERFACE)
    target_include_directories(p4_game_api INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
endif()

