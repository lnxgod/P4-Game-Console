# Byte Buddy improvement TODO

This is the working list for the Dragon game's fluidity, pacing, progression,
and new art. Keep gameplay changes deterministic and bounded on the 320x200
P4 Game API surface.

## Graphics polish v4.1 candidate

- [x] Make Ward, Echo, Siphon, and Overclock animation event-driven so authored
      trigger/fade frames correspond to real battle mechanics.
- [x] Align dragon clip changes to complete loops, keep calm reactions out of
      attack poses, and remove wrap/reset pops from particles, trails, city
      stars, habitat orbits, and Star Catcher streaks.
- [x] Compose core, halo, sigil, aura, hue, rarity, habitat, and shield/phantom
      details into every compact signal icon instead of dropping layers at
      list scale.
- [x] Make the Genome art use the lineage traits shown in its text, including
      rarity and band diversity, with a truthful dormant state.
- [x] Capture actual passive procs, add pairwise visual-distinction checks for
      every genome dimension and all twenty habitats, and inspect the complete
      112-frame authored route.
- [x] Pass the focused ASan/UBSan game suite (5/5), SDL host smoke/save suite
      (2/2), and visual review without adding placeholder art or another atlas.
- [x] Build and verify the deterministic v4.1 `.P4G` candidate and run the
      manifest/registry gate.
- [ ] Complete named human SDL and physical-tablet acceptance before deploying
      or describing v4.1 as play-tested or hardware-tested.

## Full graphics v4 pass

- [x] Audit every reachable care, activity, shop, growth, scan, tracker,
      battle, reward, loss, lineage, and transition state for missing authored
      graphics and incomplete animation chronology, including the seven legacy
      slots that can be reclaimed without increasing the bounded frame budget.
- [x] Generate and visually inspect seven original crop-safe 4x4 atlases for
      signal counters, outcomes, passives, scan states, evolution, needs, and
      Star Catcher/activity feedback.
- [x] Integrate every v11-v17 row into its reachable runtime state and remove
      the corresponding procedural or static visual stand-ins.
- [x] Give each newly authored action a complete chronological animation with
      deterministic start, anticipation, action, impact, recovery, and exit
      timing where those phases apply.
- [x] Extend deterministic previews and unit tests to cover every new row,
      every battle counter/passive/outcome, all scan phases, each need, all
      activity outcomes, and every evolution milestone. Pin exactly one
      fail-closed semantic digest for all 112 authored coverage frames.
- [x] Queue Winged, Flying, and Elemental milestones independently so multiple
      signal-earned stages play in order after returning Home; verify a
      27-signal uninterrupted route does not collapse the ceremonies.
- [x] Keep Play-triggered growth visible on Home, then start a fresh wipe and
      full Ready sequence; accept B and touch Done even during the opening wipe,
      and consume input on the exact final evolution-completion frame.
- [x] Verify the converter and runtime both require the exact ordered
      29-sheet/464-frame v4 bank and reject legacy, reordered, extra,
      truncated, or malformed sources.
- [x] Run ASan/UBSan, focused host tests, the automated SDL smoke route, visual
      inspection, two byte-identical atlas conversions, and package checks.
- [x] Build and verify a matched v4 `.P4G`/`.P4R` deployment candidate with
      the pinned RISC-V toolchain and target-matched Waveshare integration.
- [ ] Complete named human SDL acceptance and named tablet acceptance before
      describing v4 as play-tested or hardware-tested; do not install or flash
      hardware without separate explicit authorization.

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
- [x] Page all eight bounded Signal Hunt results without losing selection when
      focused scans reorder the chosen opaque token.
- [x] Add deterministic Resonance Weave battles whose route, length, art layers,
      and timing come from the signal genome instead of another tap race.
- [x] Replace the production stage-skip button with a Genome panel that explains
      inherited traits and exact link/DNA requirements for the next rank.
- [x] Rate-limit growth credit with a short deterministic care cadence so rapid
      repeated taps cannot mature the dragon instantly.
- [x] Render and inspect both signal pages, Resonance Weave, and eight-signal
      Ascended/Genome states in the deterministic 177-frame preview sequence.
- [x] Combine each 8,192-part opaque-token genome with twenty bounded habitat
      contexts so coarse channel family and protected/hidden flags add orbit,
      shield, and wisp graphics without exposing more identity.
- [x] Add combinable Wideband, Prismatic, and Chimera dragon adaptations plus
      Nova/Galaxy/Eternal variety milestones at 16/24/32 distinct signals.
- [x] Show Band/Hue/Mix adaptation progress, keep a bounded 48-link recovery
      history, and guarantee Eternal at the cap without accelerating its normal
      32-link route.
- [x] Preserve valid scan results across transient request rejection, show an
      honest bounded scanner-busy/retry state, and gate touch, controller, and
      automatic requests during backoff.
