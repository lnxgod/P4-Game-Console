# Core2 virtual dice

Standalone ESP-IDF 5.5.3 firmware for the confirmed M5Stack Core2. Provides
Ready/shake/settle dice input, recorded dice clatter, distinct vibration
pulses, cached antialiased 3D cup animation, clear numbered results, and local
TRY practice. Version 0.6.0 adds confirmed tap-to-hold controls alongside motion-responsive cup collisions and a shared
impact event for recorded sound, motor pulse, and visible dice movement.

See [the accessory guide](../../docs/DICE_ACCESSORY.md) for the reusable P4MP
contract, exact build/install workflow, recovery gates, and current evidence.
The ROM identifies this device as ESP32, not ESP32-S3. Never flash this image
to a CoreS3 or Waveshare P4.

The current build targets the recorded ESP32 revision 3.1 Core2 specifically,
with an internal-RAM animation canvas, performance compilation, and bounded
changed-tile display transfers. A serial-only `dicebench` renderer check is
available while disconnected from a P4.

The renderer owns CPU1 and display DMA. CPU0 owns the motion/feedback loop
and speaker task. A bounded latest-frame mailbox avoids rendering backlogs;
landing and result transitions remain intact when frames are coalesced.

Tap landed dice to keep/release them, then Ready and shake. SAVING means the host is confirming the selection; USE P4 means start or finish the turn on the console. Held practice dice also retain their values.
