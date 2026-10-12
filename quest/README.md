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
- **Photo materials:** 17 CC0 scans from [ambientCG](https://ambientcg.com):
  facades with their windows, bricks, limestone, concrete, asphalt, paving,
  gravel roofs, corrugated steel and wood ([ASSETS.md](ASSETS.md)). Plain brick
  and stone walls get windows drawn per floor and bay: frames, sills, rooms
  behind the glass, some lit, some with blinds. Ground floors get shop windows
  with stock on shelves and sign bands, or roll-down gates.
- **Light from a real sky:** a CC0 [Poly Haven](https://polyhaven.com) HDRI.
  The sun's direction and colour come from it, the sky's light and reflections
  from its blurred levels, a shadow map follows you, and haze thickens near the
  streets. Glass reflects the sky above and the street below.
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
python3 quest/tools/fetch_assets.py   # ~55 MB of CC0 textures and the sky, checked by SHA-256
cd quest
export ANDROID_HOME="$HOME/Library/Android/sdk"   # where Android Studio puts the SDK
./gradlew assembleRelease
```

The APK is `quest/app/build/outputs/apk/release/app-release.apk` (about 55 MB),
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
| Shoot a web, swing on it | Squeeze a **grip**; release to let go |
| Reel in | **Trigger**, while that hand's web holds |
| Zip | Pull the hand sharply away from the anchor |
| Grab a crate, barrel or thug | Grip aimed at it (a yellow marker shows over it); trigger reels it in, a sharp pull yanks it, let go to throw |
| Walk, run | **Left stick** |
| Jump / point launch | **A** |
| Turn | **Right stick**: 30-degree snap turns |
| Back to the launch roof | **Y** (also when you fall in the river) |

The coloured cube at the end of each hand's aim is where its web would stick.

## Performance

It targets the Quest's default 72 Hz at the runtime's recommended eye size with
4x multisampling: about 205,000 triangles in the whole city, culled per block,
drawn a row of blocks per call, with one 2048 shadow map. It has not yet been
measured on a headset (see *Status*): if it stutters, the first knobs are in
`src/city_renderer.cpp` (`shadowSize_`, `shadowReach_`, the haze) and the city's
size in `city::Layout`.

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
- `quest/app/src/main/cpp/`: the app: `quest_xr.cpp` (EGL, OpenXR session,
  input, swapchains), `quest_main.cpp` (the lab on the city).
- `apps/city_preview.cpp`: renders a few views without a window (Linux, EGL):

  ```sh
  python3 quest/tools/fetch_assets.py
  cmake -S . -B build/linux -DSPIDY_BUILD_CITY_PREVIEW=ON && cmake --build build/linux
  ./build/linux/spidy_city_preview quest/app/src/main/assets /tmp/shots   # PPM pictures
  ```
