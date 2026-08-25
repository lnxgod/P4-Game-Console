# Byte Buddy improvement TODO

This is the working list for the Dragon game's fluidity, pacing, progression,
and new art. Keep gameplay changes deterministic and bounded on the 320x200
P4 Game API surface.

## Tonight's pass

- [x] Record clean baseline sanitizer results for the focused game and SDL host.
- [x] Audit growth, level, Star Catcher, animation, and resource-art timing.
- [x] Generate a crop-safe 4x4 ImageGen atlas for Star Catcher rewards/effects.
- [x] Make Star Catcher movement depend on elapsed time instead of frame count.
- [x] Add a gentle, capped pace curve that rewards streaks without becoming frantic.
- [x] Slow late growth and level gains with explicit, test-covered curves.
- [x] Show the player stage progress and current mini-game pace.
- [x] Add controller/keyboard Star Catcher access with Start, Left/Right, and B.
- [x] Fill text-only care, upgrade, and style-item graphics with one compact atlas.
- [x] Add deterministic Remix recipes that combine owned body, eye, horn, trail,
      wing, and mutation components into up to 19,200 numbered looks.
- [x] Inspect 320x200 Home, Power, and Style previews plus a visible SDL
      Start/Star Catcher/B/Back smoke with the regenerated 24-sheet resource.
- [x] Integrate the new star, calm crystal, heart comet, and catch-burst art.
- [x] Regenerate the deterministic `.P4R` payload and update provenance/hashes.
- [x] Add missing Signal Hunt graphics to Home, scan results, tracking, and battle.
- [x] Build a deterministic signal genome from reusable core, halo, sigil,
      aura, palette, and rarity traits for 8,192 stable encounter designs.
- [x] Generate, inspect, and integrate the 4x4 Signal Genome ImageGen atlas.
- [x] Regenerate the 25-sheet/400-frame `.P4R` payload and record its hashes.
- [x] Run focused unit/sanitizer tests and inspect chronological preview frames.
- [x] Replace the one-win mature mutation with five cumulative lineage ranks
      gated by both unique signal count and inherited genome diversity.
- [x] Make lineage aggregation order-independent and retain rarity-weighted
      core, halo, sigil, aura, hue, channel-family, protected, and hidden traits.
- [x] Reuse the existing signal-genome layers for progressive Crest, Aurora,
      shield, phantom, dual-hue, and Mythic dragon effects.
- [x] Cap lineage battle bonuses and taper signal-fed growth so exploration
      adds variety without rushing the normal care curve.
- [x] Render and inspect one-signal Spark and five-signal Aurora animation clips
      in the deterministic 156-frame preview sequence.
- [x] Complete a visible SDL keyboard play pass: Start entered Star Catcher,
      Left steered through the spring path, one 18-second round ended naturally
      with two catches, B returned from a second round, and Back exited cleanly.
- [ ] Have a person complete a coordinate-accurate mouse/touch drag pass; the
      available desktop-control API delivered every requested click at one fixed
      point, so direct drag feel remains a separate follow-up.

## Follow-up tuning

- [ ] Collect player feedback on early egg time, first flight time, and mature-dragon time.
- [ ] Tune Star Catcher round length, reward frequency, and touch spring after play feedback.
- [ ] Add a short first-session tutorial that disappears after the first successful catch.
- [ ] Add explicit non-development stage preview access before a release candidate.
- [ ] Decide whether session progress should opt into the reviewed save service on boards
      that expose it; Byte Buddy must remain usable without writable storage.
- [ ] Add a second Signal Battle pattern so encounters are not only rapid tapping.
- [ ] Reconcile the public session-token contract with the device-key behavior
      before considering durable lineages or writable save integration.
- [ ] Clear stale results in platform SCANNING/ERROR snapshots and distinguish
      a transient busy scan request from true radio unavailability.
- [ ] Add Signal Hunt paging so all eight bounded OS results are selectable.
- [ ] Perform named tablet acceptance for touch feel, panel motion, speaker feedback,
      launcher return, and exact `.P4G`/`.P4R` hashes before calling the work hardware-tested.

## Balance targets

- First hatch remains quick enough to reveal the pet loop.
- Later stages take progressively longer, with no sudden grind wall.
- Star Catcher starts relaxed, accelerates only through successful play, eases after a
  miss or calm-crystal catch, and has a hard speed cap.
- Frame-rate changes do not change catcher or falling-item speed.
- A new player can earn an upgrade in the first session without the economy exploding.
