# GC Remote — RetroAchievements hardcore compliance (draft)

Status: prepared in the private test build (app 500, console GC Station v395). Not requested yet.
Until RetroAchievements approves the client, the server records every unlock as softcore.

## How it works

- The game runs on a **real GameCube**, launched by GC Station. There is no emulator, so there are no save states,
  no rewind, no slow motion, no fast forward and no frame advance.
- At launch, the console computes the RetroAchievements hash of the game (standard GameCube method, checked
  against rcheevos) and sends it to the phone app.
- The console then streams only the memory ranges that rcheevos asks for, over the local network
  (Broadband Adapter). The phone runs **rcheevos** (official library, MIT) with `rc_client`.
- If a frame is lost on the network, evaluation pauses until the needed memory arrives again. A gap can only
  delay or miss an unlock. It can never create one.

## Section A — RetroAchievements features

| Requirement | Status |
|---|---|
| Achievement list in the client | Yes: Plus → Succès → RetroAchievements (locked, unlocked, progress) |
| Triggers, Measured, Trigger flags | Handled by rc_client. Progress indicator and challenge indicators are shown ("En ce moment") |
| Rich Presence | Yes, always on (cannot be disabled) |
| Leaderboards | Started, failed and submitted events and the scoreboard are shown as notifications, trackers are shown on screen. They cannot be disabled |
| Offline queueing | rc_client retries pending unlocks while the app runs (disconnected and reconnected events are shown). **To do:** keep pending unlocks across app restarts |
| Save file compatibility | Memory cards are standard GameCube memory cards (real cards, or virtual card images) |

## Section B — Hardcore rules enforcement

| Rule | How it is met |
|---|---|
| Cheats disabled | GC Station can apply Action Replay or Gecko cheats at launch. Since v395, the console tells the phone whether cheats are applied to the game being launched. If they are, the session is **softcore**. If the console is older than v395 and can't say, the session is also softcore |
| Rewind, slowdown, frame advance | Not possible: real hardware |
| Loading save states | Not possible: real hardware (only in-game saves on memory cards) |
| Rich Presence and leaderboards always on | Yes |
| Resume or quick resume | Hardcore is allowed only if the app itself saw the game launch, less than 90 s before the game was loaded on RetroAchievements. A session restored afterwards (app opened mid-game) is **softcore** |
| Softcore to hardcore mid-session | Not allowed. The hardcore switch takes effect at the **next game launch**, so the game always starts from a reset. Hardcore to softcore works at any time |
| Memory editors, debuggers, scripting, TAS | While a hardcore session runs, the app refuses: writing a value, freezing a value, pausing the game, memory snapshots and the memory search tool ("Chercheur de codes"), screen capture that pauses the game, and the automatic Tingle Tuner cursor (memory write and automated input on the emulated GBA). These come back when the session ends or when hardcore is turned off |

On-screen notification (console GC Station v397, app 509): when an achievement unlocks, the app writes its title into
a small overlay buffer that GC Station places **below** Pulse, outside the game's memory (the game's arena ends below
it). The overlay only draws a banner into the displayed frame; it never reads or changes the game's state. This write
is allowed during a hardcore session because it does not touch game memory.

Read-only features stay available: the companion (health and items read from memory) and the 3D view
(positions read from memory). They do not change the game.

The phone also emulates the GBA for link-cable features (Tingle Tuner). For the Wi-Fi link only, it keeps three
link timeout counters from expiring on the GBA side. These are connection fixes. They give no gameplay advantage,
and the player still uses the Tingle Tuner manually.

## Section C — Client identity

User agent: `GCRemote/1.<build>.0 (Android <version>) rcheevos/<version>`. It is unique, stable and numeric.
It has never used another client's user agent.

## Section D — Timeline

The public app (GC Remote Lite) was first published in November 2026. The 6-month period starts then.

## Section E — Defaults and UX

- Softcore by default. Hardcore is one switch away: Plus → Succès → RetroAchievements → "Mode hardcore".
- The mode is shown at the start of every game, in a notification ("Mode hardcore" or "Mode normal" with the reason).
  It is also shown on the RetroAchievements screen.

## Section F — Transparency

- Free, no monetization.
- Licenses: CREDITS.md (rcheevos MIT, mGBA MPL 2.0, and so on).
- Privacy: the RetroAchievements screen lists what is sent to retroachievements.org: user name, login token, game
  hash and unlocks. Nothing else, and the password is not stored. **To do:** a full privacy policy page (data
  retention, server locations), required before applying.
