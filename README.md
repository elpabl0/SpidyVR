<p align="center">
  <img src="docs/images/spidy-icon.png" alt="Spidy" width="128">
</p>

<h1 align="center">Spidy</h1>

<p align="center">
  <b>VR for Marvel's Spider-Man Remastered</b><br>
  Swing on webs with your own arms, shoot from your wrists, punch with your fists.
</p>

<p align="center">
  <a href="https://ko-fi.com/ilyamezerowsky"><img src="https://img.shields.io/badge/Support_Spidy-Ko--fi-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white" alt="Support Spidy on Ko-fi"></a>
  <a href="https://discord.gg/x27ZXDpdNt"><img src="https://img.shields.io/badge/Join_us-Discord-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Join the Discord"></a>
</p>

<p align="center">
  <a href="https://github.com/IlyaMez/Spidy/releases/latest"><b>Download the latest release</b></a>
  ·
  <a href="docs/PLAYERS.md">Players' guide</a>
  ·
  <a href="docs/CHANGELOG.md">What's new</a>
</p>

Spidy is a free fan mod by **Ilya Mezerowsky** that puts Marvel's Spider-Man
Remastered in VR: native stereo at your headset's resolution, 6DoF head and
hands, and web swinging you control with your arms. Spidy is free; if you
enjoy it, a tip on [Ko-fi](https://ko-fi.com/ilyamezerowsky) keeps the updates
coming. Questions, bugs, clips: come say hi on
[Discord](https://discord.gg/x27ZXDpdNt).

<p align="center">
  <img src="docs/images/launcher.png" alt="Spidy Launcher: checks for the game, VR runtime, headset and memory, VR options, and START VR" width="820">
</p>

> **Early and in development.** Played on Quest 3 through Virtual Desktop.
> Spidy supports one Steam build of the game; a game update needs a Spidy update.

## Features

- 🕸️ **Physical web swinging.** Squeeze a grip to fire that hand's web where you
  point, swing on it, reel in with the trigger, pull sharply to zip (or swap the
  two: the trigger webs, the grip reels). Webs hold on buildings, the ground, or
  open air.
- 🎯 **Web shooter.** The trigger of a hand without a web fires the game's own
  web ball from your wrist, with aim assist on thugs. Three quick hits web a
  thug up.
- 🪢 **Grab and throw.** Web a prop or a thug, reel or yank it in, swing it on
  the web and let go to throw it. Heavy things feel heavy. Pulled thugs fly
  flailing, and get hurt when they slam into walls, the ground or each other.
- 🦸 **You are Spider-Man.** Look down and see his body; his arms and hands
  follow your controllers. A quick T-pose the first time sizes him to your
  height and arm length.
- 👊 **Real punches.** A fast fist into a thug lands the game's own melee hit,
  harder the faster you swing.
- ⚙️ **VR settings in the game's menu.** A SPIDY VR tab in the game's own
  Settings for aim markers, snap or smooth turning, vibration, swing speed,
  how heavy you swing, screen size, the HUD and more. Changes apply at once.
- 🖥️ **Menus and cutscenes on a screen.** Cutscenes in 3D: the game draws
  them once for each eye. The Touch controllers work as an Xbox controller
  there; VR resumes when play does.

## What you need

- **Marvel's Spider-Man Remastered on Steam.** The Epic Games Store version is a
  different build.
- **A PC VR headset with an OpenXR runtime.** Played on Quest 3 with
  [Virtual Desktop](https://www.vrdesktop.net/) and with SteamVR (Steam Link);
  other SteamVR headsets and Quest Link should work.
- **About 19 GB of memory Windows can give programs** (RAM plus page file).

## Install and play

1. Download the zip from [Releases](https://github.com/IlyaMez/Spidy/releases/latest)
   and extract it to a folder you can write to, such as `Documents\Spidy`.
2. Start **Spidy Launcher.exe**. If Windows says it protected your PC, choose
   *More info*, then *Run anyway*.
3. Fix anything marked red. The launcher can install the Visual C++ runtime for
   you, and *Add shortcuts* puts Spidy VR on your desktop and Start menu.
4. Connect your headset, close the game, and press **START VR**. Pick your save
   with the VR controllers; VR takes over as soon as you play.

Keep the game window in front on the desktop: the game pauses behind other
windows. If the headset shows a still picture, click the game window once.

### What the launcher does

- Finds the game in your Steam libraries and checks it is the supported build.
- Finds the OpenXR runtime your headset is connected to (or the one you choose) and checks the headset.
- Checks the Visual C++ runtime and Spidy's own files.
- Starts the game with the larger render memory VR needs, in a small window that
  saves GPU time, without frame generation or vertical sync, and puts your settings back afterwards.
- Gives each eye the game's own anti-aliasing and upscaling (*Upscaling* in its options; the game's DLSS setting does not reach the headset), and shows your left eye in the game window while you are in VR.
- Remembers your options, and saves a report of every session in `reports\`.
- Offers each new Spidy release when it starts, and updates itself in place with one click.

Spidy changes nothing in the game's folder.

## Controls

| Do this | With |
|---|---|
| Shoot a web, swing on it | Squeeze a **grip**; release to let go |
| Reel in | **Trigger**, while that hand's web is attached |
| Zip | Pull the hand sharply away from the anchor |
| Shoot a web ball | **Trigger** of a hand without a web |
| Grab a prop or thug | Grip aimed at it; trigger reels it, a sharp pull yanks it, release throws it |
| Webs on the trigger instead | Set **WEB BUTTON** to TRIGGER in VR settings (or *Web button* in the launcher): the trigger shoots and holds webs, the grip reels and shoots web balls |
| Punch | A fast fist into a thug |
| Walk and run / jump | **Left stick** / **A** (from the ground, a wall or a perch; no web zip in the air). Hold **A** on the ground to charge a leap and let go: the longer you hold, the higher; with the left stick, forward too |
| Scan (the game's R3) | Raise a hand to your temple and pull its **trigger** |
| One enemy at a time | A ring marks the thug your next punch goes to: the one you look at. Hit him and the ring locks on until he is down (**KO!**); look squarely at another for a moment to change |
| Called blows | An arrow comes into the ring from the side your fist should come from: swing a hook from that side, an uppercut from below or a blow down from above, at arm's reach or from across the room. The yellow arrow into a jagged red ring calls the finisher |
| Walls | Walk at one, or fly or jump into it, and you stay on it: at speed you run along it, then stop. **Left stick** walks it (pushed at the wall: up it), **A** jumps off, a web pulls you off. Walking it or stopped, your view turns so the wall is your floor (STAND ON WALLS in VR settings or the launcher) |
| Flip (experimental, off by default: FLIPS and FLIP SPEED in VR settings or the launcher) | **Left stick** in the air: turns you over, as fast as you tilt it; let go to come back level. Tap **A** in the air: one flip toward where the stick points. Hold **A**: you stay turned while the stick rests |
| Turn | **Right stick**: snap 30° (adjustable or off), or hold to turn smoothly with SMOOTH TURN on in VR settings |
| Dodge (the game's B) | **B**, the way the **left stick** points; not while a web carries you |
| Interact (the game's Y) | **Y**, looking at the thing from within about a metre of it; hold **Y** and you step up to what you look at until the game takes it |
| Pull a door or a grate (the game's LB + RB) | Click the **right stick**, or hold it; not while a web carries you |
| Heal (the game's D-pad down) | Push the **right stick** straight down and hold it a moment |
| Zip to a point, enter a vent (the game's LT + RT) | Push the **right stick** straight up and hold it; not while a web carries you |
| Pause (VR settings: Settings > SPIDY VR) | **Menu** button |
| Game menu (map, suits, skills) | **X** |
| Switch between VR and a flat screen | Pause, **Settings > SPIDY VR > VIEW** |

**Aim markers** show where each free hand's web would go, blue for the left
hand and orange for the right: a ring holds, a dashed ring holds in open air,
a red cross misses, a turning ring of three arcs catches a prop or thug.
AIM MARKERS in VR settings (or *Aim markers* in the launcher) hides them.

## Troubleshooting

| Problem | Fix |
|---|---|
| "This game version is not supported" | The launcher's game line names your version and the fix. An older version or a changed `Spider-Man.exe`: in Steam, right-click the game > *Properties* > *Betas* > *None*, then *Installed Files* > *Verify integrity of game files*. A newer version needs a Spidy update. |
| The headset is not found | Connect it first, check the VR runtime in the launcher, press *Check*. |
| "VR needs administrator rights" (0.2.9 and older: "[WinError 5] Access is denied", then "No loaded player within three minutes") | Steam or the game runs as administrator, and Windows keeps other programs out of such a game. Choose *Restart as administrator* when the launcher asks (older versions: right-click Spidy Launcher > *Run as administrator*), close the game, press **START VR**. If that changes nothing, allow the Spidy folder in your antivirus. |
| The picture breaks up | Close the game and start it from the launcher, which gives it the render memory VR needs. |
| Low frame rate | Set *Upscaling* in the launcher to Balanced or Performance, then lower *Render resolution* or the streaming quality in Virtual Desktop. On a laptop, plug it in and choose its highest performance mode. 90 Hz paces more evenly than 120 Hz. |
| Blurry edges | Set *Upscaling* in the launcher to High or Off, then raise *Render resolution* above 100% (try 125%); both cost frame rate and memory. |
| Web balls fly when you meant to reel | The trigger (the grip with WEB BUTTON: TRIGGER) reels only while that hand's web is attached; on a hand without a web it shoots. |

More fixes are in the [players' guide](docs/PLAYERS.md). When you report a
problem, attach the session's report from the `reports` folder, on
[Discord](https://discord.gg/x27ZXDpdNt).

## Building from source

Needs Visual Studio 2022 C++ Build Tools with CMake and Ninja, and a Windows SDK.

```powershell
.\tools\bootstrap.ps1 -Observer   # dependencies, including Dear ImGui for the launcher
.\tools\build.ps1 -Observer       # build\windows-ninja
.\build\windows-ninja\spidy_tests.exe
.\tools\package.ps1               # dist\Spidy-<version>-win64.zip
```

`Launch Spidy VR.cmd` runs VR straight from a checkout, and `Launch Spidy Lab.cmd`
opens a standalone OpenXR swinging lab that needs no game. Releases are built on
GitHub: **Actions > Release > Run workflow**.

## Quest city lab (standalone, no PC)

Spider-Man Remastered runs only on Windows, so the mod needs a Windows PC. The
**swinging lab** also runs natively on a Quest 3 (or 2, Pro, 3S), in a
procedural, photo-textured Manhattan-like city: build the APK on a Mac, Linux
or Windows (or let GitHub Actions build it), install it over USB or Wi-Fi with
`adb`, and play it from the headset's library. [quest/README.md](quest/README.md)
has the steps.

## How it works

Spidy loads its modules into the running game and renders two extra scene views
through the game's own renderer, one per eye, at the headset's resolution and
lens, then hands them to OpenXR. Swinging is Spidy's own rope physics steering
the game's mover against the game's collision world; the webs you see, the web
shooter's shots, thrown props and punches are the game's own systems, fed from
your tracked hands.

| Read more | |
|---|---|
| [Players' guide](docs/PLAYERS.md) | Setup, every control and fix (the zip's `README.txt`) |
| [Changelog](docs/CHANGELOG.md) | Every build: what changed, why, and what was measured |
| [Development notes](docs/DEVELOPMENT.md) | Launcher and release internals, tools, launch options, memory and graphics in VR, the lab |
| [Validation](docs/VALIDATION.md) | What passed locally, and what still needs a game or headset |
| [Web grab](docs/WEB-GRAB.md) · [Body](docs/BODY.md) | Design and measurements of those features |
| [Milestone 1](docs/MILESTONE-1.md) · [Reference](docs/REFERENCE.md) | Acceptance criteria, game research, sources |

## Contributing

Fixes and features are welcome as pull requests, one feature each.
[CONTRIBUTING.md](CONTRIBUTING.md) says how, and what applies when you run or
share your own build.

## Support

Spidy is free, and built with a lot of AI help whose tokens aren't. If you
enjoy it, [support it on Ko-fi](https://ko-fi.com/ilyamezerowsky) and
[join the Discord](https://discord.gg/x27ZXDpdNt).

Please share the original release zip with its credit intact, so players get
working files and know where updates come from.

Spidy is Copyright (C) 2026 Ilya Mezerowsky, and free software under the
[GNU General Public License, version 3](LICENSE).

Spidy is a free fan project, not affiliated with or endorsed by Insomniac Games,
Sony Interactive Entertainment, Marvel or Valve. Third-party licences are in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [docs/licenses](docs/licenses).