- [x] Add controller Home care, Power/Style selection, Signal Hunt paging,
      tracking, Pulse Rush, Guard, and D-pad Resonance Weave play.
- [x] Render and inspect controller focus/cursor UI plus Ascended, Mythic, Nova,
      Galaxy, Eternal, reward, and final Genome states while retaining the
      deterministic 177-frame visual-QA budget.
- [x] Generate and inspect original 4x4 Signal City, reaction-effect, and
      lineage-regalia atlases with crop-safe scanner, tracker, battle, nest,
      status, and adaptation graphics.
- [x] Replace generic radio squares, empty rune sockets, care particles,
      shop bleed-through, and late-lineage clutter with authored sprites and
      scale-aware composable genome layers.
- [x] Require the validated 28-sheet/448-frame `.P4R` art bank at launch so
      missing or malformed art fails closed instead of showing placeholders.
- [x] Add deterministic full-resource startup-failure coverage and an
      11-screen CTest preview smoke for the no-placeholder graphics contract.
- [x] Extend the chronological visual-QA sequence to 178 frames with dedicated
      authored Pulse Rush and Resonance Weave battle captures.
- [x] Build the v3.8 RISC-V cartridge and full art sidecar twice with the pinned
      toolchain, verify byte identity, and record exact payload/package hashes.
- [x] Generate and inspect an original crop-safe 4x4 signal-attack atlas with
      chronological Arc Burst, Prism Lance, Thorn Snare, and Comet Crash rows.
- [x] Append the attack atlas at stable sheet index 28 and regenerate the
      deterministic 29-sheet/464-frame `.P4R` payload twice byte-identically.
- [x] Combine attack, passive, weakness, channel arena, rarity, hue, and
      protected/hidden traits into bounded deterministic encounter genomes;
      keep labels and raw MAC/BSSID data out of every derivation helper.
- [x] Give the dragon battle HP and each signal telegraphed enemy turns,
      cooldown-safe offense, shields/status effects, element-specific counters,
      and explicit victory, HP-defeat, timeout, and retreat outcomes.
- [x] Delay collection and every coin/growth/DNA reward until the authored
      victory phase completes; losses leave the signal intact for a rematch.
- [x] Animate intro, windup, travel, impact, guard, recoil, eased HP bars,
      victory, and defeat; correct hatch order, reclaim unused flight cycles,
      smooth scanner pulses, and ease Star Catcher reward spawns.
- [x] Prove frame-chunk-independent battle ordering: rune capture before a
      lethal impact wins, an exact tie loses, and every animation/status timer
      consumes only the time after its event.
- [x] Render and inspect the legacy 178-frame route plus the exact 905-frame
      Signal Battle chronology under ASan/UBSan; run the SDL host smoke and
      save-contract tests.
- [x] Build the v3.9 RISC-V cartridge and `.P4R` twice with the pinned compiler,
      verify byte identity and package structure, and record all final hashes.
- [x] Document a paired-file canary/rollback plan for pink Waveshare unit 1,
      followed by green unit 2 only after named human acceptance.
- [ ] Complete a named SDL play pass covering all four enemy attacks, all four
      dragon counters, Pulse Rush, Resonance Weave, one victory, one genuine
      defeat, retreat/rematch, touch/controller parity, and launcher return.
- [x] Build the v3.7 RISC-V cartridge twice, verify byte identity and package
      structure, and record the exact `.P4G` payload/package hashes.
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
      that expose it; Byte Buddy's required art sidecar remains read-only and does not
      authorize writable progress storage.
- [x] Add a second Signal Battle pattern so encounters are not only rapid tapping.
- [ ] Reconcile the public session-token contract with the device-key behavior
      before considering durable lineages or writable save integration.
- [x] Distinguish a transient busy scan request from true radio unavailability
      inside Byte Buddy without discarding a selectable valid snapshot.
- [ ] Clear stale results in the platform provider's ERROR snapshot before the
      Game API validator reads it; keep the approved Waveshare scan named-only.
- [x] Add Signal Hunt paging so all eight bounded OS results are selectable.
- [ ] Perform named tablet acceptance for touch feel, panel motion, speaker feedback,
      launcher return, and exact `.P4G`/`.P4R` hashes before calling the work hardware-tested.
- [ ] After named human SDL and tablet acceptance, preserve the v3.9 rollback
      pair and canary-install the matched v4 `.P4G`/`.P4R` pair through H2 USB
      Drive or a powered-off microSD reader with separate authorization.

## Balance targets

- First hatch remains quick enough to reveal the pet loop.
- Later stages take progressively longer, with no sudden grind wall.
- Star Catcher starts relaxed, accelerates only through successful play, eases after a
  miss or calm-crystal catch, and has a hard speed cap.
- Frame-rate changes do not change catcher or falling-item speed.
- A new player can earn an upgrade in the first session without the economy exploding.
