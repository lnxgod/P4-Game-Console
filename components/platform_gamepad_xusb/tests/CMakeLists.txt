
add_executable(xusb_lifecycle_tests test_lifecycle.c
    ../../gamepad_core/src/gamepad.c ../../gamepad_core/src/snapshot.c
    ../../gamepad_core/src/xusb.c)
target_include_directories(xusb_lifecycle_tests PRIVATE . mocks ../include
    ../../gamepad_core/include ../../platform_gamepad/include
    ../../platform_usb_host/include)
target_compile_features(xusb_lifecycle_tests PRIVATE c_std_11)
target_compile_options(xusb_lifecycle_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
    -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(xusb_lifecycle_tests PRIVATE -fsanitize=address,undefined)
add_test(NAME xusb_lifecycle_tests COMMAND xusb_lifecycle_tests)

add_executable(xusb_broker_tests test_broker.c
    ../../platform_gamepad/src/platform_gamepad.c
    ../../gamepad_core/src/gamepad.c ../../gamepad_core/src/snapshot.c
    ../../gamepad_core/src/mapping.c)
target_include_directories(xusb_broker_tests PRIVATE . mocks
    ../../gamepad_core/include ../../platform_gamepad/include)
target_compile_features(xusb_broker_tests PRIVATE c_std_11)
target_compile_options(xusb_broker_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
    -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(xusb_broker_tests PRIVATE -fsanitize=address,undefined)
add_test(NAME xusb_broker_tests COMMAND xusb_broker_tests)
