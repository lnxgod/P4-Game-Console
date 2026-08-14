# SPDX-License-Identifier: MIT

if(NOT DEFINED P4_LINK_NM OR P4_LINK_NM STREQUAL "" OR
   NOT DEFINED P4_LINK_ELF OR P4_LINK_ELF STREQUAL "" OR
   NOT DEFINED P4_LINK_ARM_TOKEN_SHA256)
    message(FATAL_ERROR
        "gamepad_diag ARM link audit requires nm, ELF, and token digest")
endif()

string(LENGTH "${P4_LINK_ARM_TOKEN_SHA256}" P4_ARM_DIGEST_LENGTH)
string(REGEX MATCH "^[0-9a-f]+$" P4_ARM_DIGEST_LOWER_HEX
    "${P4_LINK_ARM_TOKEN_SHA256}")
if(NOT P4_ARM_DIGEST_LENGTH EQUAL 64 OR
   NOT P4_ARM_DIGEST_LOWER_HEX STREQUAL P4_LINK_ARM_TOKEN_SHA256)
    message(FATAL_ERROR
        "gamepad_diag ARM link audit requires an exact 64-lowercase-hex digest")
endif()

execute_process(
    COMMAND "${P4_LINK_NM}" -S -a "${P4_LINK_ELF}"
    RESULT_VARIABLE P4_ARM_NM_RESULT
    OUTPUT_VARIABLE P4_ARM_NM_OUTPUT
    ERROR_VARIABLE P4_ARM_NM_ERROR)
if(NOT P4_ARM_NM_RESULT EQUAL 0)
    message(FATAL_ERROR
        "gamepad_diag ARM link audit could not inspect ELF: ${P4_ARM_NM_ERROR}")
endif()

foreach(P4_ARM_SYMBOL
        app_main
        require_host_arm
        wait_for_host_arm
        halt_before_usb
        gamepad_diag_arm_model_authorize
        mbedtls_sha256
        mbedtls_ct_memcmp
        mbedtls_platform_zeroize
        platform_usb_host_start)
    string(REGEX MATCH
        "(^|[\r\n])[^\r\n]*[ \t]${P4_ARM_SYMBOL}([\r\n]|$)"
        P4_ARM_SYMBOL_MATCH "${P4_ARM_NM_OUTPUT}")
    if(P4_ARM_SYMBOL_MATCH STREQUAL "")
        message(FATAL_ERROR
            "gamepad_diag ARM link audit: ${P4_ARM_SYMBOL} is missing")
    endif()
endforeach()

string(REGEX MATCH
    "(^|[\r\n])[0-9a-f]+[ \t]+00000041[ \t]+[A-Za-z][ \t]+s_arm_token_sha256_hex([\r\n]|$)"
    P4_ARM_DIGEST_SYMBOL_MATCH "${P4_ARM_NM_OUTPUT}")
if(P4_ARM_DIGEST_SYMBOL_MATCH STREQUAL "")
    message(FATAL_ERROR
        "gamepad_diag ARM link audit: compiled digest symbol is not 65 bytes")
endif()

get_filename_component(P4_ARM_TOOL_DIR "${P4_LINK_NM}" DIRECTORY)
set(P4_ARM_OBJDUMP "${P4_ARM_TOOL_DIR}/riscv32-esp-elf-objdump")
if(NOT EXISTS "${P4_ARM_OBJDUMP}")
    message(FATAL_ERROR "gamepad_diag ARM link audit cannot find objdump")
endif()

execute_process(
    COMMAND "${P4_ARM_OBJDUMP}" -d --disassemble=app_main "${P4_LINK_ELF}"
    RESULT_VARIABLE P4_ARM_APP_RESULT
    OUTPUT_VARIABLE P4_ARM_APP_DISASSEMBLY
    ERROR_VARIABLE P4_ARM_APP_ERROR)
execute_process(
    COMMAND "${P4_ARM_OBJDUMP}" -d --disassemble=require_host_arm
            "${P4_LINK_ELF}"
    RESULT_VARIABLE P4_ARM_REQUIRE_RESULT
    OUTPUT_VARIABLE P4_ARM_REQUIRE_DISASSEMBLY
    ERROR_VARIABLE P4_ARM_REQUIRE_ERROR)
execute_process(
    COMMAND "${P4_ARM_OBJDUMP}" -d --disassemble=wait_for_host_arm
            "${P4_LINK_ELF}"
    RESULT_VARIABLE P4_ARM_WAIT_RESULT
    OUTPUT_VARIABLE P4_ARM_WAIT_DISASSEMBLY
    ERROR_VARIABLE P4_ARM_WAIT_ERROR)
