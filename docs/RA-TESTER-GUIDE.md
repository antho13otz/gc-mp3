# GC Station × RetroAchievements — Tester guide

Updated November 12, 2026

## What this is

GC Station lets you earn RetroAchievements on a **real GameCube**, with no emulator. Your Android phone runs the official RetroAchievements library (rcheevos), and the console sends it the game's memory over your local network.

We need testers who play normal sessions on their own setup and send back the session journal. Each journal shows how many frames were checked, so RetroAchievements can see that unlocks work across different games, networks and phones.

For now, every unlock counts in **casual mode** (formerly softcore). Hardcore is still being reviewed.

## What you need

| Item | Details |
| --- | --- |
| GameCube | Any model that can run homebrew |
| SD card adapter | SD2SP2 or SD Gecko, with your games on the SD card (.iso, .gcm or .ciso) |
| Network adapter | The official Broadband Adapter, or a W5500-based adapter for modded consoles |
| Android phone | On the same local network as the console (Wi-Fi is fine) |
| RetroAchievements account | Free, at retroachievements.org |
| A game with achievements | Check the game's page on retroachievements.org; most sets are for the US version |

## Install

1. Download the latest console file (`gc-mp3player-bNNN.dol`) and app (`gc-remote-lite-NNN.apk`) from the [Releases page](https://github.com/antho13otz/gc-mp3/releases).
2. Copy the `.dol` to your SD card and start it like your other homebrew. Later updates install from the console itself (START > Update).
3. Install the `.apk` on your phone. The app then updates itself.
4. Connect the console and the phone to the same network. The app finds the console on its own.
5. On the console, open the settings and turn on **Code finder (app)**. RetroAchievements needs it: the console computes the game's hash at launch.
6. In the app, open the achievements page (Pulse > Succès > RetroAchievements) and sign in with your RetroAchievements account. You stay signed in.

The interface can be switched between French and English.

## Play a test session

1. Open the app before you start the game, and keep it open.
2. Launch the game **from the app**: open its card in the Jeux (Games) tab and choose the Pulse launch. A game started before the app was open is not tracked from the start.
3. Play normally for at least 30 minutes. Try to unlock one or two achievements; a banner appears on the TV and a notification on the phone.
4. You can keep the phone locked or the app in the background. Some phones stop background apps to save battery; if frames go missing, allow the app to run without battery limits.
5. Quit with A + Z + START, or stop the game from the app.

The progress line on the achievements page shows frames checked and frames missed during the session. A good session ends at or very close to 100 %.

## What to send back

After each session, open the achievements page, tap **Journal**, then **Copier le journal** (Copy journal), and send it by opening an issue on this GitHub project or replying to the thread where you found this guide. Add these lines above it:

- Game and region (for example: The Legend of Zelda: The Wind Waker, US)
- Network adapter (Broadband Adapter or W5500) and how the phone is connected (Wi-Fi band, same room or not)
- Phone model and Android version
- Anything odd you noticed: an achievement that did not unlock, a pause, a disconnection

A short video of an unlock (TV and phone in the same shot) is welcome but not required.

## Known limits and FAQ

**Can a lost frame give me an achievement I did not earn?** No. If a frame is lost on the network, checking pauses until the memory arrives again. A gap can only delay or miss an unlock.

**Does it change the game?** No. The console only reads the memory the achievement set needs. Nothing is written into the game.

**No achievements show up.** Check that Code finder (app) is on, that you launched the game from the app, and that the game version matches the set on retroachievements.org.

**Cheats.** Cheats are allowed in casual mode, but please say in your report if any were active.

**iPhone?** Not yet: the app is Android only.
