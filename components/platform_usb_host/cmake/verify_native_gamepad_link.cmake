# SPDX-License-Identifier: MIT

if(NOT DEFINED P4_LINK_NM OR P4_LINK_NM STREQUAL "" OR
   NOT DEFINED P4_LINK_ELF OR P4_LINK_ELF STREQUAL "")
    message(FATAL_ERROR "native gamepad link audit requires P4_LINK_NM and P4_LINK_ELF")
endif()

execute_process(
    COMMAND "${P4_LINK_NM}" -a "${P4_LINK_ELF}"
    RESULT_VARIABLE P4_LINK_NM_RESULT
    OUTPUT_VARIABLE P4_LINK_NM_OUTPUT
    ERROR_VARIABLE P4_LINK_NM_ERROR
)
if(NOT P4_LINK_NM_RESULT EQUAL 0)
    message(FATAL_ERROR
        "native gamepad link audit could not inspect ${P4_LINK_ELF}: ${P4_LINK_NM_ERROR}")
endif()

set(P4_REQUIRED_NATIVE_GAMEPAD_SYMBOLS
    build_authorization_enabled
    s_build_authorization_gate
    usb_daemon_task
    usb_host_install
    usb_host_lib_handle_events
    platform_usb_host_start
    platform_gamepad_usb_start
    platform_usb_host_enable_root_port
    __wrap_usb_host_device_open
    platform_gamepad_usb_open_guard_call
)
foreach(P4_REQUIRED_SYMBOL IN LISTS P4_REQUIRED_NATIVE_GAMEPAD_SYMBOLS)
    string(REGEX MATCH
        "(^|[\r\n])[^\r\n]*[ \t]${P4_REQUIRED_SYMBOL}([\r\n]|$)"
        P4_REQUIRED_SYMBOL_MATCH
        "${P4_LINK_NM_OUTPUT}"
    )
    if(P4_REQUIRED_SYMBOL_MATCH STREQUAL "")
        message(FATAL_ERROR
            "native gamepad link audit: ${P4_REQUIRED_SYMBOL} was discarded from ${P4_LINK_ELF}")
    endif()
endforeach()

# The pinned usb_host_hid 1.2.0 NEW_DEV failure path can close an uninitialized
# local device handle. The final image must route its open call through the
# platform guard, which pre-clears the result before calling the real IDF API.
get_filename_component(P4_LINK_TOOL_DIR "${P4_LINK_NM}" DIRECTORY)
set(P4_LINK_OBJDUMP "${P4_LINK_TOOL_DIR}/riscv32-esp-elf-objdump")
if(NOT EXISTS "${P4_LINK_OBJDUMP}")
    message(FATAL_ERROR
        "native gamepad link audit could not find objdump beside ${P4_LINK_NM}")
endif()
execute_process(
    COMMAND "${P4_LINK_OBJDUMP}" -d "${P4_LINK_ELF}"
    RESULT_VARIABLE P4_LINK_OBJDUMP_RESULT
    OUTPUT_VARIABLE P4_LINK_OBJDUMP_OUTPUT
    ERROR_VARIABLE P4_LINK_OBJDUMP_ERROR
)
if(NOT P4_LINK_OBJDUMP_RESULT EQUAL 0)
    message(FATAL_ERROR
        "native gamepad link audit could not disassemble ${P4_LINK_ELF}: ${P4_LINK_OBJDUMP_ERROR}")
endif()
string(REGEX MATCH
    "[\r\n][^\r\n]*[ \t](jal|j|jalr)[^\r\n]*<__wrap_usb_host_device_open>"
    P4_WRAP_CALL_MATCH "${P4_LINK_OBJDUMP_OUTPUT}")
string(REGEX MATCH
    "[\r\n][^\r\n]*[ \t](jal|j|jalr)[^\r\n]*<platform_gamepad_usb_open_guard_call>"
    P4_GUARD_CALL_MATCH "${P4_LINK_OBJDUMP_OUTPUT}")
string(REGEX MATCH
    "[\r\n][^\r\n]*[ \t](jal|j|jalr)[^\r\n]*<usb_host_device_open>"
    P4_REAL_OPEN_MATCH "${P4_LINK_OBJDUMP_OUTPUT}")
if(P4_WRAP_CALL_MATCH STREQUAL "" OR P4_GUARD_CALL_MATCH STREQUAL "" OR
   P4_REAL_OPEN_MATCH STREQUAL "")
    message(FATAL_ERROR
        "native gamepad link audit: HID open guard call graph is incomplete")
endif()

message(STATUS
    "Native gamepad link audit PASS: host install, daemon, guarded HID open, HID start, and root enable retained")
