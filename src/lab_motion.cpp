#include "spidy/lab_motion.hpp"

namespace spidy {
namespace {
float radians(int degrees) {
    return static_cast<float>(degrees) * 3.14159265f / 180;
}
} // namespace
SwingConfig LabMotion::swingConfig() {
    SwingConfig c;
    c.walls = true;
    return c;
}
void LabMotion::configure(const vr_settings::Values& values) {
    rig_.snapTurn(radians(values.snapTurn));
    rig_.smoothTurn(radians(values.smoothTurn));
    rig_.flips(values.flips);
    rig_.flipSpeed(radians(values.flipSpeed));
    rig_.triggerWebs(values.triggerWebs);
    rig_.standOnWalls(values.standOnWalls);
}
void LabMotion::reset() {
    rig_.reset();
    jump_ = {};
}
LabMotion::Frame LabMotion::update(const XrFrame& f, const Swing& swing, Vec3 forward) {
    const Body& body = swing.body();
    const auto& wall = swing.wall();
    const Vec3 feet = body.position - Vec3{0, swing.config().radius, 0};
    const bool webbed = swing.webs()[0].attached || swing.webs()[1].attached;
    // A held on the ground charges a leap; a tap there is a jump as it is
    // let go; anywhere else A is A as it is held (off a wall, a flip).
    const auto keys = jump_.update(f.seconds, f.jump, body.grounded && !wall.on && !webbed);
    XrFrame controller = f;
    controller.jump = keys.jump;
    // The wall that holds the body: its floor on the wall beside its centre.
    SurfaceHold hold;
    if (wall.on) {
        hold.normal = wall.normal;
        hold.anchor = body.position - wall.normal * wall.distance;
        hold.speed = length(body.velocity);
    }
    Frame out;
    out.motion = rig_.update(controller, feet, forward, true, {0, 1, 0}, !body.grounded && !wall.on, hold);
    auto& in = out.motion.swing;
    if (out.motion.active) {
        // The tracking space as the rig placed it, from the head both share.
        placement_ = compose(out.motion.headPose, inverse(f.head));
        in.leap = keys.leap;
        // Walked at a wall from the ground, the body jumps onto it.
        in.jump = in.jump || swing.mounting();
        const Vec3 look = out.motion.headPose.orientation.rotate({0, 0, -1});
        if (finite(look) && length(look) > .5f)
            in.look = normalized(look);
    }
    out.charging = jump_.charging();
    out.charge = jump_.charge();
    out.chargeBegan = jump_.charges() != charges_;
    out.leapt = jump_.leaps() != leaps_;
    charges_ = jump_.charges();
    leaps_ = jump_.leaps();
    return out;
}
} // namespace spidy