execute_process(
    COMMAND "${P4_ARM_OBJDUMP}" -d --disassemble=halt_before_usb
            "${P4_LINK_ELF}"
    RESULT_VARIABLE P4_ARM_HALT_RESULT
    OUTPUT_VARIABLE P4_ARM_HALT_DISASSEMBLY
    ERROR_VARIABLE P4_ARM_HALT_ERROR)
if(NOT P4_ARM_APP_RESULT EQUAL 0 OR NOT P4_ARM_REQUIRE_RESULT EQUAL 0 OR
   NOT P4_ARM_WAIT_RESULT EQUAL 0 OR NOT P4_ARM_HALT_RESULT EQUAL 0)
    message(FATAL_ERROR
        "gamepad_diag ARM link audit disassembly failed: ${P4_ARM_APP_ERROR}${P4_ARM_REQUIRE_ERROR}${P4_ARM_WAIT_ERROR}${P4_ARM_HALT_ERROR}")
endif()

string(REGEX MATCH
    "[\r\n][^\r\n]*[ \t](jal|jalr)[^\r\n]*<require_host_arm>"
    P4_ARM_REQUIRE_CALL "${P4_ARM_APP_DISASSEMBLY}")
string(REGEX MATCH
    "[\r\n][^\r\n]*[ \t](jal|jalr)[^\r\n]*<platform_usb_host_start>"
    P4_ARM_HOST_CALL "${P4_ARM_APP_DISASSEMBLY}")
if(P4_ARM_REQUIRE_CALL STREQUAL "" OR P4_ARM_HOST_CALL STREQUAL "")
    message(FATAL_ERROR
        "gamepad_diag ARM link audit: app arm/host call graph is incomplete")
endif()
string(REGEX MATCHALL
    "[\r\n][^\r\n]*[ \t](jal|jalr)[^\r\n]*<require_host_arm>"
    P4_ARM_REQUIRE_CALLS "${P4_ARM_APP_DISASSEMBLY}")
string(REGEX MATCHALL
    "[\r\n][^\r\n]*[ \t](jal|jalr)[^\r\n]*<platform_usb_host_start>"
    P4_ARM_HOST_CALLS "${P4_ARM_APP_DISASSEMBLY}")
list(LENGTH P4_ARM_REQUIRE_CALLS P4_ARM_REQUIRE_CALL_COUNT)
list(LENGTH P4_ARM_HOST_CALLS P4_ARM_HOST_CALL_COUNT)
if(NOT P4_ARM_REQUIRE_CALL_COUNT EQUAL 1 OR
   NOT P4_ARM_HOST_CALL_COUNT EQUAL 1)
    message(FATAL_ERROR
        "gamepad_diag ARM link audit: app_main must contain one arm gate and one host start")
endif()
string(FIND "${P4_ARM_APP_DISASSEMBLY}" "${P4_ARM_REQUIRE_CALL}"
    P4_ARM_REQUIRE_POSITION)
string(FIND "${P4_ARM_APP_DISASSEMBLY}" "${P4_ARM_HOST_CALL}"
    P4_ARM_HOST_POSITION)
if(P4_ARM_REQUIRE_POSITION LESS 0 OR P4_ARM_HOST_POSITION LESS 0 OR
   NOT P4_ARM_REQUIRE_POSITION LESS P4_ARM_HOST_POSITION)
    message(FATAL_ERROR
        "gamepad_diag ARM link audit: arm requirement does not dominate first host start")
endif()

string(REGEX MATCH
    "[\r\n][^\r\n]*[ \t](jal|jalr)[^\r\n]*<wait_for_host_arm>"
    P4_ARM_WAIT_CALL "${P4_ARM_REQUIRE_DISASSEMBLY}")
string(REGEX MATCH
    "[\r\n][^\r\n]*[ \t](jal|jalr)[^\r\n]*<halt_before_usb>"
    P4_ARM_HALT_CALL "${P4_ARM_REQUIRE_DISASSEMBLY}")
if(P4_ARM_WAIT_CALL STREQUAL "" OR P4_ARM_HALT_CALL STREQUAL "")
    message(FATAL_ERROR
        "gamepad_diag ARM link audit: accepted/rejected branch is incomplete")
endif()
string(REGEX MATCH
    "[\r\n][^\r\n]*[ \t](beq|bne|blt|bge|bltu|bgeu)[^\r\n]*<require_host_arm\\+0x[0-9a-f]+>"
    P4_ARM_REJECT_BRANCH "${P4_ARM_REQUIRE_DISASSEMBLY}")
if(P4_ARM_REJECT_BRANCH STREQUAL "")
    message(FATAL_ERROR
        "gamepad_diag ARM link audit: require_host_arm has no conditional reject branch")
