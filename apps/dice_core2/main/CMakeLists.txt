idf_component_register(SRCS "dice_core2.cpp" "dice_feedback.cpp" "dice_view.cpp" "dice_cup.cpp" "dice_dirty.cpp" INCLUDE_DIRS "." "../assets"
    REQUIRES m5stack__m5unified bt nvs_flash esp_timer esp_system p4_multiplayer)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)

target_add_binary_data(${COMPONENT_LIB} "../assets/dice-atlas.rgb565" BINARY RENAME_TO "dice_atlas_rgb565")
target_add_binary_data(${COMPONENT_LIB} "../assets/dice-audio.pcm" BINARY RENAME_TO "dice_audio_pcm")
