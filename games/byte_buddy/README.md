# Byte Buddy: Signal Dragons

Byte Buddy is an original, touch-first dragon-raising pet for P4 Game API v1.
The pet begins as a mystery egg and changes according to both the amount and
kind of care it receives. PixelLab and ImageGen source sheets supply the dragon
art; code adds eased motion, palette customization, particles, reactions,
touch UI, growth, levels, battle stats, and the coin economy.

Version 3 adds Signal City, a privacy-bounded Wi-Fi-inspired hunt, and local
fantasy signal battles. Version 3.1 raises the runtime dragon art to 64×64,
adds authored hatch and signal-genetic atlases, and blends adjacent poses at
frame time for smoother movement. Version 3.2 makes Star Catcher motion
elapsed-time based, replaces flat late-game leveling with widening bounded
requirements, slows the later growth stages, and adds a capped success-driven
pace plus original reward art. The artwork and mechanics are original and do
not use characters, names, capture devices, or interface designs from another
game.

Version 3.3 fills the remaining text-only item presentation with original care,
Power, Style, and Remix icons. The Style shop can now deterministically remix
owned components into a numbered look recipe while preserving player purchases.
Version 3.4 gives every fantasy Wi-Fi encounter a composed signal-seed graphic.
Six stable traits select its core, halo, elemental sigil, aura, mutation
palette, and rarity shine, producing 8,192 numbered designs from sixteen
reusable art layers.

Version 3.5 turns those encounter genes into a cumulative dragon lineage.
Distinct opaque signal tokens now contribute rarity-weighted core, halo,
sigil, aura, and hue votes plus channel-family, protected, and hidden encounter
traits. Five deliberately paced ranks progressively unlock inherited markings,
auroras, mature silhouettes, and dual-hue Mythic effects without adding another
atlas or exposing raw network identity.

Version 3.6 makes that system visible and more varied in play. Signal Hunt now
pages through all eight bounded OS results, and half of the 8,192 genomes choose
a new Resonance Weave battle: hold and glide through a stable rune route whose
length, target art, timing, and order come from the encounter genome. The new
Genome screen names inherited family, halo, sigil, aura, band diversity, and
special traits while showing exact Link/DNA requirements for the next rank.
The former stage-skip control is gone, and a short care-credit cadence prevents
rapid repeated taps from bypassing the intended growth curve.

Version 3.7 adds a contextual habitat layer without exposing any additional
network identity. Each 8,192-part genome now combines with one of twenty
coarse channel/protection contexts for 163,840 bounded signal forms. Those
contexts add code-drawn orbit nodes, shield brackets, and provider-supplied
phantom wisps over the existing layer atlas. Cumulative variety can now unlock
Wideband, Prismatic, and Chimera adaptations plus the post-Mythic Nova,
Galaxy, and Eternal milestones at 16, 24, and 32 distinct finds. Signal Hunt
and Genome now report Band, Hue, and Mix progress toward those traits. A
48-link bounded history leaves recovery room after the normal 32-link Eternal
milestone and guarantees the final form at the cap. Signal Hunt also preserves
valid results when the scanner is temporarily busy and is playable end to end
with normalized controller input.

Version 3.8 completes the visual language with three original sprite atlases.
Authored Signal City props now supply the rooftop nest, towers, scanner,
tracking compass, shield, phantom, pulse, guard, and resonance runes; care and
growth reactions use four-frame effect loops; and every lineage rank and
adaptation has a reusable regalia badge. Small signal rows compose only the
high-contrast core and sigil, while larger tracker and battle views add halo,
aura, habitat, and rarity layers. The validated 28-sheet art sidecar is now a
launch requirement: missing, truncated, wrong-version, or incomplete art fails
closed instead of showing a low-fidelity placeholder.

Version 3.9 turns Signal Battle into a two-sided encounter. Arc Burst, Prism
Lance, Thorn Snare, and Comet Crash each have a chronological authored charge,
travel, strike, and impact row. The opaque token combines one of those attacks
with Ward, Echo, Siphon, or Overclock; an elemental weakness; a coarse channel
arena; hue and rarity; and optional protected/hidden traits. The dragon now has
battle HP, Pulse Strike has a speed-bounded cooldown, enemy attacks have a
readable 0.65–1.2 second tell, and both battle styles can end in HP or timeout
defeat. Guard activates an element-specific response instead of merely adding
time. Victory and defeat each finish their animation before changing screens,
and only victory can award coins, growth, lineage DNA, or achievements. The
required sidecar now contains 29 sheets and 464 frames.

Version 4 makes that same bounded 29-sheet/464-frame budget cover the complete
play loop. Seven legacy slots are reclaimed for authored signal counters,
battle outcomes, enemy passives, scan states, evolution ceremonies, dragon
needs, and activity feedback. The superseded PixelLab egg-through-elemental
sheets remain preserved as provenance and style references, while the runtime
uses the more expressive ambient, care, aerobatic, mastery, transition, and
signal-genetic sheets already derived from them. The atlas converter now
requires the exact filename at every index and rejects missing, reordered,
duplicate, or extra sources.

Version 4.1 makes the completed art bank feel continuous and makes every signal
creature readable at every in-game scale. Dragon idle clips now change only at
complete loop boundaries, calm reactions stay calm, procedural particles and
orbits use eased or wrapped motion, and scene-transition art exits without a
size pop. Ward, Echo, Siphon, and Overclock keep a quiet active pose and play
their authored trigger/fade frames only when the matching battle mechanic
actually fires. The compact 21-pixel signal rows now preserve core, halo,
sigil, aura, hue, rarity, habitat, and shield/phantom accents, while Genome
draws the aggregate lineage it names rather than treating entropy as a signal
token. This pass reuses the same required 29-sheet/464-frame `BYTEBUD.P4R`; it
does not add placeholder art or increase the resource budget.

## Touch play

- Tap **Feed**, **Play**, **Clean**, or **Rest** to provide care.
- Tap the egg or dragon directly to pet it.
- **Play** opens Star Catcher. Swipe anywhere in the full-width lane to send
  the dragon sliding under falling rewards, then tap **Done** to return.
  Successful catches build a gently faster streak, misses reset it, and calm
  crystals ease the pace back. The displayed pace never exceeds the hard cap.
- With a controller or keyboard, press **Start** to open Star Catcher, hold
  **Left/Right** to steer with the same spring motion, and press **B** to return
  to care. Touch and controller steering share the same bounded speed model.
- On Home, controller **Left/Right** selects a care action, **A** uses it,
  **Up** opens Signal Hunt, **Down** opens Upgrades, and **B** opens Genome.
  In the shops, **Left/Right** switches tabs, **Up/Down** selects a component,
  **A** buys Power or cycles an owned Style, **Start** buys the selected Style,
  and **B** returns Home. Remix remains available by touch.
- Tap **Upgrades**, then use the **Power** and **Style** tabs.
  Power buys Wings, Aura, Nest, and Magnet levels. Style buys and equips Body,
  Eyes, Horns, and Trail choices independently; the right side of each style
  card buys the next color and the left side cycles owned colors.
- **Genome** opens the lineage breakdown and next-rank requirements. It uses
  only aggregate fantasy traits; no raw MAC address or password is available.
- Tap **Exit** or use the platform **Back** action to return safely to Console OS.

### Signal Hunt

Tap **Signal Hunt** to request a bounded city scan. Use **Prev/Next** to inspect
all eight bounded results. The list contains only an
OS-sanitized display name, an opaque session token, and a simple strength and
reward preview. Select an uneaten signal, walk around, and tap **Rescan Now**.
Repeated focused scans provide a deliberately approximate `COLD`, `GETTING
WARM`, `BATTLE READY`, or `VERY HOT` cue. RSSI is noisy, so this is a playful
hotter/colder activity rather than real direction or distance.

With a controller, **Up/Down** chooses a visible result, **Left/Right** pages,
**A** tracks or starts a battle, **Start** rescans or spends Aura Guard, and
**B** retreats one screen. In Pulse Rush, **A** strikes. In Resonance Weave,
the generated cursor begins on the first rune; move it with the D-pad and hold
**A** while gliding through the route. Physical touch takes priority whenever
both inputs are present.

At -65 dBm or stronger, **Battle** unlocks. Signal strength maps from -100..-30
dBm into a clamped 0..100 encounter strength. Stronger signals have more
health, hit harder, and are harder to defeat before time expires, but pay more
coins. Each opaque token deterministically chooses Pulse Rush or Resonance
Weave plus an attack, passive, weakness, and visual genome. RSSI can change
strength and payout, but never changes those identity-derived traits.

**Pulse Rush** uses Pulse Strike damage from Power, Aura upgrades, lineage, and
matching the displayed weakness. A 0.18–0.36 second Speed-derived cooldown
prevents tap spam from skipping the animation or encounter. **Resonance Weave**
asks the player to hold and glide through four to seven large runes; genome
rarity controls route length, signal strength controls hold time, and Magnet
increases the forgiving target radius. Thorn Snare temporarily slows both
Pulse recovery and Weave charge.

