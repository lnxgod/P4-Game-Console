# Doom native gamepad plus sound compile/link composite

`doom_embedded_gamepad_audio` is a separate E4 build-only image. It preserves
the E1, E2, and E3 sources and artifacts while proving that the full native USB
input graph and the existing Doom SFX graph can coexist in one pinned ESP32-P4
link. It uses no Mac relay.

The committed image is electrically inert. Three independent app-local,
flash-resident volatile source gates are immutable zeroes. `app_main` checks all of them before
the first display, USB, GPIO30, I2S, or other game-platform hardware
initialization and returns with a
`P4_DOOM_E4 BLOCKED` marker. The separate translation unit and volatile reads
retain the WAD, sound-enabled Doom engine, mixer/worker, display, native USB,
HID, normalization, and input-adapter branches in the build. The reusable USB
fixture policy is independently false and its fallback evidence is invalid by
construction; fixture authorization alone can never enable E4 audio. No
Kconfig toggle can turn this compile/link artifact into a runtime image.

The intended dependency directions are:

```text
platform_usb_host -> platform_gamepad_usb -> gamepad_core
    -> doom_gamepad_input -> DG_GetKey

doom sound module -> doom_audio mixer/worker -> platform_audio
```

Sound effects are compiled with the sound-enabled engine and `-nomusic`.
Music remains disabled. The existing E2 memory policy keeps the SFX cache
PSRAM-eligible, and its conservative stop/unbind/destroy/recover lifecycle and
audio diagnostics are retained in the guarded branch.

## Audio hard block

The linked `platform_audio` direct-I2S implementation is a compile/link seam
only. It is not the final sound path and must not be made runnable by changing
the E4 gate. The latest cross-revision review found two mutually exclusive BOM
possibilities whose outputs can converge at J4/J6: the expected ES8311-to-U4
analog path and an optional NS4168 direct-I2S path. Connected-unit population
and isolation have not been proven. The linked implementation also waits only
20 ms after GPIO30 enable, while the current U4 review requires at least 350 ms
of zero data before nonzero PCM.

A future runnable sound image therefore needs one of these evidence-backed
replacements, plus a fresh exact runtime authorization:

- a codec-only backend after ES8311 power, I2C, GPIO24 MCLK, configuration,
  and U4-only output topology are proven; or
- a direct-I2S backend only after the NS4168 data/control/power/output parts
  are physically proven populated and the U4 outputs are absent or isolated.

Do not flash or run E4, drive GPIO30 low, or infer an audio authorization from
this build.

## Controls in the guarded branch

- D-pad or left stick: move/turn
- South button or right trigger: fire; South also accepts menus
- East: use/open
- West or left-stick click: run
- Left trigger: strafe modifier
- Left/right shoulder: strafe left/right
- Back: menu back; Guide: automap; Start: pause
- North/right-stick click: next/previous weapon

## Build-only verification

```sh
make gamepad-host
make doom-audio-host
make platform-audio-host
make build APP=doom_embedded_gamepad_audio
```

A successful build proves compilation and linkage only. It is not controller,
audio, display, fixture, or full runtime hardware evidence. All flash and
runtime authorization flags remain false.
