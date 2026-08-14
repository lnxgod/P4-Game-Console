idf_component_register(
    SRCS
        "gamepad_diag_arm_model.c"
        "gamepad_diag_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_rom
        esp_timer
        freertos
        log
        mbedtls
        platform_gamepad_usb
        platform_usb_host
)

# An active one-shot build receives only SHA-256(token), never the random
# 256-bit arm token itself.  The private token remains in the host-side 0600
# reservation/receipt.  Inactive builds compile a deliberately impossible
# all-zero digest so the complete handshake stays linked without creating an
# armable artifact.
set(P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256_HEX
    "$ENV{P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256}")
set(P4_GAMEPAD_DIAG_ZERO_ARM_TOKEN_SHA256
    "0000000000000000000000000000000000000000000000000000000000000000")

if(P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256_HEX STREQUAL "")
    if(CONFIG_PLATFORM_USB_HOST_FIXTURE_AUTHORIZED)
        message(FATAL_ERROR
            "Active gamepad_diag build requires P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256")
    endif()
    set(P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256_HEX
        "${P4_GAMEPAD_DIAG_ZERO_ARM_TOKEN_SHA256}")
elseif(NOT CONFIG_PLATFORM_USB_HOST_FIXTURE_AUTHORIZED)
    message(FATAL_ERROR
        "Refusing an arm-token digest in an electrically inactive gamepad_diag build")
endif()

string(LENGTH "${P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256_HEX}"
       P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256_LENGTH)
string(REGEX MATCH "^[0-9a-f]+$" P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256_MATCH
       "${P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256_HEX}")
if(NOT P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256_LENGTH EQUAL 64 OR
   NOT P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256_MATCH STREQUAL
       P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256_HEX)
    message(FATAL_ERROR
        "P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256 must be exactly 64 lowercase hex digits")
endif()
if(CONFIG_PLATFORM_USB_HOST_FIXTURE_AUTHORIZED AND
   P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256_HEX STREQUAL
       P4_GAMEPAD_DIAG_ZERO_ARM_TOKEN_SHA256)
    message(FATAL_ERROR "Active gamepad_diag arm-token digest cannot be all zero")
endif()

# The app-level post-link audit is declared after project(), outside this
# component directory scope. Publish the exact resolved public digest without
# ever publishing the private token preimage.
set_property(GLOBAL PROPERTY P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256_HEX
    "${P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256_HEX}")

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    "P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256_HEX=\"${P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256_HEX}\""
)

target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wshadow
    -Werror
)
