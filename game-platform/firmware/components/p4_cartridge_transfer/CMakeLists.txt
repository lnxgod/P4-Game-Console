if(COMMAND idf_component_register)
    idf_component_register(
        SRCS "src/cartridge_transfer.c"
        INCLUDE_DIRS "include"
    )

    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Werror -Wconversion -Wshadow -std=c11
    )
else()
    add_library(p4_cartridge_transfer STATIC src/cartridge_transfer.c)
    target_include_directories(p4_cartridge_transfer PUBLIC include)
    target_compile_features(p4_cartridge_transfer PUBLIC c_std_11)
    target_compile_options(p4_cartridge_transfer PRIVATE
        -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow
    )

    option(P4_ENABLE_SANITIZERS "Enable host sanitizers" ON)
    if(P4_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(p4_cartridge_transfer PRIVATE
            -fno-omit-frame-pointer -fsanitize=address,undefined
        )
        target_link_options(p4_cartridge_transfer PUBLIC
            -fno-omit-frame-pointer -fsanitize=address,undefined
        )
    endif()

    if(BUILD_TESTING)
        add_executable(p4_cartridge_transfer_tests tests/test_cartridge_transfer.c)
        target_link_libraries(p4_cartridge_transfer_tests PRIVATE p4_cartridge_transfer)
        target_compile_features(p4_cartridge_transfer_tests PRIVATE c_std_11)
        target_compile_options(p4_cartridge_transfer_tests PRIVATE
            -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow
        )
        add_test(NAME p4_cartridge_transfer COMMAND p4_cartridge_transfer_tests)
    endif()
endif()