Every enemy attack enters a bright windup before its travel and impact frames.
**Aura Guard** spends a limited charge during that tell or its short travel
grace and invokes the current dragon element: Mystery uses Nova Parry, Fire
uses Flare Counter, Ice uses Glacier Ward, and Acid uses Jam Field. Prism Lance
always deals one point of chip damage through a perfect full block; Comet Crash
spikes every third volley. Ward absorbs offense, Echo strengthens every third
attack, Siphon heals after an unguarded hit, and Overclock shortens the attack
cycle. Coarse channel families add Steady, Heavy, Quick, Echo, or Shift arena
timing without pretending to reveal a physical bearing.

Winning plays a completion crest and then turns the encounter into a signal
seed that the dragon eats automatically. Dragon HP reaching zero, time running
out, or retreating plays a separate result and returns to the tracker with the
signal intact. Defeat never awards a seed, coins, growth, DNA, or the victory
achievement, so a rematch is meaningful rather than an automatic collection.

A seed's base genome still comes only from the opaque token. Its visible
habitat accents come from the coarse channel family and the already-sanitized
protected/hidden flags. Four channel families plus an unknown-family fallback,
crossed with protected and provider-supplied hidden state, make twenty habitat
contexts. The combined `FORM` number is bounded to 0..163,839. A channel change
may therefore change the habitat accents while preserving the encounter's base
genome; RSSI never changes either design layer.

Every signal seed is accepted once per session. Its opaque token determines
rarity, Fire/Ice/Acid affinity, mutation hue, and a core/halo/sigil/aura genome;
RSSI contributes battle health and payout, never identity. The game folds each
distinct token into an order-independent lineage, so finding the same set in a
different order yields the same dominant traits. Rarity-weighted votes choose
the inherited Arc, Prism, Thorn, or Comet family and its overlays. The resulting
palette applies only when the player has not bought a body color, preserving
coin customization choices.

The display label is never used as genetic input, and Byte Buddy never receives
a raw MAC address. The platform may derive its session token internally, but
the game sees only that opaque, resettable identifier. This keeps deterministic
encounters and duplicate protection without turning network identity into save
data or exposing it in the UI.

Lineage rank requires both quantity and variety. Diversity is the number of
different core, halo, sigil, aura, hue, and rarity alleles collected; repeated
look-alike seeds can still feed the dragon but cannot rush every rank.

| Lineage | Minimum distinct seeds | Minimum gene diversity | Inherited look |
| --- | ---: | ---: | --- |
| Spark | 1 | — | mutation palette, sigil, and Spark badge |
| Crest | 3 | 11 | dominant sigil marking and Crest regalia |
| Aurora | 5 | 15 | dominant halo/aura and Aurora regalia |
| Ascended | 8 | 19 | dominant mature signal-dragon family and crest |
| Mythic | 12 | 22 | second hue and Mythic regalia |
| Nova | 16 | 24 | Nova regalia with bounded constellation motes |
| Galaxy | 24 | 26 | Galaxy regalia and prismatic halo option |
| Eternal | 32 | 27 | authored Eternal genome crest |

The normal Eternal route needs 32 distinct links and 27 DNA. The session keeps
up to 48 distinct opaque tokens, so a dragon that is still missing one common
allele at link 32 can continue exploring. Reaching the bounded 48-link cap
guarantees Eternal even after an unusually repetitive run; it does not add a
faster battle-stat tier beyond the normal milestone.

Four coarse channel families add bounded speed and particle variety without
becoming identity. Three protected encounters add a shield trait and up to
three Guard points; a provider-supplied hidden encounter adds a phantom wisp
and one Magic point. Collecting all four channel families unlocks Wideband
orbit nodes, six hues unlock a Prismatic halo, and fourteen distinct
core/halo/sigil/aura alleles unlock a second Chimera sigil. These adaptations
combine instead of replacing one another. They are fantasy traits, not claims
about network safety.
Power gains at most four points from gene diversity, Speed at most four from
channel variety, and rank adds at most five Magic. Signal growth now tapers to
one interaction for ordinary later seeds, so exploring a dense area does not
skip the care game. The bonuses are small but functional in Signal Battle:
every two lineage ranks add one Pulse Strike damage, channel variety grants up
to 0.4 seconds of starting time, the shield trait adds one Aura Guard charge,
and lineage Guard contributes to the dragon's bounded battle HP.

Signal battles are visual fiction. Byte Buddy never connects to a network,
asks for a password, sends packets, or interferes with Wi-Fi. The SDL host uses
fictional deterministic encounters. The Waveshare Console OS build now carries
a locked, passive ESP32-C6 scan provider and exposes it only after background
initialization succeeds; otherwise the same honest offline screen remains.
That provider deliberately publishes named results only, preserving the prior
user-approved policy against `HIDDEN SIGNAL` rows. The generic Game API and SDL
simulator still support the hidden flag for other providers and deterministic
fantasy coverage.
The focused v3.8 host sanitizer, deterministic preview, registry, SDL host,
RISC-V cartridge, and sidecar checks pass. This game revision has not been
flashed or accepted as a complete Waveshare firmware image; live C6 behavior
remains qualified only by its separately recorded provider evidence.

The four aspect upgrades have three levels. The next levels cost 2, 5, and 8
coins:

| Aspect | Effect |
| --- | --- |
| Wings | Stronger wing trails and high-level sparkle accents |
| Aura | More elemental particles, then a glow border and crown spark |
| Nest | A richer nest plus stronger care and pet stat recovery |
| Magnet | A wider catch region in Star Catcher |

New sessions receive four coins so the first two upgrades can be previewed.
Additional coins come from catching stars.
Signal battles are the second coin source; stronger and rarer encounters pay
more.
Any two purchased upgrade levels unlock the mature rare-morph art.

Style choices are runtime palette channels, so one motion frame supports many
combinations without duplicating art. Current choices are eight body palettes,
six eye colors, five horn colors, and five colored/animated trail families.
Purchased choices remain independently equipped for the session.
The inherited two-family wing trait and eight mutation hues extend that to
19,200 bounded look recipes. **Remix** recombines only unlocked choices, always
changes a component when that category has alternatives, and shows the stable
mixed-radix `LOOK` number for the resulting recipe.

Signals use a separate deterministic genome derived only from their opaque
session token. Four cores, four halos, four elemental sigils, four auras,
eight mutation palettes, and four rarity ranks make 8,192 bounded signal-seed
genomes. Twenty coarse habitat contexts extend those genomes to 163,840
bounded `FORM` designs. The same base genome follows an encounter from the scan
list into tracking and battle; channel family plus protected/hidden state
selects its context, while changing RSSI affects challenge and reward but never
either recipe.
Fire, Ice, and Acid crests match battle affinity; the mystery-star crest keeps
its affinity concealed until play reveals it. Byte Buddy never receives a raw
SSID or MAC/BSSID: the OS supplies only a sanitized display label and an opaque,
session-scoped token that already represents the encounter identity. Labels are
shown transiently and are never used as lineage input.

## Growth and inherited traits

Every successful care action, pet, and caught star counts as interaction. The
natural stages are:

| Interactions | Stage |
| ---: | --- |
| 0–7 | Egg |
| 8–27 | Baby |
| 28–59 | Winged |
| 60–103 | Flying |
| 104+ | Elemental |

Dragon levels use widening requirements: level 2 costs 8 interactions, then
each next level costs two more until the per-level requirement caps at 24.
Level remains capped at 99. This keeps the early feedback quick while stopping
late stats from racing ahead. Power, Speed, Guard, and Magic are deterministic
battle-ready stats derived from that level, care mix, and the four Power
upgrades.

Care style determines the mature branch rather than a menu choice:

- Feed-heavy care favors **Fire**.
- Clean and Rest favor **Ice**.
- Play favors **Acid**.
- Play and Feed favor **Spiked** wings; Clean, Rest, and petting favor
  **Shiny** wings.
- Petting favors a **Nebula** morph, Feed favors **Sungold**, Play favors
  **Jade**, and Clean/Rest favor **Glacier**. These personalities lightly tint
  early stages and choose a dedicated PixelLab adult family when rare art is
  unlocked.

