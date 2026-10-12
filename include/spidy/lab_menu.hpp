#pragma once
#include "math.hpp"
#include "vertex.hpp"
#include "vr_settings.hpp"
#include <string>
#include <vector>

namespace spidy {
// The lab's own pause menu: the game's SPIDY VR settings that the lab has
// (not the game screen's, the HUD or the body calibration), the time of day,
// and a way back to the launch roof. It stands in front of the player when
// opened (the menu button), and the lab waits behind it.
//
// Point either controller at a row and pull its trigger: the next choice
// (aimed at the "<" before a choice, the one before). Or the thumbsticks:
// up and down choose a row, left and right its choice; A the next choice.
// B or the menu button closes it.
class LabMenu {
  public:
    enum class Action { none, changed, timeOfDay, launchRoof, closed };
    struct Input {
        Pose aims[2]{}; // each controller's aim, in the world
        bool aimValid[2]{};
        float triggers[2]{};
        float stickX{}, stickY{}; // either thumbstick, the larger tilt
        bool a{}, b{}, menu{};
        float seconds{};
    };
    bool open() const {
        return open_;
    }
    // The row the sticks and A act on (the last one pointed at).
    int selected() const {
        return selected_;
    }
    // Opens facing the head (a world pose), level, a little below the eyes.
    void show(Pose head);
    void close();
    // What the input did. Settings change in `values`; the time of day (0
    // day, 1 sunset, 2 night) in `time`.
    Action update(const Input&, vr_settings::Values& values, int& time);
    // The panel, its rows and the pointers, into `out` (drawn over all else,
    // in this order).
    void draw(std::vector<Vertex>& out, const vr_settings::Values&, int time) const;
    // A pointer moved onto another row: a tick in that hand.
    int hoverHand() const {
        return hoverTick_;
    }
    struct Row {
        vr_settings::Item item{};
        int lab{}; // 0: a game setting (or a heading when item is none); else the lab's own row
        const char* title{};
        const char* help{};
    };
    static const std::vector<Row>& rows();
    // The middle of a row on the panel (or of its "<"), in the world.
    Vec3 rowPoint(int row, bool back = false) const;
    // The choices a row shows, and the one chosen.
    static std::vector<std::string> choices(const Row&);
    static int chosen(const Row&, const vr_settings::Values&, int time);

  private:
    // Where a pointer meets the panel: its row (-1: none) and whether it is
    // on that row's "<".
    struct Hit {
        int row = -1;
        bool back{};
        Vec3 point{}, from{};
        bool onPanel{};
    };
    Hit hit(Pose aim) const;
    Action step(int row, int by, vr_settings::Values&, int& time);
    Pose panel_{};
    bool open_{};
    int selected_ = 1;
    Hit hits_[2]{};
    bool triggerHeld_[2]{}, aHeld_{}, bHeld_{}, menuHeld_{};
    float stickWait_{};
    bool stickHeld_{};
    int hoverTick_ = -1;
};
} // namespace spidy
