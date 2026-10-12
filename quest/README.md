# Spidy City Lab for Quest

The swinging lab, running **natively on a Meta Quest 3** (also Quest 2, Quest
Pro, Quest 3S), in a procedural, photo-textured Manhattan-like city. No PC is
needed to play: build the APK once on a Mac (or Linux, or Windows), install it
over USB or Wi-Fi, and launch it from the headset's library.

This is the lab, not Spider-Man Remastered: the game is a Windows program and
the Windows mod (the rest of this repository) needs it. What comes across is
Spidy's own swinging, web grab, props and thugs, and the overlay hands and webs.

## The city

- **About 1,200 buildings** on a 9 x 14 grid of blocks with avenues and cross
  streets, on an island with an esplanade, a railing and a river round it.
- **Building types:** brick row houses with cornices and shop fronts,
  warehouses, prewar apartments with setbacks and wooden water towers, art deco
  towers stepping back to crowns and spires, office slabs with mechanical floors,
  and glass towers with masts, taller towards downtown (around the start) and a
  second, midtown cluster to the north.
- **Photo materials:** 20 CC0 scans from [ambientCG](https://ambientcg.com):
  facades with their windows, bricks, limestone, concrete, asphalt, paving,
  gravel and tar roofs, corrugated steel and wood ([ASSETS.md](ASSETS.md)),
  weathered in the shader: rain streaks down the walls, dirt at their feet,
  stains on the roofs. Plain brick
  and stone walls get windows drawn per floor and bay: frames, sills, rooms
  behind the glass, some lit, some with blinds. Ground floors get shop windows
  with stock on shelves and sign bands, or roll-down gates.
- **Day, sunset and night** (X switches), each lit by a real sky: CC0
  [Poly Haven](https://polyhaven.com) HDRIs. The sun's (or the moon's)
  direction and colour come from it, the sky's light from its blurred levels,
  a shadow map follows you, and haze thickens near the streets. At sunset the
  light turns low and golden; at night the street lamps throw pools of light,
  shop windows and signs glow, and offices and flats light up floor by floor.
- **Reflections of the city:** a cube map of the city round you, redrawn a
  face a frame, is what glass towers, windows, wet-looking pavement and the
  river reflect: the buildings across the street, the skyline, the lights.
- **Streets:** sidewalks and curbs, lamps, traffic lights, double yellow lines,
  lane dashes, crosswalks and stop lines.

The same seed always builds the same city. Walls, roofs, parapets, water tanks, rooftop
machinery and masts are all solid: your webs stick to them and you land on them.

## Build it on a Mac

You need:

- **Android Studio** (it brings a JDK, the Android SDK and `adb`), or just the
  [command-line tools](https://developer.android.com/studio#command-line-tools-only)
  with a JDK 17 or 21 (`brew install openjdk@21`).
- In the SDK Manager (Android Studio: *Settings > Languages & Frameworks >
  Android SDK*, the *SDK Tools* tab, *Show Package Details*), or with
  `sdkmanager`:

  ```sh
  sdkmanager "platforms;android-34" "build-tools;34.0.0" "ndk;27.2.12479018" "cmake;3.31.4" "platform-tools"
  ```
- Python 3 (macOS has it) for the asset download.

Then, from the repository:

```sh
python3 quest/tools/fetch_assets.py   # ~75 MB of CC0 textures and three skies, checked by SHA-256
cd quest
export ANDROID_HOME="$HOME/Library/Android/sdk"   # where Android Studio puts the SDK
./gradlew assembleRelease
```

The APK is `quest/app/build/outputs/apk/release/app-release.apk` (about 75 MB),
signed with your machine's debug key, which is all side-loading needs. Opening
the `quest` folder in Android Studio works too: it finds the Gradle project, and
the green Run button installs it on a connected headset.

**No Android tools at all?** On GitHub, *Actions > Quest city lab > Run workflow*
builds the same APK; download it from the run's *Artifacts* and only install it
(you still need `adb`: `brew install android-platform-tools`).

## Install it on the Quest 3

Once: turn on **developer mode**. In the Meta Horizon app on your phone:
*Menu > Devices >* your headset *> Headset settings > Developer mode*, on. (It
asks you to create or join a free developer organization the first time.)
Restart the headset.

### Over USB

1. Connect the Quest to the Mac with a USB-C cable (data, not charge-only).
2. In the headset, accept **Allow USB debugging** (tick *Always allow from this
   computer*).
3. Check and install:

   ```sh
   adb devices                 # the Quest is listed as "device"
   adb install -r quest/app/build/outputs/apk/release/app-release.apk
   ```

### Over Wi-Fi

The Mac and the Quest on the same network:

1. Connect once over USB as above, then:

   ```sh
   adb tcpip 5555
   adb shell ip route | awk '{print $9}'   # the Quest's address, e.g. 192.168.1.42
   ```
2. Unplug the cable, then:

   ```sh
   adb connect 192.168.1.42:5555
   adb install -r quest/app/build/outputs/apk/release/app-release.apk
   ```

   The connection lasts until the headset restarts; repeat `adb tcpip 5555`
   over USB after that. (Newer Quest OS versions also offer *Wireless ADB* in
   *Settings > Developer*, which needs no cable at all.)

Android Studio installs over either connection too, once `adb devices` lists
the headset.

### Play

In the headset: **Library**, the filter at the top, **Unknown sources**,
**Spidy City Lab**. You start on a rooftop 43 m up, beside an avenue.

To watch what it logs (loading time, the eye size, any missing texture):

```sh
adb logcat -s SpidyCity
```

## Controls

| Do this | With |
|---|---|
| Shoot a web, swing on it | Squeeze a **grip**; release to let go (or the trigger: `web_button = trigger`) |
| Reel in | **Trigger**, while that hand's web holds |
| Zip | Pull the hand sharply away from the anchor |
| Grab a crate, barrel or thug | Grip aimed at it (a yellow marker shows over it); trigger reels it in, a sharp pull yanks it, let go to throw |
| Walk, run | **Left stick** |
| Jump / point launch | **A** |
| Charged leap | Hold **A** on the ground and let go: the longer you hold, the higher (up to about 11 m); with the left stick, forward too |
| Walls | Walk at one, or fly or jump into it, and you stay on it: at speed you run along it, then stop. **Left stick** walks it (pushed at the wall: up it, over the top onto the roof), **A** jumps off, a web pulls you off. Walking it or stopped, your view turns so the wall is your floor (`stand_on_walls`) |
| Flip (experimental, off by default: `flips`, `flip_speed`) | **Left stick** in the air turns you over, as fast as you tilt it; let go to come back level. Tap **A** in the air: one flip towards where the stick points |
| Turn | **Right stick**: 30-degree snap turns (`snap_turn`), or hold to turn smoothly (`smooth_turn`) |
| Back to the launch roof | **Y** (also when you fall in the river) |
| Day, sunset, night | **X** |

The coloured cube at the end of each hand's aim is where its web would stick.

### Settings

Until the lab has its own menu, its settings (the same as the game's SPIDY VR
tab) are in a text file on the headset. The app writes it with the defaults
the first time it starts:

```sh
adb pull /sdcard/Android/data/io.github.spidy.citylab/files/settings.txt
# edit it, e.g. smooth_turn = 120, web_button = trigger, flips = on
adb push settings.txt /sdcard/Android/data/io.github.spidy.citylab/files/settings.txt
```

Then restart the app. It has `snap_turn`, `smooth_turn`, `web_button`,
`flips`, `flip_speed`, `stand_on_walls`, `swing_speed`, `weight`, `air_webs`,
`physical_webs` and `haptics`; `adb logcat -s SpidyCity` shows what it read.

## Performance

It targets the Quest's default 72 Hz at the runtime's recommended eye size with
4x multisampling: about 240,000 triangles in the whole city, culled per block,
drawn a row of blocks per call, one 2048 shadow map, and one 256-pixel face of
the reflection cube a frame. It has not yet been
measured on a headset (see *Status*): if it stutters, the first knobs are in
`src/city_renderer.cpp` (`shadowSize_`, `shadowReach_`, `probeSize_`, the haze)
and the city's size in `city::Layout`; skipping `renderer.reflections()` in
`quest_main.cpp` falls back to sky-only reflections.

## Status

The APK builds, and its contents are checked (the native library, the OpenXR
loader, the textures, the VR launch categories). The city, its collision and
the swing in it are tested on the desktop (`spidy_city_tests`), and the
renderer is checked by rendering the city headless (`spidy_city_preview`).
**It has not been run on a Quest yet**: the OpenXR session, the controllers
and the frame rate on the headset are untested. Reports welcome.

## For developers

- `src/city.cpp` (`include/spidy/city.hpp`): the generator, the textured mesh,
  and `CityWorld`, the swing's collision through a grid.
- `src/city_renderer.cpp`: the OpenGL ES 3 renderer shared by the app and the
  preview.
- `src/lab_motion.cpp` (`include/spidy/lab_motion.hpp`): the lab's movement,
  the game's tracking rig (turning, flips, the web button, standing on walls)
  and charged leap on the lab's own swing; `spidy_lab_motion_tests` plays each
  in the city.
- `quest/app/src/main/cpp/`: the app: `quest_xr.cpp` (EGL, OpenXR session,
  input, swapchains), `quest_main.cpp` (the lab on the city, its settings file).
- `apps/city_preview.cpp`: renders a few views without a window (Linux, EGL):

  ```sh
  python3 quest/tools/fetch_assets.py
  cmake -S . -B build/linux -DSPIDY_BUILD_CITY_PREVIEW=ON && cmake --build build/linux
  ./build/linux/spidy_city_preview quest/app/src/main/assets /tmp/shots   # PPM pictures
  ```