Animation and UI use 464 authored 64×64 source frames. Seven full sheets cover
signal counters, outcomes, passives, scan states, evolution, needs, and
activity feedback; the remaining sheets cover ambient/care motion, hatch and
signal-genetic transitions, Star Catcher rewards, items, composable signal
layers, Signal City props, reactions, lineage regalia, and signal attacks. The
renderer selects one complete crop-safe authored pose at each bounded cadence
and applies customization to that whole pose. It never blends non-overlapping
silhouettes, avoiding the ghosted double images caused by per-pixel edge
dithering while preserving crisp authored poses.
A 32-step eased hover/bounce curve, smooth drag following,
spring-and-inertia catcher motion, speed trails, star trails, a responsive
shadow, and upgrade-sensitive particles provide secondary motion. Baby idle
changes with joy and energy; winged and flying clips retain Spiked/Shiny
genetics; mature clips retain Mystery/Fire/Ice/Acid branches. All timing is
bounded and deterministic.
Signal City combines a layered procedural skyline with authored antennae,
rooftop plants, nest, scanner, tracking, shield, phantom, pulse, guard, and
resonance art. Reusable genome layers, restrained mutation motes, four-frame
impacts, and timed battle meters add motion without duplicating dragon frames.

## Signal City concept art

Four full-resolution ImageGen concepts under `assets/concepts/` establish the
home, hunt, battle, and environment hierarchy. They were generated on
2026-08-17/18 with the existing Byte Buddy baby art as continuity reference
and explicit exclusions for franchise characters, logos, capture devices,
commercial UI layouts, real network identifiers, and hacking imagery. The
runtime translates their hierarchy into clipped Game API primitives; the
large concept PNGs are source references and are not embedded in `.P4G` or
`.P4R`.

| Concept | SHA-256 | Prompt focus |
| --- | --- | --- |
| `byte_buddy_signal_city_home_concept_imagegen_v1.png` | `28cf3225a64dd08b6e99c30abdc0ee0c641da5fe4366a7523710c6ca502949ed` | Dominant dragon on a neon rooftop nest, thin care status, four care actions, and one primary Signal Hunt action |
| `byte_buddy_signal_hunt_ui_concept_imagegen_v1.png` | `6b62765090848d36004fbf4923bbb0ce75832cd1dc23962d037e7f1e29bca4a2` | Five fictional sanitized signals, elemental seed orbs, strength, rarity, reward, scan and selection actions |
| `byte_buddy_signal_battle_ui_concept_imagegen_v1.png` | `0c41e962723853ffcb734b6b39236ed69b5ee97fc65291f9cc655d7b174be743` | One abstract signal boss, RSSI-scaled health, dragon pulse impact, reward, Pulse Strike and Aura Guard |
| `byte_buddy_signal_city_background_imagegen_v1.png` | `92c53cedc599208928c3b748c9507a196c718b426afee43f57321ae478047fbb` | Character-free indigo Signal City skyline, antenna ripples, rooftop plants, and a central gold nest platform |

All prompts requested a 320×200, 16-bit-inspired practical game composition,
large touch targets, minimal chrome, dark indigo/cyan/gold/emerald color
language, and no arrows or controller glyphs. ImageGen returned larger master
images, which are preserved rather than destructively resized.

Byte Buddy remains on the stable 320×200 Game API v1 surface. On Waveshare
4.3, Console OS maps that surface to its exact-aspect 768×480 viewport. A
Byte-Buddy-only native framebuffer would require a new shared render/input
contract. Version 3.1 and later instead preserve more art detail with 64×64 sprites,
while Console OS keeps the compatible stable surface and touch mapping.

## Generated source art

The thirty-seven intentional PNGs at the top level of `assets/` are original
project inputs generated in PixelLab or ImageGen for this game. Six PixelLab
gameplay sheets are transparent 256×256 4-by-4 grids, and the retained 64×64
style anchor keeps later ImageGen sheets cohesive. Thirty 1254×1254 ImageGen
grids add two egg
families, baby reactions, flight cycles, elemental breath, nine motion
expansions, hatch and signal-genetic transitions, Star Catcher rewards, items,
signal genome layers, city props, reaction effects, lineage regalia, attacks,
counters, outcomes, passives, scan states, evolution, needs, and activity
feedback. Only the exact 29-sheet map below enters the runtime. The five
superseded PixelLab stage sheets, the PixelLab style anchor, and the two early
ImageGen egg-family sheets remain provenance/style inputs and are deliberately
excluded from the v4 bank. Some
ImageGen grids have a baked neutral checkerboard; the deterministic converter
removes border-connected neutral pixels and leaves source files untouched. For
the composable halo/aura sheet, it also removes only enclosed neutral components
large enough to be background, preserving small white highlights and sigils.

| File | SHA-256 |
| --- | --- |
| `byte_buddy_dragon_eggs_pixellab_v1.png` | `84423e7d42f7512014c116e2578a267dcd2c96d8363c55a54ff8c0ec23605aae` |
| `byte_buddy_dragon_baby_pixellab_v1.png` | `7161fb81d04d34692ba27acf5cddd1b4b28e4316a3b088234df7c3f4ce051dc3` |
| `byte_buddy_dragon_winged_pixellab_v1.png` | `f96a6d14de6751371320b5af1451cfccc6a17c849a4ad9d9d6cb38e34c6d33f6` |
| `byte_buddy_dragon_flying_pixellab_v1.png` | `fbf51c6157361ed7d85c7d95d79222e07cc72348b070344387f19b9606ab8001` |
| `byte_buddy_dragon_elemental_pixellab_v1.png` | `cba2fc20a89404e6ea66f7b7226cbf67af75e376b4b7ceafc0205cc49c48f9f9` |
| `byte_buddy_dragon_rare_variants_pixellab_v1.png` | `f6b0ac432193e2b50a2846adc506bfa668bdbcf50b2cb41755b8b98d2f4996b0` |
| `byte_buddy_dragon_style_anchor_pixellab_v1.png` | `4b40767ab5c1987bdbce3e1be91cbd3acdcbb156c6a38c82475beea42b87c364` |
| `byte_buddy_dragon_egg_morphs_imagegen_v1.png` | `253ff1143306c43025b5948931b3d54367588c49d5e1429a2b222bd3a2da39a9` |
| `byte_buddy_dragon_egg_elements_imagegen_v1.png` | `de971c8f633e7be9b24947ff416145d9d95a7338feed69a4006a30699609f006` |
| `byte_buddy_dragon_baby_reactions_imagegen_v1.png` | `e177f372d00debb6121c66ec7704fbb08c8aae772247ba4488956fdcf8e06b08` |
| `byte_buddy_dragon_flight_cycles_imagegen_v1.png` | `6108423cd35284c47aee259fd32c5fe53ec2b4f07d0f5be94af7e98e665a9c72` |
| `byte_buddy_dragon_elemental_breath_imagegen_v1.png` | `94fe69ef77c2810133a69f213770e5394c6ef4c842e8429b8fe4ddfec66ca9e9` |
| `byte_buddy_dragon_egg_morph_ambient_imagegen_v2.png` | `ad1f7ecc6b5189b793fb4133238804d1f58c4153b4c5016a53674570a5fc06fd` |
| `byte_buddy_dragon_egg_element_ambient_imagegen_v2.png` | `9177a8e0ac868f0a919b52cfec8ddf5472eb06b4ef07eb4d4a9bc6192d737483` |
| `byte_buddy_dragon_baby_idle_cycles_imagegen_v2.png` | `6a5eb4f9ac3ee221f5be0698e4fbe3d05024514b65591bea223b792e82e3d951` |
| `byte_buddy_dragon_baby_care_cycles_imagegen_v2.png` | `5bbf3c8d1cbeed00ad4a48574b6cbbb973839ad78b2b7d13356bd7d714468475` |
| `byte_buddy_dragon_winged_idle_cycles_imagegen_v2.png` | `1359426fc0076f38e67bddd1b006dbfc520c49c4d059004c6ec6f17c9a0499cc` |
| `byte_buddy_dragon_winged_care_cycles_imagegen_v2.png` | `ee4415fb9b809fe937a4bd73989c492628e29b901f0f4de9de6c4ff532677c16` |
| `byte_buddy_dragon_flight_aerobatics_imagegen_v2.png` | `475f926b97edd09780fbd9feb3e2f998401dff0428a734c261132b3d7aff1d3f` |
| `byte_buddy_dragon_elemental_mastery_imagegen_v2.png` | `57c14984ae2392f31371e9fb2fe355eb2403985616be0fc6ae14a125d621a09d` |
| `byte_buddy_dragon_elemental_impacts_imagegen_v2.png` | `859c3f149a71043f2316c687cfc7bf7ae50e972175c453046ab069a580fcf611` |
| `byte_buddy_dragon_hatch_transitions_imagegen_v3.png` | `e23e20adcd51a907e3e39152183a34d78153de1ff23b351abfa9f93a430c020d` |
| `byte_buddy_dragon_signal_genetics_imagegen_v3.png` | `eddef200a2449753a08dedbfc5cf4c69fe75b8f154b141f80a031e8303bc9937` |
| `byte_buddy_star_catcher_rewards_imagegen_v4.png` | `166f98b052a89f1d30b9a59fbce317f350a1df4a1dc48be30662ec623b5bc67f` |
| `byte_buddy_items_components_imagegen_v5.png` | `e8fade6f70766057b1915d0d3b0f2143f6fe7c2ce723ee1004cd5243e8732b68` |
| `byte_buddy_signal_genome_layers_imagegen_v6.png` | `a52c1f331f1afb4cca20d5bc8d350c9f45cbb256fe05aa739cd532d903d8b503` |
| `byte_buddy_signal_city_props_imagegen_v7.png` | `67d97a21d7675c3c738832b84698e94806eaf3f391a281818a7982b394b789f3` |
| `byte_buddy_reaction_fx_imagegen_v8.png` | `b1cbd3061e1e4a19a570d37e4b9cb4abaf9a88d971dc2032292b32e280e3bf65` |
| `byte_buddy_signal_lineage_badges_imagegen_v9.png` | `7ef6e4e6b7f6153edb4c05f8515ca8ad4233d328329b22b76a90a2cc8050ebb8` |
| `byte_buddy_signal_attack_cycles_imagegen_v10.png` | `e660d48ec4d3a721c14fd1dd6dab29dbf32b0ac6a1bca41bd1eb5eb3b185e191` |
| `byte_buddy_signal_counter_fx_imagegen_v11.png` | `f209ef282a3abcd177343cef3705273d9116d12899bcea79dca57ae561dec7b1` |
| `byte_buddy_signal_outcome_fx_imagegen_v12.png` | `3adf996dde1954d807fbab2985723d03bfba0120850da4a15430e178fdb06aee` |
| `byte_buddy_signal_passive_fx_imagegen_v13.png` | `a64c7336836fc1d2920ee018d92379ce37d83ac5d6ab4a7f584306b48d211f57` |
| `byte_buddy_signal_scan_fx_imagegen_v14.png` | `98007f62ab1a72076ac7db289437836394adad90caf58496ff88696ada4fe8de` |
| `byte_buddy_evolution_fx_imagegen_v15.png` | `151b649273e7cb634b938f651a9c81b09ae59084c0f13d04c4ee5379b8f46724` |
| `byte_buddy_need_fx_imagegen_v16.png` | `800f633eb0192d35020483f013e6cbcf26deb006ddaa3735607d49917afd8500` |
| `byte_buddy_activity_fx_imagegen_v17.png` | `2e66c3f31c26528dcac2adf23f38349638dcc5f7f125459538e1e078cc280fdb` |

