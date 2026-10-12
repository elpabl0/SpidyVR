// Spidy City Lab on the Quest: the swinging lab (apps/xr_lab.cpp on Windows)
// running natively on the headset, in the photo-textured city (city.hpp).
// The game itself is not here: Spider-Man Remastered runs only on a PC.
#include "quest_xr.hpp"
#include "spidy/city.hpp"
#include "spidy/city_renderer.hpp"
#include "spidy/lab_menu.hpp"
#include "spidy/lab_motion.hpp"
#include "spidy/lab_props.hpp"
#include "spidy/overlay_geometry.hpp"
#include "spidy/web_visual.hpp"
#include <android/asset_manager.h>
#include <android_native_app_glue.h>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

using namespace spidy;
namespace {
struct AppState {
    bool resumed{};
};
void onCommand(android_app* app, int32_t command) {
    auto& state = *static_cast<AppState*>(app->userData);
    if (command == APP_CMD_RESUME)
        state.resumed = true;
    else if (command == APP_CMD_PAUSE)
        state.resumed = false;
}
city::ReadAsset assets(AAssetManager* manager) {
    return [manager](const std::string& path) {
        std::vector<std::uint8_t> bytes;
        if (AAsset* a = AAssetManager_open(manager, path.c_str(), AASSET_MODE_BUFFER)) {
            bytes.resize(static_cast<std::size_t>(AAsset_getLength(a)));
            if (AAsset_read(a, bytes.data(), bytes.size()) != static_cast<int>(bytes.size()))
                bytes.clear();
            AAsset_close(a);
        }
        return bytes;
    };
}
// The lab's settings, the game's SPIDY VR tab's, from settings.txt in the
// app's files on the headset (Android/data/io.github.spidy.citylab/files),
// one "name = value" a line; written with the defaults when missing. Until
// the lab has its own menu.
constexpr const char* settingsHelp =
    R"(# Spidy City Lab settings: the pause menu (the menu button) changes them;
# or edit this file, then restart the app.
# adb pull /sdcard/Android/data/io.github.spidy.citylab/files/settings.txt
# adb push settings.txt /sdcard/Android/data/io.github.spidy.citylab/files/settings.txt
)";
void saveSettings(const std::string& path, const vr_settings::Values& v) {
    const auto onOff = [](bool on) { return on ? "on" : "off"; };
    std::ofstream out(path);
    out << settingsHelp;
    out << "snap_turn = " << v.snapTurn << "      # degrees a flick of the right stick; 0: none\n";
    out << "smooth_turn = " << v.smoothTurn << "     # degrees a second the right stick turns you; 0: snap\n";
    out << "web_button = " << (v.triggerWebs ? "trigger" : "grip")
        << "   # which one shoots and holds webs\n";
    out << "aim_markers = " << onOff(v.aimMarkers) << "   # where each free hand's web would stick\n";
    out << "flips = " << onOff(v.flips)
        << "         # the left stick and A in the air flip you (experimental)\n";
    out << "flip_speed = " << v.flipSpeed << "    # degrees a second at full tilt\n";
    out << "stand_on_walls = " << onOff(v.standOnWalls)
        << " # on a wall, your view turns so the wall is your floor\n";
    out << "swing_speed = " << static_cast<int>(v.swingSpeed) << "    # the fastest a web flies you, m/s\n";
    out << "weight = " << v.weight << "        # percent of real gravity while you fly\n";
    out << "air_webs = " << onOff(v.airWebs)
        << "      # a web that meets nothing in reach holds in open air\n";
    out << "physical_webs = " << onOff(v.physicalWebs) << " # webs bend round edges like ropes\n";
    out << "haptics = " << v.haptics << "       # vibration, percent\n";
}
vr_settings::Values loadSettings(const std::string& path) {
    vr_settings::Values v;
    std::ifstream in(path);
    if (!in) {
        saveSettings(path, v);
        quest::log("Settings: wrote the defaults to " + path);
        return v;
    }
    std::string line;
    while (std::getline(in, line)) {
        line = line.substr(0, line.find('#'));
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key, value;
        std::istringstream(line.substr(0, eq)) >> key;
        std::istringstream(line.substr(eq + 1)) >> value;
        const bool on = value == "on" || value == "true" || value == "1" || value == "trigger";
        const int number = std::atoi(value.c_str());
        if (key == "snap_turn")
            v.snapTurn = number;
        else if (key == "smooth_turn")
            v.smoothTurn = number;
        else if (key == "web_button")
            v.triggerWebs = on;
        else if (key == "aim_markers")
            v.aimMarkers = on;
        else if (key == "flips")
            v.flips = on;
        else if (key == "flip_speed")
            v.flipSpeed = number;
        else if (key == "stand_on_walls")
            v.standOnWalls = on;
        else if (key == "swing_speed")
            v.swingSpeed = static_cast<float>(number);
        else if (key == "weight")
            v.weight = number;
        else if (key == "air_webs")
            v.airWebs = on;
        else if (key == "physical_webs")
            v.physicalWebs = on;
        else if (key == "haptics")
            v.haptics = number;
        else if (!key.empty())
            quest::log("Settings: no setting called " + key);
    }
    v = vr_settings::sanitized(v);
    quest::log("Settings: snap " + std::to_string(v.snapTurn) + ", smooth " + std::to_string(v.smoothTurn) +
               ", webs on the " + (v.triggerWebs ? "trigger" : "grip") + ", flips " +
               (v.flips ? "on" : "off"));
    return v;
}
// Vertices added since `begin` were built around the origin: place them.
void place(std::vector<Vertex>& out, size_t begin, Vec3 position, Quat orientation) {
    for (size_t i = begin; i < out.size(); ++i)
        out[i].position = position + orientation.rotate(out[i].position);
}
// A thug: legs, torso, arms and a masked head around its centre of mass.
void addThug(std::vector<Vertex>& out, const LabProp& p) {
    const size_t begin = out.size();
    const Vec3 pants{.08f, .09f, .12f}, skin{.55f, .4f, .3f}, mask{.03f, .03f, .035f};
    for (float side : {-1.f, 1.f}) {
        addBeam(out, {side * .1f, -.1f, 0}, {side * .11f, -.93f, 0}, .075f, pants);
        addBeam(out, {side * .24f, .38f, 0}, {side * .29f, -.12f, -.05f}, .06f, p.color);
    }
    addBeam(out, {0, -.12f, 0}, {0, .42f, 0}, .18f, p.color);
    addBox(out, {-.11f, .48f, -.11f}, {.11f, .72f, .11f}, skin);
    addBox(out, {-.115f, .58f, -.115f}, {.115f, .64f, -.09f}, mask);
    place(out, begin, p.position, p.orientation);
}
void addProp(std::vector<Vertex>& out, const LabProp& p) {
    if (p.kind == TargetKind::Character) {
        addThug(out, p);
        return;
    }
    const size_t begin = out.size();
    addBox(out, -p.half, p.half, p.color);
    // Darker bands make a crate's or barrel's tumbling readable.
    addBox(out, Vec3{-p.half.x - .005f, -p.half.y * .15f, -p.half.z - .005f},
           Vec3{p.half.x + .005f, p.half.y * .15f, p.half.z + .005f}, p.color * .55f);
    place(out, begin, p.position, p.orientation);
}