endif()
string(FIND "${P4_ARM_REQUIRE_DISASSEMBLY}" "${P4_ARM_WAIT_CALL}"
    P4_ARM_WAIT_POSITION)
string(FIND "${P4_ARM_REQUIRE_DISASSEMBLY}" "${P4_ARM_REJECT_BRANCH}"
    P4_ARM_REJECT_POSITION)
string(FIND "${P4_ARM_REQUIRE_DISASSEMBLY}" "${P4_ARM_HALT_CALL}"
    P4_ARM_HALT_POSITION)
if(P4_ARM_WAIT_POSITION LESS 0 OR P4_ARM_REJECT_POSITION LESS 0 OR
   P4_ARM_HALT_POSITION LESS 0 OR
   NOT P4_ARM_WAIT_POSITION LESS P4_ARM_REJECT_POSITION OR
   NOT P4_ARM_REJECT_POSITION LESS P4_ARM_HALT_POSITION)
    message(FATAL_ERROR
        "gamepad_diag ARM link audit: wait/reject/halt control flow is not ordered")
endif()

string(FIND "${P4_ARM_REQUIRE_DISASSEMBLY}" "<platform_usb_host_"
    P4_ARM_REQUIRE_USB_POSITION)
string(FIND "${P4_ARM_WAIT_DISASSEMBLY}" "<platform_usb_host_"
    P4_ARM_WAIT_USB_POSITION)
if(NOT P4_ARM_REQUIRE_USB_POSITION LESS 0 OR
   NOT P4_ARM_WAIT_USB_POSITION LESS 0)
    message(FATAL_ERROR
        "gamepad_diag ARM link audit: pre-accept functions contain a USB Host call")
endif()

string(FIND "${P4_ARM_HALT_DISASSEMBLY}" "<vTaskSuspend>"
    P4_ARM_SUSPEND_POSITION)
string(REGEX MATCH "[\r\n][^\r\n]*[ \t]ret([\r\n]|$)"
    P4_ARM_HALT_RETURN "${P4_ARM_HALT_DISASSEMBLY}")
if(P4_ARM_SUSPEND_POSITION LESS 0 OR NOT P4_ARM_HALT_RETURN STREQUAL "")
    message(FATAL_ERROR
        "gamepad_diag ARM link audit: reject halt is not a permanent suspension")
endif()

foreach(P4_ARM_CRYPTO_SYMBOL
        gamepad_diag_arm_model_authorize
        mbedtls_sha256
        mbedtls_ct_memcmp
        mbedtls_platform_zeroize)
    string(FIND "${P4_ARM_WAIT_DISASSEMBLY}" "<${P4_ARM_CRYPTO_SYMBOL}>"
        P4_ARM_CRYPTO_CALL_POSITION)
    if(P4_ARM_CRYPTO_CALL_POSITION LESS 0)
        message(FATAL_ERROR
            "gamepad_diag ARM link audit: ${P4_ARM_CRYPTO_SYMBOL} is not retained in wait path")
    endif()
endforeach()

# Convert the expected ASCII digest to hex without creating another literal in
# the application. The final ELF must carry the exact digest once and only once.
set(P4_ARM_EXPECTED_HEX "")
string(LENGTH "${P4_LINK_ARM_TOKEN_SHA256}" P4_ARM_INPUT_LENGTH)
math(EXPR P4_ARM_LAST_INDEX "${P4_ARM_INPUT_LENGTH} - 1")
foreach(P4_ARM_INDEX RANGE 0 ${P4_ARM_LAST_INDEX})
    string(SUBSTRING "${P4_LINK_ARM_TOKEN_SHA256}" ${P4_ARM_INDEX} 1
        P4_ARM_CHARACTER)
    string(HEX "${P4_ARM_CHARACTER}" P4_ARM_CHARACTER_HEX)
    string(APPEND P4_ARM_EXPECTED_HEX "${P4_ARM_CHARACTER_HEX}")
endforeach()
file(READ "${P4_LINK_ELF}" P4_ARM_ELF_HEX HEX)
string(REGEX MATCHALL "${P4_ARM_EXPECTED_HEX}" P4_ARM_DIGEST_MATCHES
    "${P4_ARM_ELF_HEX}")
list(LENGTH P4_ARM_DIGEST_MATCHES P4_ARM_DIGEST_MATCH_COUNT)
if(NOT P4_ARM_DIGEST_MATCH_COUNT EQUAL 1)
    message(FATAL_ERROR
        "gamepad_diag ARM link audit: expected one digest literal, found ${P4_ARM_DIGEST_MATCH_COUNT}")
endif()

message(STATUS
    "Gamepad diagnostic ARM link audit PASS: unique digest; arm accept/reject dominates first host start")
