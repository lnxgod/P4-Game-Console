#!/bin/sh
set -eu
P4_TEST_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
P4_TEST_OUTPUT="$P4_TEST_ROOT/build-host/dice-core2-feedback"
mkdir -p "$P4_TEST_OUTPUT"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I "$P4_TEST_ROOT/apps/dice_core2/tests/fakes" \
    -I "$P4_TEST_ROOT/apps/dice_core2/main" \
    -I "$P4_TEST_ROOT/apps/dice_core2/assets" \
    -I "$P4_TEST_ROOT/components/p4_game_api/include" \
    "$P4_TEST_ROOT/apps/dice_core2/main/dice_feedback.cpp" \
    "$P4_TEST_ROOT/apps/dice_core2/tests/test_dice_feedback.cpp" \
    -o "$P4_TEST_OUTPUT/test-feedback"
"$P4_TEST_OUTPUT/test-feedback"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I "$P4_TEST_ROOT/apps/dice_core2/main" \
    "$P4_TEST_ROOT/apps/dice_core2/main/dice_cup.cpp" \
    "$P4_TEST_ROOT/apps/dice_core2/tests/test_dice_cup.cpp" \
    -o "$P4_TEST_OUTPUT/test-cup"
"$P4_TEST_OUTPUT/test-cup"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I "$P4_TEST_ROOT/apps/dice_core2/main" \
    "$P4_TEST_ROOT/apps/dice_core2/main/dice_dirty.cpp" \
    "$P4_TEST_ROOT/apps/dice_core2/tests/test_dice_dirty.cpp" \
    -o "$P4_TEST_OUTPUT/test-dirty"
"$P4_TEST_OUTPUT/test-dirty"

c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I "$P4_TEST_ROOT/apps/dice_core2/main" \
    -I "$P4_TEST_ROOT/components/p4_game_api/include" \
    "$P4_TEST_ROOT/apps/dice_core2/tests/test_dice_presentation.cpp" \
    -o "$P4_TEST_OUTPUT/test-presentation"
"$P4_TEST_OUTPUT/test-presentation"

c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I "$P4_TEST_ROOT/apps/dice_core2/main" \
    -I "$P4_TEST_ROOT/components/p4_game_api/include" \
    "$P4_TEST_ROOT/apps/dice_core2/tests/test_dice_controls.cpp" \
    -o "$P4_TEST_OUTPUT/test-controls"
"$P4_TEST_OUTPUT/test-controls"
