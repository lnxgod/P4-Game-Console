set(P4_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
set(P4_LUA_VENDOR_DIR "${P4_REPOSITORY_ROOT}/third_party/lua-5.4.8/src")

set(P4_LUA_VENDOR_SOURCES
    "${P4_LUA_VENDOR_DIR}/lapi.c"
    "${P4_LUA_VENDOR_DIR}/lauxlib.c"
    "${P4_LUA_VENDOR_DIR}/lbaselib.c"
    "${P4_LUA_VENDOR_DIR}/lcode.c"
    "${P4_LUA_VENDOR_DIR}/lctype.c"
    "${P4_LUA_VENDOR_DIR}/ldebug.c"
    "${P4_LUA_VENDOR_DIR}/ldo.c"
    "${P4_LUA_VENDOR_DIR}/ldump.c"
    "${P4_LUA_VENDOR_DIR}/lfunc.c"
    "${P4_LUA_VENDOR_DIR}/lgc.c"
    "${P4_LUA_VENDOR_DIR}/llex.c"
    "${P4_LUA_VENDOR_DIR}/lmathlib.c"
    "${P4_LUA_VENDOR_DIR}/lmem.c"
    "${P4_LUA_VENDOR_DIR}/lobject.c"
    "${P4_LUA_VENDOR_DIR}/lopcodes.c"
    "${P4_LUA_VENDOR_DIR}/lparser.c"
    "${P4_LUA_VENDOR_DIR}/lstate.c"
    "${P4_LUA_VENDOR_DIR}/lstring.c"
    "${P4_LUA_VENDOR_DIR}/lstrlib.c"
    "${P4_LUA_VENDOR_DIR}/ltable.c"
    "${P4_LUA_VENDOR_DIR}/ltablib.c"
    "${P4_LUA_VENDOR_DIR}/ltm.c"
    "${P4_LUA_VENDOR_DIR}/lundump.c"
    "${P4_LUA_VENDOR_DIR}/lutf8lib.c"
    "${P4_LUA_VENDOR_DIR}/lvm.c"
    "${P4_LUA_VENDOR_DIR}/lzio.c"
)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS "src/lua_runtime.c" ${P4_LUA_VENDOR_SOURCES}
        INCLUDE_DIRS "include"
        PRIV_INCLUDE_DIRS "${P4_LUA_VENDOR_DIR}"
        REQUIRES p4_game_runtime_core
    )
    set_source_files_properties(
        ${P4_LUA_VENDOR_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-w"
    )
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Werror -Wconversion -Wshadow -std=c11
    )
else()
    if(NOT TARGET p4_game_runtime_core)
        add_subdirectory(
            "${CMAKE_CURRENT_SOURCE_DIR}/../p4_game_runtime_core"
            "${CMAKE_CURRENT_BINARY_DIR}/p4_game_runtime_core"
        )
    endif()

    add_library(p4_lua_vendor STATIC ${P4_LUA_VENDOR_SOURCES})
    target_include_directories(p4_lua_vendor PUBLIC "${P4_LUA_VENDOR_DIR}")
    target_compile_features(p4_lua_vendor PUBLIC c_std_11)

    add_library(p4_lua_runtime STATIC src/lua_runtime.c)
    target_include_directories(p4_lua_runtime PUBLIC include)
    target_link_libraries(p4_lua_runtime PUBLIC p4_game_runtime_core p4_lua_vendor)
    if(NOT MSVC)
        target_link_libraries(p4_lua_runtime PUBLIC m)
    endif()
    target_compile_features(p4_lua_runtime PUBLIC c_std_11)
    target_compile_options(p4_lua_runtime PRIVATE
        -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow
    )

    if(P4_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(p4_lua_runtime PRIVATE
            -fno-omit-frame-pointer -fsanitize=address,undefined
        )
    endif()

    if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_lua_runtime.c")
        add_executable(p4_lua_runtime_tests tests/test_lua_runtime.c)
        target_link_libraries(p4_lua_runtime_tests PRIVATE p4_lua_runtime)
        target_compile_features(p4_lua_runtime_tests PRIVATE c_std_11)
        target_compile_options(p4_lua_runtime_tests PRIVATE
            -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow
        )
        add_test(NAME p4_lua_runtime COMMAND p4_lua_runtime_tests)
    endif()

    add_executable(p4_lua_smoke tools/p4_lua_smoke.c)
    target_link_libraries(
        p4_lua_smoke PRIVATE p4_lua_runtime p4_content_catalog)
    target_compile_features(p4_lua_smoke PRIVATE c_std_11)
    target_compile_options(p4_lua_smoke PRIVATE
        -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow
    )
endif()