### Exact v4 runtime sheet map

The converter accepts exactly these 29 sources in this order. Each index owns
16 consecutive 64×64 runtime frames, so index `n` maps to frames
`n * 16` through `n * 16 + 15`.

| Index | Frames | Exact source |
| ---: | ---: | --- |
| 0 | 0–15 | `byte_buddy_signal_counter_fx_imagegen_v11.png` |
| 1 | 16–31 | `byte_buddy_signal_outcome_fx_imagegen_v12.png` |
| 2 | 32–47 | `byte_buddy_signal_passive_fx_imagegen_v13.png` |
| 3 | 48–63 | `byte_buddy_signal_scan_fx_imagegen_v14.png` |
| 4 | 64–79 | `byte_buddy_evolution_fx_imagegen_v15.png` |
| 5 | 80–95 | `byte_buddy_dragon_rare_variants_pixellab_v1.png` |
| 6 | 96–111 | `byte_buddy_need_fx_imagegen_v16.png` |
| 7 | 112–127 | `byte_buddy_activity_fx_imagegen_v17.png` |
| 8 | 128–143 | `byte_buddy_dragon_baby_reactions_imagegen_v1.png` |
| 9 | 144–159 | `byte_buddy_dragon_flight_cycles_imagegen_v1.png` |
| 10 | 160–175 | `byte_buddy_dragon_elemental_breath_imagegen_v1.png` |
| 11 | 176–191 | `byte_buddy_dragon_egg_morph_ambient_imagegen_v2.png` |
| 12 | 192–207 | `byte_buddy_dragon_egg_element_ambient_imagegen_v2.png` |
| 13 | 208–223 | `byte_buddy_dragon_baby_idle_cycles_imagegen_v2.png` |
| 14 | 224–239 | `byte_buddy_dragon_baby_care_cycles_imagegen_v2.png` |
| 15 | 240–255 | `byte_buddy_dragon_winged_idle_cycles_imagegen_v2.png` |
| 16 | 256–271 | `byte_buddy_dragon_winged_care_cycles_imagegen_v2.png` |
| 17 | 272–287 | `byte_buddy_dragon_flight_aerobatics_imagegen_v2.png` |
| 18 | 288–303 | `byte_buddy_dragon_elemental_mastery_imagegen_v2.png` |
| 19 | 304–319 | `byte_buddy_dragon_elemental_impacts_imagegen_v2.png` |
| 20 | 320–335 | `byte_buddy_dragon_hatch_transitions_imagegen_v3.png` |
| 21 | 336–351 | `byte_buddy_dragon_signal_genetics_imagegen_v3.png` |
| 22 | 352–367 | `byte_buddy_star_catcher_rewards_imagegen_v4.png` |
| 23 | 368–383 | `byte_buddy_items_components_imagegen_v5.png` |
| 24 | 384–399 | `byte_buddy_signal_genome_layers_imagegen_v6.png` |
| 25 | 400–415 | `byte_buddy_signal_city_props_imagegen_v7.png` |
| 26 | 416–431 | `byte_buddy_reaction_fx_imagegen_v8.png` |
| 27 | 432–447 | `byte_buddy_signal_lineage_badges_imagegen_v9.png` |
| 28 | 448–463 | `byte_buddy_signal_attack_cycles_imagegen_v10.png` |

The exact prompts are retained here so later sheets can match the same art
direction.

### Egg

> Byte Buddy dragon virtual-pet game sprite, stage one mystery egg: one large
> squat dragon egg with a strong cute silhouette, layered scale plates and a
> small crown-like ridge, three subtle elemental markings in ember red,
> glacier blue, and acid green, front three-quarter view. Crisp hand-crafted
> 16-bit pixel art, chunky 2-pixel midnight-navy outline, limited jewel-tone
> palette, readable at 64 pixels, centered on a shared baseline with generous
> transparent padding. Egg only, fully closed, no baby visible, no nest, no
> floor, no cast shadow, no scenery, no text, no logo, one object only.

### Baby

> Byte Buddy dragon virtual-pet sprite, newly hatched BABY stage. Generate the
> same lovable baby dragon identity in varied idle and reaction poses:
> oversized bright eyes, round head and belly, tiny horns, stubby legs, short
> tail, only tiny folded wing buds, jewel-like belly scale, friendly curious
> expression. Include calm idle, blink, happy bounce, nuzzle, sleepy and
> surprised reactions while preserving the same proportions and front
> three-quarter camera. Crisp hand-crafted 16-bit pixel art matching the
> mystery egg sheet: chunky 2-pixel midnight-navy outline, limited violet,
> teal, coral and gold jewel palette, readable at 64 pixels, each variation
> centered on the same baseline with generous transparent padding. One baby
> dragon only per frame, no full-size wings, no floor, shadow, scenery, text or
> logo.

### Winged

> Byte Buddy dragon virtual-pet sprite, WINGED juvenile stage. Preserve the
> baby dragon's purple body, orange belly jewel, bright cyan eyes, rounded
> snout and tiny horns, now older with two clearly readable medium wings while
> its feet remain on the ground. Across the variations show two hereditary
> wing families: bold jagged SPIKED wing edges and gentle smooth SHINY wings
> with cyan-gold highlights. Include idle, blink, proud stretch, playful flap,
> happy, sleepy and surprised reaction poses while keeping the same character
> proportions and front three-quarter camera. Crisp hand-crafted 16-bit pixel
> art, chunky 2-pixel midnight-navy outline, limited violet, teal, coral and
> gold jewel palette, readable at 64 pixels, centered on a shared baseline with
> transparent padding. One dragon only per frame, not airborne, no floor,
> shadow, scenery, text or logo.

### Flying

> Byte Buddy dragon virtual-pet sprite, FLYING young-dragon stage. Preserve the
> same purple dragon, orange belly jewel, cyan eyes, rounded snout and horns,
> now airborne with both feet clearly lifted, tail balancing the pose, and
> large readable wings. Across the variations show two hereditary wing
> families: bold jagged SPIKED purple wing edges and gentle smooth SHINY
> cyan-gold wings. Include hover, strong downstroke, upstroke, glide, playful
> bank, happy midair bounce and surprised reaction poses while preserving
> identity and a front three-quarter camera. Crisp hand-crafted 16-bit pixel
> art, chunky 2-pixel midnight-navy outline, limited violet, teal, coral and
> gold jewel palette, readable at 64 pixels, each airborne pose centered with
> generous transparent padding. One dragon only per frame, visibly flying, no
> ground, floor, cast shadow, scenery, text or logo.

