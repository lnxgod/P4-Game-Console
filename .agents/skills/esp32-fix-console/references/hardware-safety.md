# Hardware safety gates

## Before a new unit's first project write

- Read and hash the full external flash.
- Store the binary outside Git and its byte count/hash in `hardware/backups/manifest.json`.
- Bind the backup to the physical unit with the SHA-256 of its normalized base identity. Never store or print the raw identifier.
- Confirm the detected flash size before reading or restoring.
- Start with pin-independent firmware when the exact PCB profile is unresolved.

For an exact unit already registered in the manifest, verify and reuse its
recorded backup rather than repeating the factory read. Preserve current data
affected by a layout migration. Use firmware supported by the selected board;
the current Tab5 build route supports Console OS, not legacy `bringup` apps.

## Before enabling a peripheral

- Verify the signal and GPIO in the schematic for the exact SKU and revision. If the revision is unreadable, a bounded subsystem may proceed only after comparing every published revision and proving the complete path invariant.
- Verify signal direction, voltage, required rails, active polarity, and shared-pin conflicts.
- Set a conservative clock first, then increase it only with evidence.
- Ensure a peripheral cannot source power into the board or another supply unexpectedly.
- Record a scoped authorization that names every allowed rail, dedicated interface, and GPIO, and explicitly denies unneeded or revision-sensitive pins. Global `pin_map_authorized: false` remains in force for everything outside that scope.
- If an exact-unit owner explicitly directs replay of a previously working
  factory peripheral despite a documented unresolved topology risk, preserve
  that as a separate operator-accepted exception: bind one hashed identity,
  pinned factory source, prior nondamaging evidence, exact GPIOs, recovery
  bytes, and the accepted risk. Do not rewrite the topology review or expand
  the exception to another unit, revision, or peripheral.

## USB-specific rule

USB data connectivity does not imply host-power capability. A valid host fixture needs a controlled and current-limited 5 V source, common ground, correctly routed D+/D-, and backfeed protection. Review its schematic before connecting it to the CrowPanel.

## Recovery

Prefer a targeted app/partition write over an erase. Routine authorized Tab5 successors use device checksum verification; use full readback only for recovery, diagnostics or an explicit request, and record which method ran. Other boards keep their exact installer verification contract. If recovery is required, require the live identity hash to match the backup, identify the exact flash offsets from the saved partition table, and make restoration a separate, explicit operation.
