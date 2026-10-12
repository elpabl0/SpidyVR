// Spidy City Lab on the Quest: the swinging lab (apps/xr_lab.cpp on Windows)
// running natively on the headset, in the photo-textured city (city.hpp).
// The game itself is not here: Spider-Man Remastered runs only on a PC.
#include "quest_xr.hpp"
#include "spidy/city.hpp"
#include "spidy/city_renderer.hpp"
#include "spidy/game_tracking.hpp"
#include "spidy/lab_props.hpp"
#include "spidy/overlay_geometry.hpp"
#include "spidy/web_visual.hpp"
#include <android/asset_manager.h>
#include <android_native_app_glue.h>
#include <chrono>
#include <cmath>
#include <optional>
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
    Swing swing;
    WebGrab grab;
    Rig rig;
    std::vector<Vertex> geometry;
    bool snapHeld = false, resetHeld = false, haveHead = false;
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
        rig = {launch.feet, launch.yaw};
    };
    reset();
    quest::log(
        "Squeeze a grip: web and swing. Trigger: reel. Pull sharply: zip. A: jump. Y: back to the roof.");
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
                if (f.focused && f.valid && f.reset && !resetHeld)
                    reset();
                resetHeld = f.reset;
                const float turn = f.hands[1].stickX;
                if (f.focused && f.valid && std::abs(turn) > .7f && !snapHeld) {
                    rig.turn(turn > 0 ? -.5235988f : .5235988f, f.head.position);
                    snapHeld = true;
                }
                if (std::abs(turn) < .3f)
                    snapHeld = false;
                const auto in = trackedSwingInput(f, rig);
                // A press aimed at a prop belongs to the grab, never to the swing.
                const auto forSwing = grab.claim(f.seconds, in, world, props, swing.body());
                const Vec3 previous = swing.body().position;
                swing.update(f.seconds, forSwing, world);
                rig.origin += swing.body().position - previous;
                props.advance(f.seconds, grab, world);
                for (auto event : swing.events())
                    xr.haptic(event.hand, event.strength);
                for (const auto& event : grab.events()) {
                    xr.haptic(event.hand, event.strength);
                    if (event.hand == 0 || event.hand == 1)
                        pulseUntil[event.hand] = f.predictedDisplayTime + 30'000'000;
                }
                for (int i = 0; i < 2; ++i)
                    if (const auto& held = grab.grabs()[i]; held.phase != GrabPhase::None &&
                                                            held.tension > .05f &&
                                                            f.predictedDisplayTime >= pulseUntil[i])
                        xr.haptic(i, std::min(.6f, .6f * held.tension));
                if (props.knockdowns() != knockdowns) {
                    knockdowns = props.knockdowns();
                    quest::log("Thugs knocked down: " + std::to_string(knockdowns));
                }
                // Into the river: back to the roof.
                if (swing.body().position.y < city::City::waterLevel + .6f)
                    reset();
                if (f.valid) {
                    lastHead = rig.toWorld(f.head);
                    haveHead = true;
                }
                geometry.clear();
                for (const auto& p : props.props())
                    addProp(geometry, p);
                const Vec3 viewer = haveHead ? lastHead.position : swing.body().position;
                for (int i = 0; i < 2; ++i) {
                    if (!f.hands[i].valid)
                        continue;
                    const auto hand = rig.toWorld(f.hands[i].grip);
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
                        const auto aim = rig.toWorld(f.hands[i].aim);
                        const Vec3 color = i ? Vec3{.2f, .65f, 1} : Vec3{1, .12f, .18f};
                        if (const auto next = grab.preview(aim, world, props)) {
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
            },
            [&](unsigned, const Pose& eye, const EyeFov& fov, unsigned width, unsigned height) {
                const Pose world = rig.toWorld(eye);
                const Mat4 vp = multiply(city::glProjection(fov), viewMatrix(world));
                renderer.view(vp, world.position, width, height, geometry, clock);
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
