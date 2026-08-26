# LORD 1.3.0 feature-parity audit

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
| First Bank | Complete | ChompCoin deposit, withdrawal, interest, teamwork bonus, bounded local transfer |
| Dark Forest monsters | Complete/adapted | All 131 authorized source records imported by a hash-gated generator; stats/rewards stay exact while victory text and a few dated display names are kid-safe |
| Forest events | Complete/adapted | All 15 event families; terminal sub-prompts are resolved as bounded controller encounters |
| DarkCloak Tavern | Complete/adapted | Horse access, ChompCoin game outcome, ANSI scene |
| Red Dragon | Complete | Once per day at level 12, battle, deed, stronger rebirth |
| Red Dragon Inn | Complete | Sleep, berry fizz, conversation, Seth, Violet, bard, Dragon Dice, friendly sparring, announcement, room |
| Bartender | Complete/adapted | Berry fizz, Seth/Violet/dragon gossip, friendship riddle |
| Seth and Violet | Complete/adapted | Talk, jokes, Dragon Dice, gem sharing, best-friend pact/parting |
| Player friendship/mentoring | Complete | Trust, encouragement, shared supplies, adventure-team formation/parting, teamwork bonus, mentoring young heroes |
| Player directory/rankings | Complete | Eight persistent local warriors, records, sayings, rankings |
| PvP/challenges | Complete | Daily fights, normal and inn sparring, defeat/revival, ChompCoin/XP, records and mail |
| Mail | Complete | Inbox, sent mail, typed body, replies, read state, event mail |
| Public conversation/news | Complete | Typed tavern line, announcement, persistent bounded daily log |
| Daily rollover | Complete offline | Healing, vault interest, fights, skills, PvP, friendship, IGM reset, revival, possible youth-mentoring event |
| Aragorn's Math | Complete/adapted | Bounded ChompCoin wager/math resolution |
| Barak's House | Complete/adapted | Raid risk/reward, “sugar,” profession lesson |
| The Grab Bag | Complete/adapted | Trivia reward, cabin rest, team challenge |
| The Graveyard | Complete/adapted | Search risk/reward, epitaph, respects |
| Olodrin's Youth Guild | Complete/adapted | Sponsor supplies, guide a lost youngster, helper-count horse reward |
| The Outhouse | Complete/adapted | Search, behind-trees reward, typed wall message |
| The Pickle Goddess | Complete/adapted | Good/bad randomized stat change |
| ANSI/RIP visuals | Complete | Generated 16-color title/reference art, shared pinned CP437 boxes/arrows/smiles/music/blocks/shading, twelve gallery scenes, and additional social/recovery/victory compositions |
| Durable local save | Complete | Schema 3, CRC, validation, launch decode, async queue/poll/commit |
| BBS sysop administration | Not a cartridge feature | Configuration, inactivity deletion, server maintenance, and raw record editing belong to Console OS/BBS administration |
| Shared remote realm | Envelope complete; OS adapter pending | `LRSY` records carry opaque actor ID, nonce, revisions, CRC, and save snapshot; accounts, authoritative merge, transport, and server day require `realm` |
| Real-time multiplayer duel | Optional OS extension | Classic LORD is asynchronous; future live play requires `multiplayer-session` |
| Arbitrary external IGMs | OS adapter pending | Seven pinned add-ons are built in; installed third-party packages require typed `module-handoff` |
| Upstream raw RIP/ICN execution | Intentionally not ported | Replaced with safe project art and bounded drawing; raw scripts/terminal commands are not executable |

The package is therefore complete for standalone play and persistence. The
remaining rows are OS/BBS federation or administration work, not missing
offline game screens or rules.
