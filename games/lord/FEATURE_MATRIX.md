# LORD 1.6.1 feature-parity audit

Audited against the authorized Synchronet source at commit
`25232df05a8ba27a4dd9fcd38b4012c43660fb50`. “Adapted” means the player-facing
loop is present but redesigned for a bounded controller/touch cartridge rather
than Synchronet's terminal, files, and server process.

| Upstream area | P4 status | Notes |
|---|---|---|
| Character creation | Complete | Name, hero/heroine style, profession, new-player messages |
| Core player record | Complete | HP, max HP, level, weapon, armour, strength, defence, ChompCoin, vault, XP, charm, gems, youth helped, friendship badges, encounters, horse, fairy/lore, amulet, high spirits, dragon deeds |
| Death Knight / Mystical / Thieving | Complete | Independent 0–40 mastery and daily uses; all three usable in battle |
| King Arthur's weapons | Complete | All 16 progression entries and trade-in value |
| Abdul's armour | Complete | All 16 progression entries and trade-in value |
| Turgon's training | Complete | Eleven masters, XP thresholds, fights, and stat rewards |
| Healer | Complete | One-point and full healing with ChompCoin cost |
| First Bank | Complete | ChompCoin deposit, withdrawal, interest, teamwork bonus, offline local transfer, and hub-validated two-sided 100-ChompCoin transfer |
| Dark Forest monsters | Complete/adapted | All 131 authorized source records imported by a hash-gated generator; stats/rewards stay exact while victory text and a few dated display names are kid-safe |
| Forest events | Complete/adapted | All 15 event families; terminal sub-prompts are resolved as bounded controller encounters |
| DarkCloak Tavern | Complete/adapted | Horse access, ChompCoin game outcome, ANSI scene |
| Red Dragon | Complete | Once per day at level 12, battle, deed, stronger rebirth |
| Red Dragon Inn | Complete | Local-only sleep, berry fizz, conversation, Seth, Violet, bard, Dragon Dice, friendly sparring, announcement, room; realm-bound sleep waits for the trusted hourly hub rollover |
| Bartender | Complete/adapted | Berry fizz, Seth/Violet/dragon gossip, and a friendship riddle bounded by the daily friendship-action counter |
| Seth and Violet | Complete/adapted | Talk, jokes, bounded Dragon Dice badges, gem sharing, and a reversible 5-HP best-friend pact/parting bonus |
| Player friendship/mentoring | Complete | Offline loop plus hub-delivered encouragement/supplies, reciprocal team consent/parting, one idempotent and reversible 5-HP teamwork bonus, and shared mentoring events |
| Player directory/rankings | Complete | Eight persistent offline warriors or Previous/Next pages across 100 bounded hub profiles with records, presence, trust, team state, sayings, and rankings |
| PvP/challenges | Complete | Offline fights plus hub-leased, once-resolved asynchronous outcomes, ChompCoin/XP, records and mail |
| Mail | Complete | Inbox, sent mail, typed body, replies/read state offline, and durable exactly-once hub delivery |
| Public conversation/news | Complete | Typed tavern line, announcement, persistent local log, and bounded hub feed fan-out |
| Daily rollover | Complete offline | Healing, vault interest, fights, skills, PvP, friendship, IGM reset, revival, possible youth-mentoring event |
| Aragorn's Math | Complete/adapted | Bounded ChompCoin wager/math resolution |
| Barak's House | Complete/adapted | Raid risk/reward, “sugar,” profession lesson |
| The Grab Bag | Complete/adapted | Trivia reward, cabin rest, team challenge |
| The Graveyard | Complete/adapted | Search risk/reward, epitaph, respects |
| Olodrin's Youth Guild | Complete/adapted | Sponsor supplies, guide a lost youngster, helper-count horse reward |
| The Outhouse | Complete/adapted | Search, behind-trees reward, typed wall message |
| The Pickle Goddess | Complete/adapted | Good/bad randomized stat change |
| ANSI/RIP visuals | Complete | Generated 16-color title/reference art, shared pinned CP437 boxes/arrows/smiles/music/blocks/shading, twelve gallery scenes, and additional social/recovery/victory compositions |
| Durable local save | Complete/host-tested | Schema 5, schema-3/4 migration, CRC, event replay cursor, persisted sync base, validation, launch decode, async queue/poll/commit; the authenticated, anti-rollback Console OS 0.4.88 successor still needs exact-device hardware acceptance |
| BBS sysop administration | Not a cartridge feature | Configuration, inactivity deletion, server maintenance, and raw record editing belong to Console OS/BBS administration |
| Shared local Mac realm | Complete/host-tested | P4RM v3 over OS-owned USB/BLE P4MP adds safe offline-base reconciliation (including explicit parent-authorized local adoption), dirty-branch-first upload ordering, cursor-serialized durable events, private vault validation, teams, PvP leases, and shared feeds; independent 2-, 4-, and 10-client tests include a 12-day/60-duel chaos campaign, while exact-device realm acceptance is pending |
| Public authenticated realm | OS adapter pending | Accounts, TLS, moderation, quotas, hostile-client validation, and Internet service still require an OS-owned `realm` adapter |
| Real-time multiplayer duel | Optional OS extension | Classic LORD is asynchronous; future live play requires `multiplayer-session` |
| Arbitrary external IGMs | OS adapter pending | Seven pinned add-ons are built in; installed third-party packages require typed `module-handoff` |
| Upstream raw RIP/ICN execution | Intentionally not ported | Replaced with safe project art and bounded drawing; raw scripts/terminal commands are not executable |

The package is therefore complete for standalone play and persistence. The
remaining rows are OS/BBS federation or administration work, not missing
offline game screens or rules.
