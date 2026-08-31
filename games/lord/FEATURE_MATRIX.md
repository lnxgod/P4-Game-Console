# LORD 1.8.0 feature-parity audit

Audited against the authorized Synchronet source at commit
`25232df05a8ba27a4dd9fcd38b4012c43660fb50`. “Adapted” means the player-facing
loop is present but redesigned for a bounded controller/touch cartridge rather
than Synchronet's terminal, files, and server process.

| Upstream area | P4 status | Notes |
|---|---|---|
| Character creation | Complete | Name, hero/heroine style, profession, new-player messages |
| Core player record | Complete | HP, max HP, level, weapon, armour, strength, defence, ChompCoin, vault, XP, charm, gems, youth helped, friendship badges, encounters, horse, fairy/lore, amulet, high spirits, dragon deeds; every five badges grant two points of incoming-damage guard up to eight, the amulet improves triple-damage critical chance, fairy lore guarantees forest escape, and high spirits add 15% attack damage until reset |
| Death Knight / Mystical / Thieving | Complete | Independent 0–40 mastery and daily uses; Reckless Blow deals heavy damage but opens the Death Knight to a harder reply, Mystic Renewal heals while attacking, and Shadowstep adds one forest-purse bonus that is collected only on victory while evading any non-Quick reply |
| King Arthur's weapons | Complete | All 16 progression entries and trade-in value |
| Abdul's armour | Complete | All 16 progression entries and trade-in value |
| Turgon's training | Complete | Eleven masters, XP thresholds, fights, and stat rewards; 241 estimated tier-average monster victories at about 12 fights from 15 daily forest actions target roughly 21 days to level 12 |
| Healer | Complete | One-point and full healing with ChompCoin cost |
| First Bank | Complete | ChompCoin deposit, withdrawal, offline local transfer, and hub-validated two-sided 100-ChompCoin transfer; neither local sleep nor server rollover pays passive interest |
| Dark Forest monsters | Complete/adapted | All 131 authorized source records imported by a hash-gated generator; stats/rewards stay exact, every encounter has a deterministic two-frame animated CP437 portrait, and victory text plus a few dated display names are kid-safe |
| Tactical combat | Complete | Visible Strike, Power, Guard, or Quick enemy intent with player-selected Strike, Guard, class Technique, Feint, Run, or Stats; intent/action matchups change dealt and received damage |
| Forest events | Complete/adapted | All 15 event families; terminal sub-prompts are resolved as bounded controller encounters |
| DarkCloak Tavern | Complete/adapted | Horse access, ChompCoin game outcome, ANSI scene |
| Red Dragon | Complete | Once per day at level 12, battle, deed, stronger rebirth. A matching-base offline deed is accepted only from an authoritative hub head already at level 12: sync once at level 12 before finishing if the head is lower; after that accepted head, the whole encounter may be offline without a separate `seen_dragon` upload, subject to the exact one-deed canonical rebirth check |
| Red Dragon Inn | Complete | Local-only sleep, berry fizz, conversation, Seth, Violet, bard, push-your-luck Dragon Dice, friendly sparring, announcement, room; the dice player stakes 5 ChompCoin, chooses Roll/Hold/Leave against target 18, and receives 8 on a win for deliberately negative expected value. Only the first daily win grants one charm and high spirits, tracked by a schema-5 daily bit; realm-bound sleep waits for the trusted hourly hub rollover |
| Bartender | Complete/adapted | Berry fizz, Seth/Violet/dragon gossip, and a friendship riddle bounded by the daily friendship-action counter |
| Seth and Violet | Complete/adapted | Talk, jokes, bounded Dragon Dice badges, gem sharing, and a reversible 5-HP best-friend pact/parting bonus |
| Player friendship/mentoring | Complete | Offline loop plus hub-delivered encouragement/supplies, reciprocal team consent/parting, one idempotent and reversible 5-HP teamwork bonus, and shared mentoring events |
| Adventure Clubs | Complete/host-tested | Separate from two-person adventure teams: up to eight members, sixteen curated names, daily class routes, cooperative Banner Stars, 24-realm-day seasons, paged standings, cheers, and friendly club clashes. Membership and scoring are Mac-authoritative and award only club prestige/season points—never personal ChompCoin, XP, deeds, or combat stats. Cold offline play remains complete; shared club mutations wait for the hub |
| Player directory/rankings | Complete | Eight persistent offline warriors or Previous/Next pages across 100 bounded hub profiles with records, presence, trust, team state, and sayings. Before pagination, the hub orders all accepted profiles by descending dragon deeds, level, XP, and PvP wins, then ascending PvP losses and stable ties. The Hall re-sorts only the local hero plus the current up-to-eight-entry page by deeds/level/XP/wins, not all 100 at once. Backward-compatible P4RM v3 kind 22 carries an 18-byte actor/deed sidecar while leaving existing summary/stats packets unchanged |
| PvP/challenges | Complete | Unbound local-mode fights retain classic ChompCoin/XP; realm-bound fights use hub-leased, once-resolved asynchronous outcomes with server-authored ChompCoin and win/loss prestige (no XP), records, mail, and exact receipt validation |
| Mail | Complete | Inbox, sent mail, typed body, replies/read state offline, and durable exactly-once hub delivery |
| Public conversation/news | Complete | Typed tavern line, announcement, persistent local log, and bounded hub feed fan-out |
| Daily rollover | Complete offline | Healing, fights, skills, PvP, friendship, IGM reset, revival, and possible youth-mentoring event, with no bank interest; realm-bound characters receive it only from the server's hourly clock |
| Aragorn's Math | Complete/adapted | One real four-choice addition/subtraction or harder multiplication puzzle after a 5- or 20-ChompCoin wager; a correct answer pays twice the wager and grants high spirits. With durable save, the stake and daily-use bit reach `COMMITTED` before the puzzle appears and input stays blocked; queue/status failure refunds the hidden round |
| Barak's House | Complete/adapted | Raid risk/reward, “sugar,” profession lesson |
| The Grab Bag | Complete/adapted | Trivia reward, cabin rest, team challenge |
| The Graveyard | Complete/adapted | Search risk/reward, epitaph, respects |
| Olodrin's Youth Guild | Complete/adapted | Sponsor supplies, guide a lost youngster, helper-count horse reward |
| The Outhouse | Complete/adapted | Search, behind-trees reward, typed wall message |
| The Pickle Goddess | Complete/adapted | Good/bad randomized stat change |
| ANSI/RIP visuals | Complete | Generated 16-color title/reference art, shared pinned CP437 boxes/arrows/smiles/music/blocks/shading, twelve gallery scenes, and additional social/recovery/victory compositions |
| Durable local save | Complete/host-tested | Schema 5, schema-3/4 migration, CRC, event replay cursor, persisted sync base, validation, launch decode, async queue/poll/commit, and paid-minigame commit barriers. Outcomes remain hidden and input blocked until the debit commits; queue/status failure refunds without revealing. Session-only profiles have no durable anti-preview guarantee |
| BBS sysop administration | Not a cartridge feature | Configuration, inactivity deletion, server maintenance, and raw record editing belong to Console OS/BBS administration |
| Shared local Mac realm | Complete/host-tested | P4RM v3 over OS-owned USB/BLE P4MP adds safe offline-base reconciliation (including explicit parent-authorized local adoption), dirty-branch-first upload ordering, cursor-serialized durable events, private vault validation, teams, PvP leases, shared feeds, Adventure Clubs, authoritative deed-first directory ordering with the compatible kind-22 sidecar, trainer-threshold/maximum-three-level checks, and cumulative XP/stat/economy gain caps; independent 2-, 4-, and 10-client tests plus a 14-player/30-day club campaign cover conflicts, replay, restart, seasons, and economy isolation, while exact-device realm acceptance is pending |
| Public authenticated realm | OS adapter pending | Accounts, TLS, moderation, quotas, hostile-client validation, and Internet service still require an OS-owned `realm` adapter |
| Real-time multiplayer duel | Optional OS extension | Classic LORD is asynchronous; future live play requires `multiplayer-session` |
| Arbitrary external IGMs | OS adapter pending | Seven pinned add-ons are built in; installed third-party packages require typed `module-handoff` |
| Upstream raw RIP/ICN execution | Intentionally not ported | Replaced with safe project art and bounded drawing; raw scripts/terminal commands are not executable |

The package is therefore complete for standalone play and persistence. The
remaining rows are OS/BBS federation or administration work, not missing
offline game screens or rules.

Release boundary: Console OS 0.4.88 / LORD 1.6.1 is the historical accepted
host/build security baseline and was not installed on Pink or Green. Console
OS 0.4.90 / LORD 1.8.0 is the current unsealed, unflashed successor candidate;
none of the host-tested rows is exact-device acceptance.
