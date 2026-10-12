#include "spidy/lab_menu.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace spidy;
using vr_settings::Item;
namespace {
void check(bool ok, const std::string& what) {
    if (!ok)
        throw std::runtime_error(what);
}
int rowOf(Item item) {
    const auto& rows = LabMenu::rows();
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].item == item)
            return static_cast<int>(i);
    throw std::runtime_error("no row for a setting");
}
int rowNamed(const std::string& title) {
    const auto& rows = LabMenu::rows();
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (title == rows[i].title)
            return static_cast<int>(i);
    throw std::runtime_error("no row " + title);
}
// The right controller at the chest, pointing at `target`.
LabMenu::Input pointAt(Vec3 target, bool pull) {
    LabMenu::Input in;
    in.seconds = 1.f / 72;
    const Vec3 hand{.2f, 1.3f, -.2f};
    in.aims[1] = {hand, arc({0, 0, -1}, normalized(target - hand))};
    in.aimValid[1] = true;
    in.triggers[1] = pull ? 1.f : 0.f;
    return in;
}
// A pull of the right trigger at a point: pressed, then let go.
LabMenu::Action click(LabMenu& m, Vec3 at, vr_settings::Values& v, int& time) {
    m.update(pointAt(at, false), v, time);
    const auto a = m.update(pointAt(at, true), v, time);
    m.update(pointAt(at, false), v, time);
    return a;
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
    const Pose head{{0, 1.7f, 0}, {}};
    test("the menu has the lab's settings and not the game screen's", [] {
        for (Item item : {Item::webButton, Item::snapTurn, Item::smoothTurn, Item::flips, Item::standOnWalls,
                          Item::swingSpeed, Item::weight, Item::haptics, Item::airWebs, Item::physicalWebs})
            rowOf(item);
        for (const auto& r : LabMenu::rows())
            check(r.item != Item::hud && r.item != Item::screenSize && r.item != Item::calibrate &&
                      r.item != Item::view && r.item != Item::screenDepth,
                  std::string("a game-only row: ") + r.title);
        for (const auto& r : LabMenu::rows())
            for (const auto& c : LabMenu::choices(r))
                check(c.find('\xC2') == std::string::npos, "a choice the font cannot draw: " + c);
    });
    test("pointing at a row and pulling the trigger steps its choice; at its < the one before", [&] {
        LabMenu m;
        vr_settings::Values v;
        int time = 0;
        m.show(head);
        const int snap = rowOf(Item::snapTurn);
        check(v.snapTurn == 30, "defaults changed");
        check(click(m, m.rowPoint(snap), v, time) == LabMenu::Action::changed && v.snapTurn == 45,
              "the trigger did not step snap turn to 45: " + std::to_string(v.snapTurn));
        click(m, m.rowPoint(snap, true), v, time);
        click(m, m.rowPoint(snap, true), v, time);
        check(v.snapTurn == 15, "the < did not step back to 15: " + std::to_string(v.snapTurn));
        const int flips = rowOf(Item::flips);
        click(m, m.rowPoint(flips), v, time);
        check(v.flips, "flips did not switch on");
        const int webs = rowOf(Item::webButton);
        click(m, m.rowPoint(webs), v, time);
        check(v.triggerWebs, "the web button did not move to the trigger");
    });
    test("the time of day and the way back to the roof", [&] {
        LabMenu m;
        vr_settings::Values v;
        int time = 0;
        m.show(head);
        check(click(m, m.rowPoint(rowNamed("TIME OF DAY")), v, time) == LabMenu::Action::timeOfDay &&
                  time == 1,
              "no sunset");
        click(m, m.rowPoint(rowNamed("TIME OF DAY")), v, time);
        click(m, m.rowPoint(rowNamed("TIME OF DAY")), v, time);
        check(time == 0, "night did not go round to day");
        check(click(m, m.rowPoint(rowNamed("BACK TO THE ROOF")), v, time) == LabMenu::Action::launchRoof,
              "no way back");
        check(click(m, m.rowPoint(rowNamed("CLOSE")), v, time) == LabMenu::Action::closed && !m.open(),
              "CLOSE did not close");
    });
    test("the sticks choose rows past the headings and step choices; B closes", [&] {
        LabMenu m;
        vr_settings::Values v;
        int time = 0;
        m.show(head);
        LabMenu::Input in;
        in.seconds = 1.f / 72;
        in.b = true; // still held from before the menu opened: not a close
        m.update(in, v, time);
        check(m.open(), "a B held from before closed the menu");
        in.b = false;
        // Down to the snap turn row, past the headings, then right.
        const int target = rowOf(Item::snapTurn);
        for (int i = 0; i < 40 && m.selected() != target; ++i) {
            in.stickY = -1;
            m.update(in, v, time);
            in.stickY = 0;
            m.update(in, v, time);
            check(LabMenu::rows()[static_cast<std::size_t>(m.selected())].item != Item::none ||
                      LabMenu::rows()[static_cast<std::size_t>(m.selected())].lab,
                  "the stick stopped on a heading");
        }
        check(m.selected() == target, "the stick never reached snap turn");
        in.stickX = 1;
        m.update(in, v, time);
        check(v.snapTurn == 45, "right did not step snap turn");
        in.b = true;
        check(m.update(in, v, time) == LabMenu::Action::closed && !m.open(), "B did not close");
    });
    test("the menu draws its panel, rows and pointer", [&] {
        LabMenu m;
        vr_settings::Values v;
        int time = 2;
        std::vector<Vertex> out;
        m.draw(out, v, time);
        check(out.empty(), "a closed menu drew");
        m.show(head);
        m.update(pointAt(m.rowPoint(rowOf(Item::weight)), false), v, time);
        m.draw(out, v, time);
        check(out.size() > 3000, "too little drawn: " + std::to_string(out.size()));
        for (const auto& vertex : out)
            check(finite(vertex.position) && length(vertex.position - head.position) < 3, "a vertex astray");
    });
    std::cout << total - failed << '/' << total << " tests passed\n";
    return failed ? 1 : 0;
}