void run(android_app* app, AppState& state) {
    quest::Xr xr;
    xr.initialize(app);
    const auto started = std::chrono::steady_clock::now();
    const city::City city;
    const city::CityWorld world(city.boxes());
    city::Renderer renderer;
    renderer.initialize(city.mesh(), assets(app->activity->assetManager), quest::log);
    quest::log(
        "City: " + std::to_string(city.buildings()) + " buildings, " +
        std::to_string(city.mesh().indices.size() / 3) + " triangles, ready in " +
        std::to_string(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count()) +
        " s");
    LabProps props(city.props());
    Swing swing(LabMotion::swingConfig());
    WebGrab grab;
    LabMotion motion;
    const std::string files = app->activity->externalDataPath ? app->activity->externalDataPath : "";
    const std::string settingsPath = files + "/settings.txt";
    auto settings = loadSettings(settingsPath);
    float vibration = 1;
    const auto apply = [&] {
        motion.configure(settings);
        swing.limitSpeed(settings.swingSpeed);
        swing.setGravity(vr_settings::gravity(settings.weight));
        swing.allowAirAnchors(settings.airWebs);
        swing.allowPhysical(settings.physicalWebs);
        vibration = static_cast<float>(settings.haptics) / 100;
    };
    apply();
    LabMenu menu;
    std::vector<Vertex> ui;
    bool menuHeld = false, settingsChanged = false;
    bool heldFromMenu[3]{}; // each hand's trigger and grip, and A
    int time = 0;
    const auto buzz = [&](int hand, float strength, float seconds = .025f) {
        if (vibration > 0)
            xr.haptic(hand, strength * vibration, seconds);
    };
    const Vec3 facing = Quat::yaw(city.launch().yaw).rotate({0, 0, -1});
    std::vector<Vertex> geometry;
    bool resetHeld = false, timeHeld = false, haveHead = false;
    Pose lastHead{};
    unsigned knockdowns = 0;
    int64_t pulseUntil[2]{};
    float clock = 0;
    const auto launch = city.launch();
    auto reset = [&]() {
        swing.reset({launch.feet + Vec3{0, .3501f, 0}, {}, true});
        grab.releaseAll();
        props.reset();
        knockdowns = 0;
        motion.reset();
    };
    reset();
    quest::log(
        "Squeeze a grip: web and swing. Trigger: reel. Pull sharply: zip. A: jump; hold it on the ground to "
        "leap. Walk or fly into a wall to run along it and climb it. Y: back to the roof. X: day, sunset, "
        "night. Menu button: settings.");
    while (!app->destroyRequested) {
        // Android's events: wait for them while no frames run (paused, or
        // the runtime not ready yet); between frames, only take what is there.
        for (;;) {
            int events = 0;
            android_poll_source* source = nullptr;
            const int timeout = xr.running() ? 0 : state.resumed ? 10 : 250;
            if (ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void**>(&source)) < 0)
                break;
            if (source)
                source->process(app, source);
            if (app->destroyRequested)
                return;
        }
        if (!xr.poll()) {
            ANativeActivity_finish(app->activity);
            return;
        }
        if (!xr.running())
            continue;
        xr.frame(
            [&](const XrFrame& f) {
                clock += f.seconds;
                // The menu button: the pause menu, and the lab waits behind it.
                const bool menuButton = (f.buttons & buttonMenu) != 0;
                if (f.focused && f.valid && menuButton && !menuHeld && !menu.open()) {
                    menu.show(motion.toWorld(f.head));
                    buzz(0, .3f);
                }
                menuHeld = menuButton;
                ui.clear();
                if (menu.open()) {
                    LabMenu::Input in;
                    for (int i = 0; i < 2; ++i) {
                        in.aimValid[i] = f.focused && f.hands[i].valid;
                        in.aims[i] = motion.toWorld(f.hands[i].aim);
                        in.triggers[i] = f.hands[i].trigger;
                        if (std::hypot(f.hands[i].stickX, f.hands[i].stickY) >
                            std::hypot(in.stickX, in.stickY)) {
                            in.stickX = f.hands[i].stickX;
                            in.stickY = f.hands[i].stickY;
                        }
                    }
                    in.a = f.jump;
                    in.b = (f.buttons & buttonB) != 0;
                    in.menu = menuButton;
                    in.seconds = f.seconds;
                    switch (menu.update(in, settings, time)) {
                    case LabMenu::Action::changed:
                        apply();
                        settingsChanged = true;
                        buzz(1, .25f);
                        break;
                    case LabMenu::Action::timeOfDay:
                        renderer.timeOfDay(static_cast<city::TimeOfDay>(time));
                        buzz(1, .25f);
                        break;
                    case LabMenu::Action::launchRoof:
                        reset();
                        menu.close();
                        break;
                    default:
                        break;
                    }
                    if (menu.hoverHand() >= 0)
                        buzz(menu.hoverHand(), .12f, .01f);
                    if (!menu.open() && settingsChanged) {
                        saveSettings(settingsPath, settings);
                        settingsChanged = false;
                        quest::log("Settings: saved");
                    }
                    menu.draw(ui, settings, time);
                    // The lab waits: crates, thugs and hands drawn where they are.
                    geometry.clear();
                    for (const auto& p : props.props())
                        addProp(geometry, p);
                    for (int i = 0; i < 2; ++i)
                        if (f.hands[i].valid)
                            addTrackedHand(geometry, motion.toWorld(f.hands[i].grip),
                                           static_cast<unsigned>(i), f.hands[i].squeeze, false);
                    if (f.valid)
                        lastHead = motion.toWorld(f.head);
                    renderer.shadows(lastHead.position);
                    renderer.reflections(lastHead.position);
                    heldFromMenu[0] = heldFromMenu[1] = heldFromMenu[2] = true;
                    resetHeld = f.reset;
                    timeHeld = (f.buttons & buttonX) != 0;
                    return;
                }
                if (f.focused && f.valid && f.reset && !resetHeld)
                    reset();
                resetHeld = f.reset;
                // X: day, sunset, night.
                const bool timeButton = (f.buttons & buttonX) != 0;
                if (f.focused && timeButton && !timeHeld) {
                    time = (time + 1) % 3;
                    renderer.timeOfDay(static_cast<city::TimeOfDay>(time));
                    const char* names[] = {"day", "sunset", "night"};
                    quest::log(std::string("Time of day: ") + names[time]);
                }
                timeHeld = timeButton;
                // Turning, flips, the web button, walls and the charged leap.
                // Buttons still held from the menu do nothing until let go.
                XrFrame played = f;
                for (int i = 0; i < 2; ++i) {
                    heldFromMenu[i] =
                        heldFromMenu[i] && (f.hands[i].trigger > .1f || f.hands[i].squeeze > .1f);
                    if (heldFromMenu[i])
                        played.hands[i].trigger = played.hands[i].squeeze = 0;
                }
                heldFromMenu[2] = heldFromMenu[2] && f.jump;
                if (heldFromMenu[2])
                    played.jump = false;
                const auto moved = motion.update(played, swing, facing);
                if (moved.chargeBegan)
                    buzz(1, .3f);
                else if (moved.charging) {
                    // It builds in both hands, and holds at full.
                    buzz(0, .08f + .3f * moved.charge);
                    buzz(1, .1f + .4f * moved.charge);
                }
                if (moved.leapt) {
                    buzz(0, .8f, .12f);
                    buzz(1, 1, .12f);
                }
                if (moved.motion.releaseWebs)
                    swing.releaseAll();
                // A press aimed at a prop belongs to the grab, never to the swing.
                const auto forSwing = grab.claim(f.seconds, moved.motion.swing, world, props, swing.body());
                swing.update(f.seconds, forSwing, world);
                props.advance(f.seconds, grab, world);
                for (auto event : swing.events()) {
                    if (event.hand == 0 || event.hand == 1)
                        buzz(event.hand, event.strength);
                    else if (event.kind == EventKind::WallOn || event.kind == EventKind::Leap) {
                        buzz(0, std::min(event.strength, 1.f) * .6f, .05f);
                        buzz(1, std::min(event.strength, 1.f) * .6f, .05f);
                    }
                }
                for (const auto& event : grab.events()) {
                    buzz(event.hand, event.strength);
                    if (event.hand == 0 || event.hand == 1)
                        pulseUntil[event.hand] = f.predictedDisplayTime + 30'000'000;
                }
                for (int i = 0; i < 2; ++i)
                    if (const auto& held = grab.grabs()[i]; held.phase != GrabPhase::None &&
                                                            held.tension > .05f &&
                                                            f.predictedDisplayTime >= pulseUntil[i])
                        buzz(i, std::min(.6f, .6f * held.tension));
                if (props.knockdowns() != knockdowns) {
                    knockdowns = props.knockdowns();
                    quest::log("Thugs knocked down: " + std::to_string(knockdowns));
                }
                // Into the river: back to the roof.
                if (swing.body().position.y < city::City::waterLevel + .6f)
                    reset();
                if (f.valid) {
                    lastHead = motion.toWorld(f.head);
                    haveHead = true;
                }
                geometry.clear();
                for (const auto& p : props.props())
                    addProp(geometry, p);
                const Vec3 viewer = haveHead ? lastHead.position : swing.body().position;
                for (int i = 0; i < 2; ++i) {
                    if (!f.hands[i].valid)
                        continue;
                    const auto hand = motion.toWorld(f.hands[i].grip);
                    const auto& web = swing.webs()[i];
                    addTrackedHand(geometry, hand, static_cast<unsigned>(i), f.hands[i].squeeze,
                                   web.attached);
                    const auto& held = grab.grabs()[i];
                    const auto target =
                        held.phase != GrabPhase::None ? props.find(held.target) : std::nullopt;
                    if (web.attached || target) {
                        WebLine line;
                        line.start = webWrist(hand);
                        line.end = target ? target->position : web.anchor;
                        const float rope = target ? held.length : web.length;
                        const bool taut = target ? held.taut : web.tension > 0;
                        line.slack = taut ? 0.f : std::max(rope - length(line.end - line.start), 0.f);
                        line.seed = static_cast<float>(i) * 1.7f;
                        appendWeb(geometry, line, viewer, .001f);
                    } else {
                        const auto aim = motion.toWorld(f.hands[i].aim);
                        const Vec3 color = i ? Vec3{.2f, .65f, 1} : Vec3{1, .12f, .18f};
                        if (!settings.aimMarkers) {
                            // AIM MARKERS off: none.
                        } else if (const auto next = grab.preview(aim, world, props)) {
                            const Vec3 top = next->position + Vec3{0, next->radius + .25f, 0};
                            addBox(geometry, top - Vec3{.06f, .06f, .06f}, top + Vec3{.06f, .06f, .06f},
                                   {1, .85f, .2f});
                        } else if (auto hit =
                                       world.raycast(aim.position, aim.orientation.rotate({0, 0, -1}), 100)) {
                            // Where this hand's web would stick, scaled to stay visible far off.
                            const float r = std::clamp(length(hit->point - viewer) * .006f, .04f, .5f);
                            addBox(geometry, hit->point - Vec3{r, r, r}, hit->point + Vec3{r, r, r}, color);
                        }
                    }
                }
                renderer.shadows(viewer);
                // The city in glass and water: one face of the reflection a frame.
                renderer.reflections(viewer);
            },
            [&](unsigned, const Pose& eye, const EyeFov& fov, unsigned width, unsigned height) {
                const Pose world = motion.toWorld(eye);
                const Mat4 vp = multiply(city::glProjection(fov), viewMatrix(world));
                renderer.view(vp, world.position, width, height, geometry, clock, ui);
            });
    }
}
} // namespace

void android_main(android_app* app) {
    AppState state;
    app->userData = &state;
    app->onAppCmd = onCommand;
    JNIEnv* env = nullptr;
    app->activity->vm->AttachCurrentThread(&env, nullptr);
    try {
        run(app, state);
    } catch (const std::exception& e) {
        quest::log(std::string("Spidy City Lab stopped: ") + e.what());
        ANativeActivity_finish(app->activity);
    }
    app->activity->vm->DetachCurrentThread();
}
