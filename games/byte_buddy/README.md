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
- Tap **Upgrades**, then use the touch-only **Power** and **Style** tabs.
  Power buys Wings, Aura, Nest, and Magnet levels. Style buys and equips Body,
  Eyes, Horns, and Trail choices independently; the right side of each style
  card buys the next color and the left side cycles owned colors.
- **Dev Stage** is the temporary preview cheat. Each tap advances one visual
  stage; once mature, it cycles the elemental preview.
- Tap **Exit** or use the platform **Back** action to return safely to Console OS.

### Signal Hunt

Tap **Signal Hunt** to request a bounded city scan. The list contains only an
OS-sanitized display name, an opaque session token, and a simple strength and
reward preview. Select an uneaten signal, walk around, and tap **Pulse Scan**.
Repeated focused scans provide a deliberately approximate `COLD`, `GETTING
WARM`, `BATTLE READY`, or `VERY HOT` cue. RSSI is noisy, so this is a playful
hotter/colder activity rather than real direction or distance.

At -65 dBm or stronger, **Battle** unlocks. Signal strength maps from -100..-30
dBm into a clamped 0..100 encounter strength. Stronger signals have more
health and are harder to defeat before time expires, but pay more coins.
**Pulse Strike** deals damage from dragon level and Aura upgrades; **Aura
Guard** spends a limited charge to buy more time. Winning turns the encounter
into a signal seed that the dragon eats automatically.

Every signal seed is accepted once per session. Its opaque token determines
rarity, Fire/Ice/Acid affinity, mutation hue, and a core/halo/sigil/aura genome;
RSSI contributes battle health and payout, never identity. The game folds each
distinct token into an order-independent lineage, so finding the same set in a
different order yields the same dominant traits. Rarity-weighted votes choose
the inherited Arc, Prism, Thorn, or Comet family and its overlays. The resulting
palette applies only when the player has not bought a body color, preserving
coin customization choices.

Lineage rank requires both quantity and variety. Diversity is the number of
different core, halo, sigil, aura, hue, and rarity alleles collected; repeated
look-alike seeds can still feed the dragon but cannot rush every rank.

| Lineage | Minimum distinct seeds | Minimum gene diversity | Inherited look |
| --- | ---: | ---: | --- |
| Spark | 1 | — | mutation palette and orbit sparks |
| Crest | 3 | 11 | dominant sigil marking |
| Aurora | 5 | 15 | dominant halo/aura and glow frame |
| Ascended | 8 | 19 | dominant mature signal-dragon family |
| Mythic | 12 | 22 | second hue, doubled aura, and crown |

Four coarse channel families add bounded speed and particle variety without
becoming identity. Three protected encounters add a shield trait and up to
three Guard points; a provider-supplied hidden encounter adds a phantom wisp
and one Magic point. These are fantasy traits, not claims about network safety.
Power gains at most four points from gene diversity, Speed at most four from
channel variety, and rank adds at most five Magic. Signal growth now tapers to
one interaction for ordinary later seeds, so exploring a dense area does not
skip the care game. The bonuses are small but functional in Signal Battle:
every two lineage ranks add one Pulse Strike damage, channel variety grants up
to 0.4 seconds of starting time, the shield trait adds one Aura Guard charge,
and rank/phantom magic lengthens each guard by at most 0.42 seconds.

Signal battles are visual fiction. Byte Buddy never connects to a network,
asks for a password, sends packets, or interferes with Wi-Fi. The SDL host uses
fictional deterministic encounters. The Waveshare Console OS build now carries
a locked, passive ESP32-C6 scan provider and exposes it only after background
initialization succeeds; otherwise the same honest offline screen remains.
The complete firmware and cartridge build passes, but live C6 scan behavior is
not called hardware-qualified until it has retained-UART acceptance evidence.

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
recipes. The same
composed seed and `GENE` number follow an encounter from the scan list into
tracking and battle; changing RSSI affects challenge and reward, never identity.
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

Animation and UI use 400 authored 64×64 source frames: four-frame ambient clips,
eight-frame care and power reactions, a dedicated shell-to-baby hatch, four
signal-genetic flight families, four Star Catcher reward/effect loops, and
sixteen care/Power/Style/Remix item sprites plus sixteen composable signal
layers. The renderer eases palette color between adjacent
poses while switching non-overlapping silhouette pixels together at the
midpoint. This avoids the ghosted double silhouettes caused by per-pixel edge
dithering while preserving crisp authored poses.
A 32-step eased hover/bounce curve, smooth drag following,
spring-and-inertia catcher motion, speed trails, star trails, a responsive
shadow, and upgrade-sensitive particles provide secondary motion. Baby idle
changes with joy and energy; winged and flying clips retain Spiked/Shiny
genetics; mature clips retain Mystery/Fire/Ice/Acid branches. All timing is
bounded and deterministic.
Signal City adds layered skyline motion, antenna ripples, orbiting mutation
motes, expanding encounter rings, hit pulses, and timed battle meters without
duplicating the dragon frames.

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

