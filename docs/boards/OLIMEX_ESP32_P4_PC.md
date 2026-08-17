# Olimex ESP32-P4-PC Rev.B

This is the development-board profile for P4 Console OS. It is additive: the
Elecrow 10 in variant remains the default and its window-manager UI is shared,
not replaced.

## Supported baseline

The profile is pinned to Olimex's Rev.B schematic, manual, and production-test
source at commit `99a802ec029f531692102b2c754e20dae2226038`:

- ESP32-P4NRW32, 16 MiB flash, 32 MiB PSRAM;
- 1280x720p60 HDMI through LT8912B, I2C1 GPIO7/GPIO8, two DSI lanes;
- four-bit microSD on CLK/CMD/D0-D3 GPIO43/44/39/40/41/42, active-low power
  gate GPIO45;
- P4 high-speed USB through the onboard powered FE1.1s four-port USB-A hub,
  active-low hub reset GPIO21;
- native USB-C Serial/JTAG for programming and monitoring;
- ES8311 output on shared I2C1 GPIO7/GPIO8 and I2S1 GPIO13/12/10/9, with
  active-high amplifier control GPIO53 and the 3.5mm audio jack;
- no touch panel.

The exact source paths and SHA-256 values are in
`hardware/boards/olimex-esp32-p4-pc-rev-b.json`. Source review authorizes the
build, not a write to an unbacked device.

## Build

Prepare the ignored, hash-pinned Doom shareware input exactly as documented in
`AGENTS.md`, then run:

```sh
make console-os-olimex-idf
```

This performs focused board, shell, storage, and HID host checks; builds with
the locked ESP-IDF and component versions; and verifies the final partition
table, source graph, symbols, cartridges, WAD, and update package. Outputs are
under `apps/console_os/build-olimex-esp32-p4-pc/`.

Do not add a repo-wide test run or repeat the unchanged firmware build. Run the
Elecrow build as well only when shared Console OS or board-selection code has
changed.

## Prepare game storage

The build emits an `sd-card/` tree containing:

```text
GAMES/MAZE.P4G
GAMES/INVADERS.P4G
DOOM1.WAD
README.TXT
UPDATE/P4UPDATE.P4U
```

Power the P4-PC off before removing or installing its card. Mount a FAT card in
the laptop and use an explicit existing mount point:

```sh
make install-olimex-sd-card SD_MOUNT=/Volumes/P4GAMES
```

The copier validates every source, refuses root/home/repository and non-mount
destinations, writes each known file atomically, verifies it, and leaves
unrelated files alone. Eject the card cleanly before moving it back. Console OS
mounts it at `/game-data`, never formats it, and does not support hot-removal.
USB-C does not expose this filesystem to the laptop.

Game Manager validates/removes `GAMES/*.P4G` cartridges and accepts root
packages from older cards. File Manager lists and
removes other bounded regular root files after confirmation. Selecting the OS
update validates `UPDATE/P4UPDATE.P4U`, streams it to the inactive OTA slot,
and changes the boot target only after its final digest passes.

## Input

Plug peripherals into the USB-A host ports. The transport supports one generic
HID/DirectInput gamepad, one boot keyboard, and one boot mouse concurrently.
Xbox/XInput/GIP devices are not covered by this generic HID tier.

- gamepad D-pad or keyboard arrow/WASD: move selection;
- gamepad A or keyboard Z/Space/Enter: accept;
- gamepad B or keyboard X/Escape/Backspace: back;
- keyboard R/F5 or mouse middle: refresh;
- mouse left: point/activate; right: back; wheel: move selection.

Native games see normalized controls. Short/malformed reports fail closed, and
unplugging a device neutralizes its held state before the next game update.

## First-device flash and acceptance gate

Before any write, enter the ROM loader through the board's documented
BOOT/RESET controls and create a complete 16 MiB backup with the repository
backup command. Hold BOOT, tap and release RST, then release BOOT and use the
manual-loader route so the native USB probe does not reset back into the
factory application:

```sh
./scripts/backup-flash.sh --manual-loader \
  --port /dev/cu.usbmodemXXXX \
  --output hardware/backups/olimex-esp32-p4-pc-rev-b-factory-before-project.bin
```

Record its byte count and SHA-256 and bind it to a hash of the live device
identity without storing the raw identifier. Only then may
`flash_authorized` be reviewed and enabled for that exact unit.

The first full flash must use the generated bootloader, partition table, OTA
data, and OTA-0 image together because this profile has two 8,323,072-byte OTA
slots and no internal game-data partition. After readback verification, record:

1. serial boot markers and first Program Manager frame;
2. stable centered HDMI output with the existing window-manager design;
3. microSD mount plus Maze Chase and Space Invaders launch/return;
4. one named gamepad's VID/PID, class/protocol, descriptor SHA-256, mapping,
   cold/hot plug, held-control unplug neutralization, and reconnect behavior;
5. simultaneous keyboard, mouse, and gamepad navigation;
6. Doom launch from the exact SD-backed shareware WAD, video progress, audible
   sound, all three input types, and clean return/reboot behavior;
7. a verified `.P4G` add/remove cycle and inactive-slot `.P4U` install;
8. at least 30 minutes of continuous play for a controller support claim.

Until these observations are captured, label the port build-tested, not
hardware-tested. The image includes the complete Doom display, ES8311 audio,
and USB-input handoff, but a successful build does not prove those circuits on
the connected unit.