### Elemental

> Byte Buddy dragon virtual-pet sprite, mature ELEMENTAL DRAGON stage. Create
> varied poses of the same grown but lovable dragon identity with orange belly
> jewel, bright eyes, horns, long tail and large wings. Cover all six readable
> evolution branches across the variations: FIRE with ember-red/orange scales,
> ICE with glacier-blue/white scales, and ACID with emerald/lime scales; each
> element must appear with both bold jagged SPIKED wings and gentle smooth
> SHINY wings with jewel highlights. Include proud idle, blink, hover, happy
> roar and a compact fire, frost or acid breath reaction contained inside the
> frame. Crisp hand-crafted 16-bit pixel art matching earlier stages, chunky
> 2-pixel midnight-navy outline, limited jewel palette, readable at 64 pixels,
> centered with transparent padding. One dragon only per frame, no ground,
> cast shadow, scenery, text or logo.

### Rare genetic variants

> Byte Buddy dragon virtual-pet sprite expansion, RARE GENETIC VARIANTS for
> the same lovable mature dragon identity: orange belly jewel, bright
> expressive eyes, rounded snout, horns, long tail and large wings. Generate
> sixteen clearly different but cohesive collectible morphs across four visual
> families: nebula violet with star-speckled scales and crescent horns;
> sun-gold crystal with amber belly and faceted shiny wings; moss-and-jade with
> leaflike spiked wings and acid-lime markings; glacier pearl with cyan eyes
> and frosted translucent wings. Mix proud idle, hover, happy roar, playful
> bank, blink and compact magic-breath reaction poses while preserving the same
> front three-quarter camera and proportions. Crisp hand-crafted 16-bit pixel
> art matching Byte Buddy, chunky 2-pixel midnight-navy outline, limited
> jewel-tone palette, readable at 64 pixels, every variation centered with
> generous transparent padding. One dragon only per frame, no egg, no baby, no
> ground, cast shadow, scenery, text or logo.

### ImageGen motion expansion

The nine `imagegen_v2` sheets were generated with OpenAI ImageGen on
2026-08-17 as original project art under the user's explicit authorization.
They are 4-by-4, sixteen-frame source atlases and use the existing Byte Buddy
art as identity/style reference. A 5-by-4 winged draft and an elemental draft
with missing dragons were rejected; neither is a project asset. The accepted
prompts combined the exact row requests below with these shared constraints:

> Use case: stylized-concept. Asset type: game sprite animation sheet. Create
> exactly four columns by four rows: sixteen independently crop-safe equal
> cells. Preserve the referenced Byte Buddy identity, proportions, front
> three-quarter camera, hard pixel edges, chunky two-pixel midnight-navy
> outline, and limited jewel palette, readable at 48 by 48 pixels. Center one
> complete character or egg in every cell on a shared baseline with generous
> transparent padding. No checkerboard, grid lines, borders, text, labels,
> numbers, watermark, floor, cast shadow, scenery, merged cells, or overlap.

| Source | Exact per-row animation request |
| --- | --- |
| `byte_buddy_dragon_egg_morph_ambient_imagegen_v2.png` | Columns: Nebula, Sungold, Jade, Glacier. Rows: lean left; centered breathing glow; lean right; tiny happy hop with brighter markings. Eggs stay fully closed. |
| `byte_buddy_dragon_egg_element_ambient_imagegen_v2.png` | Columns: Mystery, Fire, Ice, Acid. Rows: lean left; centered elemental pulse; lean right; tiny hop with compact glow. Eggs stay fully closed. |
| `byte_buddy_dragon_baby_idle_cycles_imagegen_v2.png` | Row 1 breathing; row 2 blink/look-around; row 3 tail-wag and in-place scamper; row 4 yawn and head-nod. Four coherent left-to-right frames per loop. |
| `byte_buddy_dragon_baby_care_cycles_imagegen_v2.png` | Row 1 feed follow-through: chew, swallow, belly pat, settle. Row 2 play/pet: nuzzle, bounce, tail wag, settle. Row 3 clean: shake, polish, gleam, settle. Row 4 rest: curl, eyes close, dream breath, settle. |
| `byte_buddy_dragon_winged_idle_cycles_imagegen_v2.png` | Row 1 Spiked breathing/blink; row 2 Shiny breathing/blink; row 3 Spiked stretch/flap/hop/settle; row 4 Shiny stretch/shimmer flap/hop/settle. Grounded except the tiny hop. |
| `byte_buddy_dragon_winged_care_cycles_imagegen_v2.png` | Row 1 Spiked nuzzle/bite/tail-wag/celebrate; row 2 Shiny nuzzle/bite/shimmer/celebrate; row 3 Spiked shake/polish/yawn/settle; row 4 Shiny shake/polish/yawn/settle. |
| `byte_buddy_dragon_flight_aerobatics_imagegen_v2.png` | Row 1 Spiked upstroke/forward/downstroke/glide; row 2 matching Shiny cycle; row 3 Spiked bank-left/level/bank-right/dive-recover; row 4 matching Shiny cycle. Feet stay airborne. |
| `byte_buddy_dragon_elemental_mastery_imagegen_v2.png` | Rows: Mystery, Fire, Ice, Acid. Each row: hover, blink, compact elemental charge, proud settle. Effects stay close to the dragon. |
| `byte_buddy_dragon_elemental_impacts_imagegen_v2.png` | Rows: Fire, Ice, Acid, Mystery. Each row: small lingering effect beside the visible dragon, proud recoil, happy blink, settled pose. The complete dragon remains visible in all sixteen cells. |

### ImageGen hatch and signal genetics

The two `imagegen_v3` sheets were generated with OpenAI ImageGen on
2026-08-18 from the accepted Byte Buddy egg, baby, elemental, and rare sheets.
Both final prompts required an original, exact 4-by-4 crop-safe atlas; one
complete subject per cell; stable camera, scale, baseline, identity, pixel
outline, and jewel palette; transparent padding; and no grid, text, logo,
scenery, floor, shadow, watermark, merged cells, or overlap.

| Source | Final prompt layout |
| --- | --- |
| `byte_buddy_dragon_hatch_transitions_imagegen_v3.png` | Columns preserve Nebula, Sungold, Jade, and Glacier shells. Rows progress from closed wobble/first crack, to eyes and one horn peeking, to head and forepaws emerging, to a happy baby sitting in the broken lower shell. |
| `byte_buddy_dragon_signal_genetics_imagegen_v3.png` | Columns preserve Arc circuit wings, Prism crystal wings, Thorn spiked wings, and Comet star wings. Rows progress through low hover/downstroke, rising blink/pulse, high-hover signal charge, and happy settling spark. A neutral violet/cyan/coral palette supports eight runtime mutation recolors. |

### ImageGen Star Catcher rewards

`byte_buddy_star_catcher_rewards_imagegen_v4.png` was generated with the
built-in OpenAI ImageGen tool on 2026-08-19 as original project art under the
user's explicit authorization. The existing style-anchor and flying-dragon
sheets were identity/style references only; the prompt explicitly excluded the
dragon so every cell is a reusable reward or effect. The accepted source is a
1254×1254 alpha PNG with an exact crop-safe 4-by-4 layout.

> Use case: stylized-concept. Asset type: game sprite animation sheet for Byte
> Buddy Star Catcher. Create exactly four columns by four rows: sixteen
> independently crop-safe equal square cells. Row 1 is one golden five-point
> star progressing through a calm four-frame twinkle loop. Row 2 is one cyan
> moon-shaped calm crystal progressing through a soft four-frame pulse loop.
> Row 3 is one coral heart comet progressing through a compact four-frame glide
> loop, with its tiny trail contained in each cell. Row 4 is one small
> jewel-tone catch burst progressing through a four-frame sparkle-and-settle
> loop. Use original hand-crafted 16-bit pixel art, hard pixel edges, a chunky
> two-pixel midnight-navy outline, and the limited violet, cyan, coral, gold,
> and white jewel palette. Keep one complete centered object per cell with
> consistent scale and transparent padding. No checkerboard, grid, borders,
> text, labels, numbers, logo, watermark, floor, shadow, scenery, character,
> dragon, extra objects, merged cells, overlap, or frame bleed.

### ImageGen item and component atlas

`byte_buddy_items_components_imagegen_v5.png` was generated with the built-in
OpenAI ImageGen tool on 2026-08-20 as original project art under the user's
explicit authorization. The accepted source is a 1254×1254 RGBA PNG with an
exact crop-safe 4-by-4 layout and real transparency. Rows provide care items,
Power upgrades, Style categories, and a four-frame Remix prism respectively.