The twenty-six PNGs in `assets/` are original project inputs generated in
PixelLab or ImageGen for this game. Six PixelLab gameplay sheets are transparent
256×256 4-by-4 grids, and the retained 64×64 style anchor keeps later ImageGen
sheets cohesive. Nineteen 1254×1254 ImageGen grids add two egg families, baby
reactions, flight cycles, elemental breath, nine motion expansions, a hatch
transition, signal-genetic motion, Star Catcher rewards, items, and signal
genome layers. Some
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

## Deterministic atlas conversion

`tools/png_to_dragon_atlas.py` scales each source sheet to 256×256 with
nearest-neighbor sampling and splits it into sixteen 64×64 frames. Every frame
gets a deterministic 15-color RGB565 palette plus transparent index zero, and
two 4-bit indices are packed per byte. A frame therefore occupies 2,080 bytes
instead of 8,192 bytes of raw RGB565.

The first six sheets form a 199,744-byte built-in `BBDART2` fallback inside the
`.P4G`. All twenty-five gameplay sheets contain 400 master frames in an
832,064-byte
resource payload. The
Console OS build wraps that payload as `BYTEBUD.P4R`: a bounded, same-ID,
SHA-256-verified read-only SD sidecar. The executable remains under the existing
512 KiB safety limit, while optional artwork can grow independently up to the
platform's 8 MiB `.P4R` bound. If the sidecar is missing, Byte Buddy uses the
six-sheet fallback; a present but malformed or mismatched sidecar fails closed.

The converter also clears rows 54–63 in each runtime flying cell. PixelLab put
faint label-like marks in that unused transparent margin; the original PNG is
preserved for provenance, while the runtime cleanup is explicit and
reproducible.

Regenerate the include from the repository root:

```sh
python3 games/byte_buddy/tools/png_to_dragon_atlas.py \
  --builtin-count 6 \
  --include-output games/byte_buddy/src/generated/byte_buddy_dragon_atlas.inc \
  --resource-output games/byte_buddy/assets/generated/byte_buddy_dragon_art.bin \
  games/byte_buddy/assets/byte_buddy_dragon_eggs_pixellab_v1.png \
  games/byte_buddy/assets/byte_buddy_dragon_baby_pixellab_v1.png \
  games/byte_buddy/assets/byte_buddy_dragon_winged_pixellab_v1.png \
  games/byte_buddy/assets/byte_buddy_dragon_flying_pixellab_v1.png \
  games/byte_buddy/assets/byte_buddy_dragon_elemental_pixellab_v1.png \
  games/byte_buddy/assets/byte_buddy_dragon_rare_variants_pixellab_v1.png \
  games/byte_buddy/assets/byte_buddy_dragon_egg_morphs_imagegen_v1.png \
  games/byte_buddy/assets/byte_buddy_dragon_egg_elements_imagegen_v1.png \
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
  games/byte_buddy/assets/byte_buddy_signal_genome_layers_imagegen_v6.png

python3 scripts/build-game-resource.py \
  --manifest games/byte_buddy/game.json \
  --payload games/byte_buddy/assets/generated/byte_buddy_dragon_art.bin \
  --output /tmp/BYTEBUD.P4R
```

Current deterministic art identities:

- built-in include: `450156a7e0b1179934f5286b24208906f6350dd1b823affb3aba0a42a122ece2`;
- full `BBDART2` payload: `a0b245a4d9583e78b26397865d3b13fea556ef413aca7b8f22567e8e09dbad57`;
- wrapped `BYTEBUD.P4R`: 832,192 bytes,
  `ae1d39daa3cf444e628b1248ff3ea7f83b57e1718a1a75ff476e65151f8650ca`.

No v3.5 `.P4G` hash is claimed by this host-only art conversion. Record it
only after the focused RISC-V cartridge build and verifier pass.

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
visual QA. Its two-argument animation mode also renders 156 chronological
gameplay frames covering every expanded stage clip, the multi-step hatch, and
one-signal `signal-lineage-spark` plus five-signal
`signal-lineage-aurora` clips. The tests cover touch hit areas,
care achievements, shop spending paths, preview progression, drag and
controller Star Catcher play, trait selection, customization costs,
all 19,200 collision-free look recipe IDs, deterministic owned-choice Remix,
levels/battle stats, signal profile scaling, focused scans, battle rewards,
all five lineage boundaries, order-independent aggregation, low-diversity
stalling, capped lineage stats, privacy-service validation, and guarded
rendering.

Care, coins, upgrades, and growth are session-only until the platform exposes a
reviewed writable save service. The game owns no display, touch, audio, or
storage hardware directly.
