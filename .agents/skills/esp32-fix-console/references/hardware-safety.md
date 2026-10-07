# Hardware safety gates

## Before flashing

- Do not create or refresh a firmware backup, including before a new unit's first write. Backups run only when the user explicitly requests a backup.
- Firmware backups, snapshots and backup manifests are never required for flashing, on any board or route. Preserve existing recovery files and manifests. Recovery can rebuild old source; validate the device and reviewed artifacts directly.
- Confirm the exact unit, board/revision, detected flash size, security state, partition layout, predecessor and authorized artifact. Bind identity with a SHA-256; never store or print the raw identifier.
- Start with pin-independent firmware when the exact PCB profile is unresolved.

Preserve current user data affected by a layout migration. Use firmware
supported by the selected board; the current Tab5 build route supports Console
OS, not legacy `bringup` apps.

## Explicitly requested firmware backup

Only for a user-requested backup, read the full detected flash, store the binary
outside Git, and record its byte count, SHA-256 and hashed live-device binding
in `hardware/backups/manifest.json`. A flash request alone does not select this
operation.

## Before enabling a peripheral

- Verify the signal and GPIO in the schematic for the exact SKU and revision. If the revision is unreadable, a bounded subsystem may proceed only after comparing every published revision and proving the complete path invariant.
- Verify signal direction, voltage, required rails, active polarity, and shared-pin conflicts.
- Set a conservative clock first, then increase it only with evidence.
- Ensure a peripheral cannot source power into the board or another supply unexpectedly.
- Record a scoped authorization that names every allowed rail, dedicated interface, and GPIO, and explicitly denies unneeded or revision-sensitive pins. Global `pin_map_authorized: false` remains in force for everything outside that scope.
- If an exact-unit owner explicitly directs replay of a previously working
  factory peripheral despite a documented unresolved topology risk, preserve
  that as a separate operator-accepted exception: bind one hashed identity,
  pinned factory source, prior nondamaging evidence, exact GPIOs, and the
  accepted risk. Do not rewrite the topology review or expand
  the exception to another unit, revision, or peripheral.

## USB-specific rule

USB data connectivity does not imply host-power capability. A valid host fixture needs a controlled and current-limited 5 V source, common ground, correctly routed D+/D-, and backfeed protection. Review its schematic before connecting it to the CrowPanel.

## Recovery

Prefer a targeted app/partition write over an erase. Routine authorized Tab5 successors use device checksum verification; use full readback only for recovery, diagnostics or an explicit request, and record which method ran. Other boards retain their device/image verification contracts without requiring firmware backups. If recovery is required, rebuild the reviewed old source, bind the resulting artifact to the exact live identity and layout, and make restoration a separate, explicit operation. An existing matching recovery image can also be used when explicitly selected; it is never a flashing prerequisite.
