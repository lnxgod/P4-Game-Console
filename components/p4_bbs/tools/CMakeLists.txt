add_executable(p4_bbs_preview render_bbs_preview.c)
target_link_libraries(p4_bbs_preview PRIVATE p4_bbs)
target_compile_features(p4_bbs_preview PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_bbs_preview PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(p4_bbs_preview PRIVATE
        -fsanitize=address,undefined
    )
endif()
