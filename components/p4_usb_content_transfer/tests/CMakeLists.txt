add_executable(test_h1_usb_drive_control
    test_h1_usb_drive_control.c
)
target_link_libraries(test_h1_usb_drive_control PRIVATE p4_h1_usb_drive_control)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_h1_usb_drive_control PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(test_h1_usb_drive_control PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME h1_usb_drive_control COMMAND test_h1_usb_drive_control)