> Use case: stylized-concept. Asset type: game UI item and component sprite
> atlas for the original Byte Buddy dragon virtual-pet game. Create exactly
> four columns by four rows: sixteen independently crop-safe equal square
> cells. Row 1, left to right: one golden berry-and-treat bowl icon for FEED;
> one coral bouncing star ball icon for PLAY; one cyan soap bubble with a tiny
> cleaning brush icon for CLEAN; one violet crescent moon resting on a small
> gold pillow icon for REST. Row 2, left to right: one paired dragon-wing
> feather icon for WINGS; one luminous elemental orb with a thin ring for AURA;
> one warm woven nest icon for NEST; one horseshoe magnet pulling a tiny gold
> star for MAGNET. Row 3, left to right: one compact dragon scale patch icon for
> BODY; one bright expressive dragon eye gem icon for EYES; one paired crescent
> dragon-horn crest icon for HORNS; one curling sparkling comet trail icon for
> TRAIL. Row 4: the same small faceted DNA/remix prism in a four-frame
> animation—closed dim prism, opening color split, bright recombination swirl,
> settled rainbow jewel. Original hand-crafted 16-bit pixel art, hard pixel
> edges, chunky two-pixel midnight-navy outline, and a limited violet, cyan,
> coral, gold, emerald, white, and deep-indigo jewel palette. One complete
> centered icon per cell, consistent scale, generous transparent padding, and
> no object crossing a cell boundary. Transparent background; no checkerboard,
> grid, borders, text, letters, labels, numbers, logo, watermark, floor, cast
> shadow, scenery, dragon character, extra objects, merged cells, overlap, or
> frame bleed. Every cell remains readable at 64×64 pixels.

### ImageGen signal genome layers

`byte_buddy_signal_genome_layers_imagegen_v6.png` was generated and refined
with the built-in OpenAI ImageGen tool on 2026-08-23 as original project art.
The v5 item atlas supplied exact pixel-art continuity and the Signal Hunt
concept supplied subject/color continuity. The accepted 1254×1254 source keeps
an exact 4-by-4 layout: neutral cores, empty-center halos, small elemental
sigils, and empty-center auras. Runtime code stacks one frame from each row and
applies one of eight bounded mutation palettes.

> Use case: precise-object-edit. Asset type: composable 4-by-4 Byte Buddy
> signal-genome sprite atlas. Preserve the exact grid, sixteen positions,
> pixel-art style, palette, scale, and layer geometry. In Row 1, use four
> neutral base silhouettes—luminous jewel orb, faceted crystal seed,
> triangular signal pod, and comet-heart capsule—with no elemental symbols.
> Row 2 contains only four empty-center halo overlays: radio-wave arcs,
> three-node antenna crown, satellite orbit, and angular pulse waveform. Row 3
> contains only four small centered sigils: mystery star, flame, snow crystal,
> and acid droplet. Row 4 contains only four empty-center aura overlays: star
> sparks, prism shards, round motes, and digital pulse fragments. Use crisp
> hand-crafted 16-bit pixel art, chunky midnight-navy outlines, and the Byte
> Buddy cyan, violet, coral, gold, emerald palette. One crop-safe layer per
> cell; no dragons, scenery, floor, shadows, borders, text, logos, watermark,
> or neighboring-cell overlap.

### ImageGen finished Signal City graphics

The three `imagegen_v7` through `imagegen_v9` sheets were generated with the
built-in OpenAI ImageGen tool on 2026-08-29 as original Byte Buddy art. Their
prompts carry forward the established midnight-indigo outline, cyan, violet,
coral, gold, emerald, and white jewel palette. Each accepted master is an exact
4-by-4 crop-safe atlas with one complete centered subject per cell and no text,
labels, logos, watermark, floor, cast shadow, scenery, merged cells, overlap,
or frame bleed.

`byte_buddy_signal_city_props_imagegen_v7.png`:

> Create sixteen original hand-crafted 16-bit pixel-art Signal City props in
> four columns by four rows. Row 1: cyan radio tower with contained waves,
> indigo-gold signal spire, warm lantern with rooftop plants, violet relay.
> Row 2: golden woven dragon nest platform, luminous planter, rail console,
> distant indigo city cluster. Row 3: scanner dish, tracking compass, protected
> antenna under a shield dome, friendly phantom signal wisp. Row 4: cyan pulse
> strike ring, gold aura-guard shield, violet resonance rune, prismatic
> lineage-complete crest. Hard pixel edges, chunky midnight-navy outline,
> readable at 64×64, neutral transparent-ready background, one object per cell.

`byte_buddy_reaction_fx_imagegen_v8.png`:

> Create sixteen original transparent Byte Buddy reaction-effect sprites in a
> four-by-four atlas. Row 1 is a four-frame coral heart/affection sparkle loop;
> row 2 is a four-frame cyan soap-bubble clean loop; row 3 is a four-frame
> violet crescent, star, and dream-breath rest loop; row 4 is a four-frame
> gold-cyan growth and signal-victory burst. Effects only, no dragon or item,
> compact animation progression, consistent scale and generous alpha padding.

`byte_buddy_signal_lineage_badges_imagegen_v9.png`:

> Create sixteen original Byte Buddy lineage and adaptation badges in a
> four-by-four pixel-art atlas. Row 1: Spark, Crest, Aurora, Ascended. Row 2:
> Mythic, Nova, Galaxy, Eternal. Row 3: Shield, Phantom, Wideband, Prismatic.
> Row 4: Chimera, Band, Hue, Mix. Make every badge a distinct readable jewel
> crest with no letters or numbers, hard pixel edges, chunky midnight-navy
> outline, neutral transparent-ready background, and generous crop-safe
> padding.

### ImageGen signal attack cycles

`byte_buddy_signal_attack_cycles_imagegen_v10.png` was generated with the
built-in OpenAI ImageGen tool on 2026-08-29 as original Byte Buddy art. The
accepted 1254×1254 RGBA master has real transparency and an exact four-by-four
chronological layout. Columns are always charge, travel, strike, and impact;
rows are the encounter families below.

| Row | Attack | Four-frame progression |
| ---: | --- | --- |
| 0 | Arc Burst | cyan electrical charge, forked bolt, arcing discharge, electric impact ring |
| 1 | Prism Lance | violet crystal glint, faceted charge, rainbow lance, shard impact |
| 2 | Thorn Snare | green signal seed, curling tendrils, thorn-and-acid snare, toxic burst |
| 3 | Comet Crash | gold warning star, descending comet, bright impact, star-spark crater ring |

> Use case: stylized-concept. Asset type: original pixel-art combat animation
> atlas for the Byte Buddy ESP32-P4 game. Primary request: create one exact
> 4-column by 4-row sprite atlas containing four chronological signal-enemy
> attack animations. Every row is one attack and every column is the next frame
> in time. Scene/backdrop: fully transparent background; no scenery.
> Style/medium: polished saturated 16-bit arcade pixel art with hard square
> pixel edges, dark navy outlines, bright cyan, magenta, violet, gold, electric
> green, and white highlights; cute magical cyber-fantasy matching a friendly
> signal-dragon game. Composition/framing: sixteen equal square cells in a
> precise 4x4 grid; every effect centered on the same baseline and contained
> within about 70 percent of its cell with generous transparent padding; no
> element crosses a cell boundary. Row 1, left to right: ARC BURST — tiny cyan
> electrical charge, growing forked bolt, bright arcing discharge, compact
> electric impact ring. Row 2, left to right: PRISM LANCE — small violet
> crystal glint, charged faceted crystal, narrow rainbow energy lance,
> crystalline shard impact. Row 3, left to right: THORN SNARE — small green
> signal seed, curling neon tendrils expanding, complete thorn-and-acid snare
> ring, compact toxic burst with droplets. Row 4, left to right: COMET CRASH —
> small gold warning star, descending magenta-blue comet, brilliant centered
> comet impact, fading star-spark crater ring. Constraints: exactly 4 columns
> and exactly 4 rows; chronological animation continuity within each row;
> transparent background; crop-safe isolated frames; consistent scale and
> optical center; limited palette; no characters; no buildings; no UI panels;
> no text; no letters; no numbers; no cell borders; no grid lines; no
> checkerboard baked into the image; no watermark; original design only.
> Avoid: painterly blur, antialiasing, photorealism, gradients that destroy
> pixel edges, oversized effects touching cell edges, duplicated frames,
> decorative objects unrelated to the named attack.

### ImageGen full-graphics v4 sheets

The seven `imagegen_v11` through `imagegen_v17` sheets were generated with the
built-in OpenAI ImageGen tool on 2026-08-29 as original Byte Buddy project art.
The accepted masters are 1254×1254 RGBA images and were visually inspected for
the requested exact 4-by-4 layout, chronological rows, crop safety, and real
transparent padding. The authoritative final generation specifications are
retained below. Each sheet-specific prompt included this exact shared art and
layout constraint:

