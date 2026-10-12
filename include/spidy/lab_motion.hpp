#pragma once
#include "game_pad.hpp"
#include "game_tracking.hpp"
#include "swing.hpp"
#include "vr_settings.hpp"

namespace spidy {
// The lab's movement, as the game has it: the game's tracking rig (snap or
// smooth turning, flips, webs on either button, the view turning onto a wall
// the player stands on) and its charged leap, on the lab's own swing in
// place of the game's player. The swing walks, jumps, runs along walls and
// climbs them itself (SwingConfig's walls), so the rig follows the swing's
// body where the game's version follows the game's actor.
class LabMotion {
  public:
    struct Frame {
        // The rig's frame: the swing's input (leap and look in it), the head
        // in the world, whether the view stands on a wall.
        GameMotionFrame motion;
        bool charging{};             // A held on the ground charges a leap
        float charge{};              // how far, 0 to 1
        bool chargeBegan{}, leapt{}; // this frame: a charge began, a leap went
    };
    // The swing the lab moves with walls on, as the lab wants them.
    static SwingConfig swingConfig();
    // Turning, flips, the web button and standing on walls from the settings.
    void configure(const vr_settings::Values&);
    // A new start: the rig lines up again with the next frame.
    void reset();
    // forward: the way the lab faces the player at a start (level).
    Frame update(const XrFrame&, const Swing&, Vec3 forward);
    // A pose in the tracking space (the headset's, an eye's, a hand's), in
    // the world: where the rig placed the tracking space at the last frame
    // that tracked.
    Pose toWorld(Pose tracked) const {
        return compose(placement_, tracked);
    }

  private:
    GameTrackingRig rig_;
    game_pad::ChargeJump jump_;
    Pose placement_{};
    std::uint32_t charges_{}, leaps_{};
};
} // namespace spidy
