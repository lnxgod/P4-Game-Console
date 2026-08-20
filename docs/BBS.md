# P4 BBS interface

Console OS treats the BBS as a platform interface, not a painted launcher
screen. The Waveshare 4.3 home view is an 80-column, 30-row CP437 terminal
rendered natively into the centered 768x480 content viewport. Games continue
to receive the stable 320x200 Game API surface and never depend on terminal
geometry.

The visual language deliberately resembles a 1990s dial-up board: DOS colors,
CP437 boxes and shading, terse node/baud/carrier status, numbered doors, and
keyboard-first prompts. `WAREZ` is an aesthetic reference only. The platform
catalog is for original, open-source, freely redistributable, or creator-owned
games and assets. A server must not advertise or transfer unauthorized
commercial software.

## Components

`components/p4_ansi` is the terminal primitive. It owns the pinned 8x16 CP437
font, DOS 16-color palette, 80x30 cell grid, ECMA-48 parser, and RGB565
renderer. The parser bounds parameters and escape-sequence length, discards
OSC/DCS-style strings, ignores unsupported controls, and never allocates.
Remote bytes are untrusted input.

Bold printable ASCII is rendered with a one-pixel right-hand stroke inside its
existing 9x16 cell. This makes launcher headings, door names, and controls
slightly larger and easier to read without changing the 80x30 geometry, CP437
line art, 768x480 framebuffer, or touch hit regions.

`components/p4_bbs` is the product UI. `p4_bbs_build_launcher()` consumes a
sanitized six-door page model and emits ANSI through the same parser used for
remote boards. `p4_bbs_hit_test()` maps native pixels back to door or Back
controls, so touch, mouse, keyboard, and gamepad selection share one layout.
`p4_bbs_build_boot_screen()` renders the ANSI adaptation of the official Game
Changers AI lightbulb/circuit mark and the POST, disk, dialing, V.22bis,
directory-sync, connected, and degraded phases. The final handoff reports
`CONNECT 2400`, briefly holds carrier detect, and reveals the launcher from
top to bottom like a remote BBS drawing its first ANSI screen. Catalog refresh
uses connection language instead of a generic loading message.

Console Shell uses the BBS model only for the native Waveshare home page.
Selecting Appearance > Windows 3.1 returns to the existing window manager.
All detail pages remain reachable in either mode.

## Local and remote boards

A local board builds its door page from the validated SD-card catalog. A
remote board sends bounded CP437/ECMA-48 screen updates into `p4_ansi_write()`;
the terminal renderer does not care whether bytes originated locally, over
H1 serial, or from a future network transport. Input is normalized by Console
OS before a transport sees it.

The first wired transport is H1 through the on-board CH343 USB-to-UART bridge.
H2 stays controller-first USB host so a BBS session never disconnects the
player's wired controller. Production traffic must use an explicit framed
session and must not be mixed ambiguously with boot logs. The existing bounded
P4MP stream framing remains the reference for magic resynchronization, payload
limits, sequence numbers, and CRC32; BBS catalog/file-transfer message types
will be frozen separately rather than pretending the multiplayer packet types
are interchangeable.

The central service will eventually provide:

1. badge enrollment and a visible local/remote board directory;
2. paged door catalogs with exact package ID, version, size, license, and
   SHA-256 identity;
3. resumable upload/download to a staging area followed by validation and an
   atomic SD-card install;
4. creator-approved game trading and remix provenance;
5. BBS messages, scores, and multiplayer lobby rendezvous without giving a
   game raw serial, USB, filesystem, or network ownership.

Binary packages and multiplayer data are separate from ANSI display bytes.
The server cannot make an unverified package executable merely by drawing a
door for it.

## Host previews

Run `make p4-bbs-host` for the bounded parser/UI tests. The generated tools can
render native PPM previews without firmware:

```sh
build-host/p4_bbs/tools/p4_bbs_preview /tmp/p4-bbs-home.ppm
build-host/p4_bbs/tools/p4_bbs_preview boot /tmp/p4-bbs-boot.ppm
build-host/console_shell/tools/console_shell_bbs_preview home \
  /tmp/p4-console-bbs.ppm
```
