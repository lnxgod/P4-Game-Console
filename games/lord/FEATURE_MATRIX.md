# LORD 1.0.0 feature-parity audit

Audited against the authorized Synchronet source at commit
`25232df05a8ba27a4dd9fcd38b4012c43660fb50`. “Adapted” means the player-facing
loop is present but redesigned for a bounded controller/touch cartridge rather
than Synchronet's terminal, files, and server process.

| Upstream area | P4 status | Notes |
|---|---|---|
| Character creation | Complete | Name, male/female choice, profession, new-player messages |
| Core player record | Complete | HP, max HP, level, weapon, armour, strength, defence, gold, bank, XP, charm, gems, children, encounters, horse, fairy/lore, amulet, high spirits, dragon deeds |
| Death Knight / Mystical / Thieving | Complete | Independent 0–40 mastery and daily uses; all three usable in battle |
| King Arthur's weapons | Complete | All 16 progression entries and trade-in value |
| Abdul's armour | Complete | All 16 progression entries and trade-in value |
| Turgon's training | Complete | Eleven masters, XP thresholds, fights, and stat rewards |
| Healer | Complete | One-point and full healing with gold cost |
| First Bank | Complete | Deposit, withdrawal, interest, spouse bonus, bounded local transfer |
| Dark Forest monsters | Complete | All 131 authorized source records imported by a hash-gated generator |
| Forest events | Complete/adapted | All 15 event families; terminal sub-prompts are resolved as bounded controller encounters |
| DarkCloak Tavern | Complete/adapted | Horse access, rest/gamble outcome, ANSI scene |
| Red Dragon | Complete | Once per day at level 12, battle, deed, stronger rebirth |
| Red Dragon Inn | Complete | Sleep, bartender, conversation, Seth, Violet, bard, blackjack, sleeper attack, announcement, room |
| Bartender | Complete/adapted | Ale, Seth/Violet/dragon gossip, drinking contest |
| Seth and Violet | Complete/adapted | Talk, flirt, kiss/private evening, gem gift, proposal, marriage/divorce |
| Player romance/family | Complete | Affection, compliments, gifts, proposal/refusal, marriage/divorce, spouse bonus, children |
| Player directory/rankings | Complete | Eight persistent local warriors, records, sayings, rankings |
| PvP/slaughter | Complete | Daily fights, normal and inn attacks, death/revival, gold/XP, records and mail |
| Mail | Complete | Inbox, sent mail, typed body, replies, read state, event mail |
| Public conversation/news | Complete | Typed tavern line, announcement, persistent bounded daily log |
| Daily rollover | Complete offline | Healing, bank interest, fights, skills, PvP, romance, IGM reset, revival, possible child |
| Aragorn's Math | Complete/adapted | Bounded wager/math resolution |
| Barak's House | Complete/adapted | Raid risk/reward, “sugar,” profession lesson |
| The Grab Bag | Complete/adapted | Trivia reward, cabin rest, invitation |
| The Graveyard | Complete/adapted | Grave risk/reward, epitaph, respects |
| Olodrin's Orphans | Complete/adapted | Adopt, catch, child count, horse trade |
| The Outhouse | Complete/adapted | Search, behind-trees reward, typed wall message |
| The Pickle Goddess | Complete/adapted | Good/bad randomized stat change |
| ANSI/RIP visuals | Complete | Generated 16-color title, framed ANSI UI, twelve clipped code-drawn scenes |
| Durable local save | Complete | Schema 3, CRC, validation, launch decode, async queue/poll/commit |
| BBS sysop administration | Not a cartridge feature | Configuration, inactivity deletion, server maintenance, and raw record editing belong to Console OS/BBS administration |
| Shared remote realm | OS adapter pending | Game has the offline model/UI; opaque remote identity, transactions, transport, and consent require `realm` |
| Real-time multiplayer duel | Optional OS extension | Classic LORD is asynchronous; future live play requires `multiplayer-session` |
| Arbitrary external IGMs | OS adapter pending | Seven pinned add-ons are built in; installed third-party packages require typed `module-handoff` |
| Upstream raw RIP/ICN execution | Intentionally not ported | Replaced with safe project art and bounded drawing; raw scripts/terminal commands are not executable |

The package is therefore complete for standalone play and persistence. The
remaining rows are OS/BBS federation or administration work, not missing
offline game screens or rules.