> Original hand-crafted 16-bit pixel art with hard square pixel edges, a chunky
> 2-pixel midnight-navy outline, and the established cyan, violet, coral, gold,
> emerald, ice-blue, and white jewel palette. Create exactly four columns by
> four rows with one crop-safe centered subject in every cell and generous real
> transparent padding. No checkerboard, grid, cell border, text, letters,
> numbers, logo, watermark, floor, cast shadow, scenery, dragon, character,
> merged cells, overlap, or frame bleed.

`byte_buddy_signal_counter_fx_imagegen_v11.png`:

> Create a Byte Buddy signal-counter animation atlas. Across every row, columns
> are chronological: tell/charge, guard/field, counter/disruption, recovery.
> Row 1 is Nova Parry: warning star, prismatic disc, reflected energy, fading
> orbit. Row 2 is Flare Counter: ember tell, flame shield sweep, hot counter
> burst, fading sparks. Row 3 is Glacier Counter: ice glyph, crystalline shield,
> cracking counter wave, settling snow. Row 4 is Jam Counter: static warning,
> interference field, disrupted signal burst, fading digital bits.

`byte_buddy_signal_outcome_fx_imagegen_v12.png`:

> Create a Byte Buddy signal-battle outcome animation atlas. Across every row,
> columns are chronological: onset, expansion, resolution, settle. Row 1 is a
> friendly taming victory with a contained prismatic bond burst. Row 2 is HP
> defeat with a cracked energy crest collapsing safely. Row 3 is timeout with a
> clocklike signal ring expiring and dimming. Row 4 is retreat with a protective
> portal or directional energy sweep closing behind it.

`byte_buddy_signal_passive_fx_imagegen_v13.png`:

> Create a Byte Buddy signal-enemy passive animation atlas. Across every row,
> columns are chronological: activation, active state, triggered effect, fade.
> Row 1 is Ward, a compact shield field. Row 2 is Echo, a duplicated pulse and
> returning ripple. Row 3 is Siphon, an emerald-violet energy drain and recovery
> mote. Row 4 is Overclock, a gold-magenta acceleration ring with controlled
> sparks.

`byte_buddy_signal_scan_fx_imagegen_v14.png`:

> Create a Byte Buddy Signal Hunt scanner-state animation atlas with four
> chronological frames in every row. Row 1 is active scan: scanner seed, growing
> radar sweep, bright detection pulse, settling return. Row 2 is busy/retry:
> queued pulse, blocked interference, retry orbit, ready glint. Row 3 is
> offline/error: broken signal glyph, dim crossed wave, contained error pulse,
> safe idle. Row 4 is empty or moved-away: fading contact, broken trail,
> dispersing motes, empty locator ring.

`byte_buddy_evolution_fx_imagegen_v15.png`:

> Create a Byte Buddy evolution-transition effect atlas. Across every row,
> columns are chronological: onset, energy wrap, reveal, settle. Row 1 is Baby
> to Winged with a small wing-shaped aura. Row 2 is Winged to Flying with an
> uplifting flight spiral. Row 3 is Flying to Elemental with a four-color
> elemental crown. Row 4 is signal-genome absorption with a digital seed,
> helixlike wrap, prismatic integration burst, and stable lineage orbit.

`byte_buddy_need_fx_imagegen_v16.png`:

> Create a Byte Buddy dragon-needs animation atlas. Every row is a seamless
> four-frame readable loop. Row 1 is hunger using a warm food-orb and gentle
> tummy cue. Row 2 is low joy using a dim toy-star that perks back up. Row 3 is
> dirty using dust motes and a contained smudge cloud. Row 4 is sleepy using a
> crescent, small dream puffs, and settling stars. Effects and symbolic need
> objects only.

`byte_buddy_activity_fx_imagegen_v17.png`:

> Create a Byte Buddy activity and shop-feedback atlas. Row 1 is a chronological
> Star Catcher ready/go sequence. Row 2 is a four-frame miss reaction that
> appears, falls, disperses, and settles. Row 3 is a four-frame end-of-round
> summary reward burst. Row 4 contains four distinct shop states from left to
> right: unlock onset, unlocked, equipped, unavailable.

## Deterministic atlas conversion

`tools/png_to_dragon_atlas.py` scales each source sheet to 256×256 with
nearest-neighbor sampling and splits it into sixteen 64×64 frames. Every frame
gets a deterministic 15-color RGB565 palette plus transparent index zero, and
two 4-bit indices are packed per byte. A frame therefore occupies 2,080 bytes
instead of 8,192 bytes of raw RGB565.

All twenty-nine gameplay sheets contain 464 master frames in a 965,184-byte
resource payload. Console OS wraps that payload as `BYTEBUD.P4R`: a bounded,
same-ID, SHA-256-verified read-only SD sidecar. Byte Buddy 4.x requires storage
resource format v1 and the exact ordered 29-sheet bank before initializing.
The converter rejects a missing, reordered, renamed, duplicate, or extra source
before it writes either output. Runtime validation separately fails closed on
missing, null, truncated, wrong-version, or incomplete art; there is no
low-fidelity launch path or placeholder renderer. The executable remains under
the existing 512 KiB safety limit, while the art bank remains below the
platform's 8 MiB `.P4R` bound.

The converter also emits the first six v4 sheets as a 199,744-byte include used
only as an unlinked determinism regression artifact. Byte Buddy does not include
or link that file into `.P4G`; production always pairs the cartridge with the
full sidecar.

Border-connected neutral pixels are removed from baked checkerboards, with
bounded enclosed-background cleanup on the composable genome, Signal City, and
lineage-badge sheets. Source PNGs remain unchanged.

Regenerate the include from the repository root:

```sh
python3 games/byte_buddy/tools/png_to_dragon_atlas.py \
  --builtin-count 6 \
  --include-output games/byte_buddy/src/generated/byte_buddy_dragon_atlas.inc \
  --resource-output games/byte_buddy/assets/generated/byte_buddy_dragon_art.bin \
  games/byte_buddy/assets/byte_buddy_signal_counter_fx_imagegen_v11.png \
  games/byte_buddy/assets/byte_buddy_signal_outcome_fx_imagegen_v12.png \
  games/byte_buddy/assets/byte_buddy_signal_passive_fx_imagegen_v13.png \
  games/byte_buddy/assets/byte_buddy_signal_scan_fx_imagegen_v14.png \
  games/byte_buddy/assets/byte_buddy_evolution_fx_imagegen_v15.png \
  games/byte_buddy/assets/byte_buddy_dragon_rare_variants_pixellab_v1.png \
  games/byte_buddy/assets/byte_buddy_need_fx_imagegen_v16.png \
  games/byte_buddy/assets/byte_buddy_activity_fx_imagegen_v17.png \
  games/byte_buddy/assets/byte_buddy_dragon_baby_reactions_imagegen_v1.png \
  games/byte_buddy/assets/byte_buddy_dragon_flight_cycles_imagegen_v1.png \
  games/byte_buddy/assets/byte_buddy_dragon_elemental_breath_imagegen_v1.png \
  games/byte_buddy/assets/byte_buddy_dragon_egg_morph_ambient_imagegen_v2.png \
  games/byte_buddy/assets/byte_buddy_dragon_egg_element_ambient_imagegen_v2.png \
  games/byte_buddy/assets/byte_buddy_dragon_baby_idle_cycles_imagegen_v2.png \
  games/byte_buddy/assets/byte_buddy_dragon_baby_care_cycles_imagegen_v2.png \
  games/byte_buddy/assets/byte_buddy_dragon_winged_idle_cycles_imagegen_v2.png \
  games/byte_buddy/assets/byte_buddy_dragon_winged_care_cycles_imagegen_v2.png \
  games/byte_buddy/assets/byte_buddy_dragon_flight_aerobatics_imagegen_v2.png \
  games/byte_buddy/assets/byte_buddy_dragon_elemental_mastery_imagegen_v2.png \
  games/byte_buddy/assets/byte_buddy_dragon_elemental_impacts_imagegen_v2.png \
  games/byte_buddy/assets/byte_buddy_dragon_hatch_transitions_imagegen_v3.png \
  games/byte_buddy/assets/byte_buddy_dragon_signal_genetics_imagegen_v3.png \
  games/byte_buddy/assets/byte_buddy_star_catcher_rewards_imagegen_v4.png \
  games/byte_buddy/assets/byte_buddy_items_components_imagegen_v5.png \
  games/byte_buddy/assets/byte_buddy_signal_genome_layers_imagegen_v6.png \
  games/byte_buddy/assets/byte_buddy_signal_city_props_imagegen_v7.png \
  games/byte_buddy/assets/byte_buddy_reaction_fx_imagegen_v8.png \
  games/byte_buddy/assets/byte_buddy_signal_lineage_badges_imagegen_v9.png \
  games/byte_buddy/assets/byte_buddy_signal_attack_cycles_imagegen_v10.png

python3 scripts/build-game-resource.py \
  --manifest games/byte_buddy/game.json \
  --payload games/byte_buddy/assets/generated/byte_buddy_dragon_art.bin \
  --output /tmp/BYTEBUD.P4R
```

