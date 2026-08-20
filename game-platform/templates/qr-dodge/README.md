# QR Dodge

QR Dodge is the compact reference game for the additive `p4.arcade` helpers.
It uses deterministic source-only logic, procedural shapes, normalized input,
and runtime-owned tone effects. There are no external or generated assets.

Pack and measure it from the repository root:

```sh
python3 game-platform/scripts/p4cart.py pack \
  game-platform/templates/qr-dodge /tmp/qr-dodge.p4cart
python3 game-platform/scripts/p4qr.py estimate /tmp/qr-dodge.p4cart
python3 game-platform/scripts/p4qr.py split \
  /tmp/qr-dodge.p4cart /tmp/qr-dodge-frames
```

The P4 Cart, QR transport, and reviewed `p4-lua-5.4-v1` execution path are
implemented. QR image rendering, scan/import UI, and on-device acceptance are
still pending.
