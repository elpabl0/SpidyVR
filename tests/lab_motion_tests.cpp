#include "spidy/city.hpp"
#include "spidy/lab_motion.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace spidy;
using namespace spidy::city;
namespace {
void check(bool ok, const std::string& what) {
    if (!ok)
        throw std::runtime_error(what);
}
const City& theCity() {
    static const City c;
    return c;
}
const CityWorld& theWorld() {
    static const CityWorld w(theCity().boxes());
    return w;
}
// A player standing still at 1.7 m, looking ahead (-z), both controllers at
// the chest, at 72 Hz.
struct Player {
    XrFrame f;
    std::int64_t time = 1;
    Player() {
        f.focused = f.valid = true;
        f.seconds = 1.f / 72;
        f.head = {{0, 1.7f, 0}, {}};
        for (unsigned i = 0; i < 2; ++i) {
            f.eyes[i] = {{{i ? .032f : -.032f, 1.7f, 0}, {}}, {-.8f, .8f, -.8f, .8f}, 1000, 1000};
            auto& h = f.hands[i];
            h.valid = true;
            h.aim = h.grip = {{i ? .3f : -.3f, 1.3f, -.3f}, {}};
        }
    }
    XrFrame next() {
        f.predictedDisplayTime = time += 13'888'889;
        return f;
    }
};
// One frame of the lab: the rig, then the swing.
LabMotion::Frame step(LabMotion& motion, Swing& swing, Player& p, Vec3 forward = {0, 0, -1}) {
    const auto frame = motion.update(p.next(), swing, forward);
    swing.update(p.f.seconds, frame.motion.swing, theWorld());
    return frame;
}
float yawOf(const LabMotion& m) {
    const Vec3 ahead = m.toWorld({{}, {}}).orientation.rotate({0, 0, -1});
    return std::atan2(-ahead.x, -ahead.z);
}
Swing standingOnRoof() {
    Swing s(LabMotion::swingConfig());
    s.reset({theCity().launch().feet + Vec3{0, .3501f, 0}, {}, true});
    return s;
}
} // namespace
int main() {
    int total = 0, failed = 0;
    auto test = [&](const char* name, const std::function<void()>& f) {
        ++total;
        try {
            f();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& e) {
            ++failed;
            std::cerr << "FAIL " << name << ": " << e.what() << '\n';
        }
    };
    test("a tap of A on the roof jumps, and the player lands again", [] {
        LabMotion m;
        Swing s = standingOnRoof();
        Player p;
        for (int i = 0; i < 20; ++i)
            step(m, s, p);
        const float ground = s.body().position.y;
        p.f.jump = true;
        for (int i = 0; i < 6; ++i)
            step(m, s, p);
        p.f.jump = false;
        float top = ground;
        for (int i = 0; i < 150; ++i) {
            const auto frame = step(m, s, p);
            check(!frame.leapt, "a tap leapt");
            top = std::max(top, s.body().position.y);
        }
        check(top > ground + .5f, "no jump: up " + std::to_string(top - ground) + " m");
        check(s.body().grounded && std::abs(s.body().position.y - ground) < .05f, "did not land on the roof");
    });
    test("A held on the roof charges a leap that goes far higher", [] {
        LabMotion m;
        Swing s = standingOnRoof();
        Player p;
        for (int i = 0; i < 20; ++i)
            step(m, s, p);
        const float ground = s.body().position.y;
        p.f.jump = true;
        bool began = false;
        float charge = 0;
        for (int i = 0; i < 80; ++i) {
            const auto frame = step(m, s, p);
            began |= frame.chargeBegan;
            charge = std::max(charge, frame.charge);
            check(s.body().grounded, "left the ground while charging");
        }
        check(began && charge > .9f, "no full charge: " + std::to_string(charge));
        p.f.jump = false;
        bool leapt = false;
        float top = ground;
        for (int i = 0; i < 200; ++i) {
            leapt |= step(m, s, p).leapt;
            top = std::max(top, s.body().position.y);
        }
        check(leapt, "no leap");
        check(top > ground + 8, "a full leap rose only " + std::to_string(top - ground) + " m");
    });
    test("the right stick snap turns 30 degrees, or turns smoothly when set", [] {
        LabMotion snap;
        Swing s = standingOnRoof();
        Player p;
        step(snap, s, p);
        const float before = yawOf(snap);
        p.f.hands[1].stickX = 1;
        for (int i = 0; i < 30; ++i)
            step(snap, s, p);
        const float snapped = std::remainder(yawOf(snap) - before, 6.2831853f);
        check(std::abs(snapped + .5235988f) < .01f, "a flick turned " + std::to_string(snapped));
        LabMotion smooth;
        vr_settings::Values v;
        v.smoothTurn = 90;
        smooth.configure(v);
        Swing t = standingOnRoof();
        Player q;
        step(smooth, t, q);
        const float start = yawOf(smooth);
        q.f.hands[1].stickX = 1;
        for (int i = 0; i < 36; ++i)
            step(smooth, t, q);
        const float half = std::remainder(yawOf(smooth) - start, 6.2831853f);
        check(std::abs(half + .7854f) < .05f, "half a second at 90 deg/s turned " + std::to_string(half));
    });
    test("with WEB BUTTON on the trigger, the trigger webs and the grip does not", [] {
        for (bool triggerWebs : {false, true}) {
            LabMotion m;
            vr_settings::Values v;
            v.triggerWebs = triggerWebs;
            m.configure(v);
            Swing s = standingOnRoof();
            Player p;
            // The right hand points up and ahead, at the towers.
            p.f.hands[1].aim.orientation = Quat::around({1, 0, 0}, .6f);
            for (int i = 0; i < 5; ++i)
                step(m, s, p);
            p.f.hands[1].trigger = 1;
            for (int i = 0; i < 10; ++i)
                step(m, s, p);
            check(s.webs()[1].attached == triggerWebs,
                  triggerWebs ? "the trigger did not web" : "the trigger webbed with the grip set");
        }
    });
    test("flying into a wall holds the player on it, and stopped there the view stands on it", [] {
        // A wall 40 m up: the first one straight ahead from the launch roof.
        const Vec3 eye = theCity().launch().feet + Vec3{0, 1.6f, 0};
        std::optional<RayHit> hit;
        for (float yaw = 0; !hit && yaw < 6.3f; yaw += .2f)
            if (auto h = theWorld().raycast(eye, {std::sin(yaw), 0, -std::cos(yaw)}, 80);
                h && std::abs(h->normal.y) < .1f && length(h->point - eye) > 10)
                hit = h;
        check(hit.has_value(), "no wall in sight of the roof");
        LabMotion m;
        Swing s(LabMotion::swingConfig());
        s.reset({hit->point + hit->normal * 4, hit->normal * -9.f, false});
        Player p;
        bool on = false, standing = false;
        float tilt = 0;
        for (int i = 0; i < 300; ++i) {
            const auto frame = step(m, s, p, hit->normal * -1.f);
            on |= s.wall().on;
            standing |= frame.motion.standing;
            tilt = std::max(tilt, frame.motion.viewTilt);
        }
        check(on, "the wall did not take the player");
        check(s.wall().on, "the player fell off the wall");
        check(standing && tilt > 1.4f, "the view did not stand on the wall: tilt " + std::to_string(tilt));
        // A jumps off; the view comes back level.
        p.f.jump = true;
        for (int i = 0; i < 4; ++i)
            step(m, s, p, hit->normal * -1.f);
        p.f.jump = false;
        float level = 10;
        for (int i = 0; i < 120; ++i)
            level = step(m, s, p, hit->normal * -1.f).motion.viewTilt;
        check(!s.wall().on || level < .2f, "a jump did not leave the wall");
    });
    test("walking at a building from the street climbs onto its wall", [] {
        // The street beside the launch building, facing a building across it.
        const auto& launch = theCity().launch();
        std::optional<RayHit> hit;
        Vec3 from{};
        for (float dx = 2; !hit && dx < 40; dx += 2) {
            from = {launch.feet.x - dx, .5f, launch.feet.z};
            for (Vec3 d : {Vec3{1, 0, 0}, Vec3{-1, 0, 0}, Vec3{0, 0, 1}, Vec3{0, 0, -1}})
                if (auto h = theWorld().raycast(from, d, 12);
                    h && std::abs(h->normal.y) < .1f && h->point.y > .3f &&
                    !theWorld().raycast(from + Vec3{0, 1, 0}, {0, -1, 0}, .4f))
                    hit = h;
        }
        check(hit.has_value(), "no wall beside the street");
        LabMotion m;
        Swing s(LabMotion::swingConfig());
        const auto ground = theWorld().raycast(hit->point + hit->normal * 3 + Vec3{0, 2, 0}, {0, -1, 0}, 5);
        check(ground.has_value(), "no ground before the wall");
        s.reset({ground->point + Vec3{0, .3501f, 0}, {}, true});
        Player p;
        p.f.hands[0].stickY = 1; // the left stick ahead: at the wall
        bool on = false;
        for (int i = 0; i < 300 && !on; ++i) {
            step(m, s, p, hit->normal * -1.f);
            on = s.wall().on;
        }
        check(on, "walking at the wall did not climb it");
    });
    test("with FLIPS on, the left stick in the air turns the player over, and lets go level", [] {
        for (bool flips : {false, true}) {
            LabMotion m;
            vr_settings::Values v;
            v.flips = flips;
            m.configure(v);
            Swing s(LabMotion::swingConfig());
            // High over the avenue, falling.
            s.reset({theCity().launch().feet + Vec3{-14, 60, 0}, {}, false});
            Player p;
            step(m, s, p);
            p.f.hands[0].stickY = 1;
            float most = 0;
            for (int i = 0; i < 40; ++i) {
                step(m, s, p);
                const Vec3 up = m.toWorld({{}, {}}).orientation.rotate({0, 1, 0});
                most = std::max(most, std::acos(std::clamp(up.y, -1.f, 1.f)));
            }
            if (!flips) {
                check(most < .01f, "the stick flipped with FLIPS off");
                continue;
            }
            check(most > 1.5f, "the stick turned the player only " + std::to_string(most));
            p.f.hands[0].stickY = 0;
            float tilt = 10;
            for (int i = 0; i < 150; ++i) {
                step(m, s, p);
                const Vec3 up = m.toWorld({{}, {}}).orientation.rotate({0, 1, 0});
                tilt = std::acos(std::clamp(up.y, -1.f, 1.f));
            }
            check(tilt < .05f, "not level again: " + std::to_string(tilt));
        }
    });
    std::cout << total - failed << '/' << total << " tests passed\n";
    return failed ? 1 : 0;
}