Verified v4.0.0 release identities:

- v4 unlinked six-sheet regression include:
  `87586dab97edfd44b19a05851a7479ab2f6c5576a61edcd3dc0d8c6779dfa91d`;
- v4 full 29-sheet `BBDART2` payload: 965,184 bytes,
  `a1c59644d9503284b80ce0cef4d358647c5f9e9cc084d406e323d404dfd1cb92`;
- v4 wrapped `BYTEBUD.P4R`: 965,312 bytes,
  `5d3b604daa3bf97f2c99268b4f2eedb3a4270f18558415fa7aee8e0a9a40dcd4`;
- v4 `BYTEBUD.P4G`: 188,672 bytes with a 188,416-byte ELF payload,
  payload SHA-256
  `7a6d6ff8b0a3987875936258242113ff41a4a9ea437c9da07529f81e3e14c7d3`
  and package SHA-256
  `220fdaea0d51cd2a089f55493b621d019d156f243d86061c30f8975996d14480`;
- v3.9 rollback `BYTEBUD.P4R`: 965,312 bytes,
  `034c3253e60507b94818a7bc4a6fc76c7847ee6640c03934fa6543b841bcd65e`;
- v3.9 rollback `BYTEBUD.P4G`: 186,400 bytes, package SHA-256
  `d799f064ab33d2f50d1a676cd67e51c1b56c7014e1eefd78da91e42313038a49`.

Two independent atlas conversions produced byte-identical v4 include and
resource outputs. Two independent focused RISC-V builds with
`riscv32-esp-elf-gcc (crosstool-NG esp-14.2.0_20251107) 14.2.0` produced the
byte-identical v4 cartridge recorded above. The package verifier accepted its
header, manifest fields, capability masks, ELF payload, and digest. This is
artifact verification, not on-device acceptance.

### v4.1 host candidate

Two independent focused builds with the same pinned compiler produced an
identical 189,932-byte `BYTEBUD.P4G` with a 189,676-byte ELF payload. The
payload SHA-256 is
`85a50b699e1d8fc9926b5a43f79b9ef9f6e782c62e629ad8485cb701076ec137`
and the package SHA-256 is
`5b01567d4f094f79de039a299156ed6a5d80d75e216f9065e2b03953708ff142`.
The independent package verifier accepted the v4.1 header, manifest fields,
capability masks, embedded version, ELF payload, and digest. The manifest and
registry gate also passed. Because v4.1 changes no source atlas or resource
payload, its required `BYTEBUD.P4R` remains byte-identical to v4: 965,312 bytes
with SHA-256
`5d3b604daa3bf97f2c99268b4f2eedb3a4270f18558415fa7aee8e0a9a40dcd4`.
The verified pair is under `build-host/byte-buddy-4.1.0/`. This is a host-built
artifact candidate only; it has not received a matching firmware build, named
human play acceptance, tablet acceptance, or deployment.

### v4 deployment candidate

The controller-first Waveshare Console OS 0.4.89 build passed its no-flash
verifier with H1 P4R support and the exact v4 pair above. Its application image
is 1,832,640 bytes with
SHA-256
`2f9183918d7089f4375b73083bbeed81172d695eedb8eb424a23b140022ce862`,
leaving 78 percent of the 8,323,072-byte application slot free. Deploy v4 as a
paired `BYTEBUD.P4G` and `BYTEBUD.P4R` game-only update only after named SDL
play and physical-tablet acceptance. The first canary is the recorded pink
Waveshare 4.3 unit 1,
identity SHA-256
`c9004de451366bc54158d9d1f3504892c068153610a3f31093785827f1de380d`;
promote the same verified pair to green unit 2 only after canary acceptance.
Because the sidecar changed, remain on File Transfer and use H1 to install
`BYTEBUD.P4R` first and `BYTEBUD.P4G` second, then pull both back and verify
their exact hashes before launching. Each replacement is atomic, but an
interruption between them can leave an old-executable/new-resource pair; keep
the exact v3.9 pair above for recovery. Then exercise every attack family and
dragon counter plus Pulse Rush, Resonance Weave, retreat/rematch, one win, and
one genuine defeat with no loss reward. No host build is hardware acceptance.

## Verification

```sh
cmake -S games/byte_buddy -B build-host/byte_buddy-dragons -G Ninja
cmake --build build-host/byte_buddy-dragons
ctest --test-dir build-host/byte_buddy-dragons --output-on-failure

cmake -S tools/p4-game-host -B build-host/play-byte_buddy \
  -G Ninja -DP4_GAME=byte_buddy
cmake --build build-host/play-byte_buddy
ctest --test-dir build-host/play-byte_buddy --output-on-failure
make play-game GAME=byte_buddy
```

`byte_buddy_touch_previews` accepts the generated art payload and renders
deterministic closed egg, cracking egg, Power shop, Style shop, customized baby, flying,
elemental, mini-game, signal-list, focused tracker, and battle frames for
visual QA. Its two-argument animation mode also renders 178 chronological
gameplay frames covering every expanded stage clip, the multi-step hatch, both
controller-active signal-list pages, dormant/Eternal Genome panels, a signal
reward card, and one-signal Spark, five-signal Aurora, eight-signal Ascended,
12-signal Mythic, 16-signal Nova, 24-signal Galaxy, and 32-signal Eternal clips.
Dedicated Pulse Rush and Resonance Weave frames show their authored effects,
controller cursor, and hints. The sequence remains exactly 178 frames. The tests
also run a dedicated 905-frame Signal Battle chronology covering every attack
family, all four passive combinations, telegraphs, travel, impact, guard,
player damage, HP defeat, rematch, retreat, Pulse victory, Resonance victory,
and delayed rewards. Its passive coverage captures genuine Ward absorption,
Echo amplification, Siphon healing, and Overclock windup events instead of an
ambient animation loop. A third exact 112-frame suite covers every reclaimed v4
cell and locks semantic ROI digests for counters, outcomes, passives, scanner
states, needs, signal-earned evolution, all four visible Star-ready phases,
Star miss/summary, Power growth, neutral scene wipes, per-run summaries, and
shop onset/unlocked/equipped/unavailable mappings. Every one of those 112
authored frames has one fail-closed semantic digest; missing, duplicated,
renamed, or column-swapped expectations fail the route. A review-only
`--review-authored-motion` mode emits the same frames and their hashes without
weakening the normal fail-closed CTest path. Additional non-emitted checks
require pairwise-distinct 21-pixel list-icon regions when core, halo, sigil,
aura, hue, rarity, or any of the twenty habitat contexts changes. They also
defeat 27 unique signals without returning Home, then require distinct
Winged and Flying ceremonies in order. They also require a Play-triggered
milestone to finish visibly on Home before a fresh wipe and full Ready sequence,
require both B and touch Done to work during the opening wipe, and consume an
input edge that lands on the final evolution-completion frame. The suites
cover touch hit areas,
care achievements, shop spending paths, preview progression, drag and
controller Star Catcher play, trait selection, customization costs,
all 19,200 collision-free look recipe IDs, deterministic owned-choice Remix,
levels/battle stats, paced care credit, signal profile scaling, exhaustive
eight-result paging, focused scan reordering, transient scanner-busy retention,
both battle modes, all 8,192 genome pattern assignments and rune routes, all
twenty habitat contexts and every one of the 163,840 collision-free form IDs,
controller Signal Hunt and touch-to-controller Resonance play, battle rewards,
all 163,840 attack/channel/flag encounter derivations, dragon defenses and
cooldown bounds, capture-before-impact and exact-tie ordering, loss reward
gating, immutable in-battle signal snapshots,
all five base lineage boundaries plus both sides of the Nova/Galaxy/Eternal
thresholds, order-independent aggregation, independent on/off boundaries for
all five combinable adaptations, the 48-link recovery/fail-safe path,
low-diversity stalling, capped lineage stats, bounded signal-provider
integration, guarded rendering, the required storage/resource-v1 contract,
and rejection of missing, null, malformed, truncated, wrong-version, or
incomplete full art. CTest renders the eleven primary 320×200 screens, the
complete 178-frame route, and hashes the exact 905-frame battle and 112-frame
authored routes with the committed resource as deterministic preview smokes.
The v4.1 focused CTest run passed 5/5 and the SDL host smoke/save run passed
2/2. Its deterministic cartridge/resource pair and independent package checks
also passed. The matched Waveshare build verifier result above belongs to the
v4.0 deployment candidate; v4.1 named human SDL play, target integration, and
physical-tablet acceptance remain pending.

Care, coins, upgrades, growth, and lineage are session-only until the platform
exposes a reviewed writable save service. The game owns no display, touch,
audio, radio, or storage hardware directly.
