#include "spidy/lab_menu.hpp"
#include "spidy/overlay_geometry.hpp"
#include "spidy/overlay_text.hpp"
#include <algorithm>
#include <cmath>

namespace spidy {
namespace {
using vr_settings::Item;
constexpr int labTime = 1, labRoof = 2, labClose = 3;
// The panel: metres, its left edge to its right, its top row down.
constexpr float width = 1.2f, rowHeight = .062f, textHeight = .028f, margin = .05f, choiceX = .74f,
                distance = 1.25f;
constexpr int helpLines = 2;
// The font has no degree sign.
std::string plain(const char* text) {
    std::string out;
    for (const char* c = text; *c; ++c) {
        if (static_cast<unsigned char>(c[0]) == 0xC2 && static_cast<unsigned char>(c[1]) == 0xB0) {
            out += " DEG";
            ++c;
        } else {
            out += *c;
        }
    }
    return out;
}
float panelHeight() {
    return static_cast<float>(LabMenu::rows().size()) * rowHeight + 2 * margin + helpLines * rowHeight;
}
} // namespace

const std::vector<LabMenu::Row>& LabMenu::rows() {
    static const std::vector<Row> all = [] {
        std::vector<Row> r;
        r.push_back({Item::none, 0, "SPIDY CITY LAB", nullptr});
        r.push_back(
            {Item::none, labTime, "TIME OF DAY", "Day, a golden sunset, or night with the street lamps on."});
        r.push_back({Item::none, labRoof, "BACK TO THE ROOF",
                     "Start again on the launch roof, crates and thugs reset."});
        for (const auto& row : vr_settings::rows()) {
            // The game screen's, the HUD's and the body's rows are the game's.
            if (row.item == Item::calibrate || row.item == Item::view || row.item == Item::screenSize ||
                row.item == Item::screenDepth || row.item == Item::hud)
                continue;
            if (row.item == Item::none && std::string(row.title) == "BODY")
                continue;
            r.push_back({row.item, 0, row.title, row.help});
        }
        r.push_back({Item::none, labClose, "CLOSE", "Back to the city (or press B, or the menu button)."});
        return r;
    }();
    return all;
}
std::vector<std::string> LabMenu::choices(const Row& row) {
    if (row.lab == labTime)
        return {"DAY", "SUNSET", "NIGHT"};
    if (row.lab)
        return {};
    for (const auto& r : vr_settings::rows())
        if (r.item == row.item && row.item != Item::none) {
            if (r.choices.empty())
                return {"OFF", "ON"};
            std::vector<std::string> out;
            for (const char* c : r.choices)
                out.push_back(plain(c));
            return out;
        }
    return {};
}
int LabMenu::chosen(const Row& row, const vr_settings::Values& values, int time) {
    if (row.lab == labTime)
        return time;
    return row.lab || row.item == Item::none ? 0 : vr_settings::choice(row.item, values);
}

void LabMenu::show(Pose head) {
    const Vec3 ahead = head.orientation.rotate({0, 0, -1});
    const Vec3 level =
        length(Vec3{ahead.x, 0, ahead.z}) > .1f ? normalized(Vec3{ahead.x, 0, ahead.z}) : Vec3{0, 0, -1};
    const Quat facing = Quat::yaw(std::atan2(-level.x, -level.z));
    // Its middle a little below the eyes, tipped back towards them.
    panel_ = {head.position + level * distance - Vec3{0, .12f, 0}, facing * Quat::around({1, 0, 0}, .12f)};
    open_ = true;
    triggerHeld_[0] = triggerHeld_[1] = aHeld_ = bHeld_ = menuHeld_ = true; // the press that opened it
    stickWait_ = 0;
}
void LabMenu::close() {
    open_ = false;
}

Vec3 LabMenu::rowPoint(int row, bool back) const {
    const float x = back ? choiceX - .01f : width / 3,
                down = margin + (static_cast<float>(row) + .5f) * rowHeight;
    const Vec3 local{x - width / 2, panelHeight() / 2 - down, 0};
    return panel_.position + panel_.orientation.rotate(local);
}
LabMenu::Hit LabMenu::hit(Pose aim) const {
    Hit h;
    const Vec3 normal = panel_.orientation.rotate({0, 0, 1}), dir = aim.orientation.rotate({0, 0, -1});
    const float facing = dot(dir, normal);
    if (facing > -1e-3f)
        return h;
    const float t = dot(panel_.position - aim.position, normal) / facing;
    if (t < 0 || t > 6)
        return h;
    h.point = aim.position + dir * t;
    h.from = aim.position;
    const Vec3 local = panel_.orientation.conjugate().rotate(h.point - panel_.position);
    const float x = local.x + width / 2, down = panelHeight() / 2 - local.y;
    h.onPanel = x >= 0 && x <= width && down >= 0 && down <= panelHeight();
    const int row = static_cast<int>(std::floor((down - margin) / rowHeight));
    if (h.onPanel && down >= margin && row >= 0 && row < static_cast<int>(rows().size())) {
        const auto& r = rows()[static_cast<std::size_t>(row)];
        if (r.item != Item::none || r.lab) {
            h.row = row;
            h.back = x > choiceX - .06f && x < choiceX + .04f;
        }
    }
    return h;
}

LabMenu::Action LabMenu::step(int row, int by, vr_settings::Values& values, int& time) {
    const auto& r = rows()[static_cast<std::size_t>(row)];
    if (r.lab == labRoof)
        return Action::launchRoof;
    if (r.lab == labClose) {
        open_ = false;
        return Action::closed;
    }
    const int n = static_cast<int>(choices(r).size());
    if (n == 0)
        return Action::none;
    const int next = ((chosen(r, values, time) + by) % n + n) % n;
    if (r.lab == labTime) {
        time = next;
        return Action::timeOfDay;
    }
    return vr_settings::choose(r.item, next, values) ? Action::changed : Action::none;
}

LabMenu::Action LabMenu::update(const Input& in, vr_settings::Values& values, int& time) {
    hoverTick_ = -1;
    if (!open_)
        return Action::none;
    Action action = Action::none;
    const auto take = [&](Action a) {
        if (a != Action::none)
            action = a;
    };
    // Closing: B or the menu button, pressed (not still held from opening).
    if ((in.b && !bHeld_) || (in.menu && !menuHeld_)) {
        open_ = false;
        action = Action::closed;
    }
    bHeld_ = in.b;
    menuHeld_ = in.menu;
    if (!open_)
        return action;
    // The pointers.
    for (int i = 0; i < 2; ++i) {
        const Hit h = in.aimValid[i] ? hit(in.aims[i]) : Hit{};
        if (h.row >= 0 && h.row != hits_[i].row) {
            hoverTick_ = i;
            selected_ = h.row;
        }
        hits_[i] = h;
        const bool pulled = in.triggers[i] > .7f;
        if (pulled && !triggerHeld_[i] && h.row >= 0)
            take(step(h.row, h.back ? -1 : 1, values, time));
        triggerHeld_[i] = in.triggers[i] > .3f;
    }
    // The sticks: a push moves once, held it repeats.
    const int count = static_cast<int>(rows().size());
    stickWait_ -= in.seconds;
    const bool vertical = std::abs(in.stickY) > .6f && std::abs(in.stickY) > std::abs(in.stickX);
    const bool horizontal = std::abs(in.stickX) > .6f && !vertical;
    if (!vertical && !horizontal) {
        stickWait_ = 0;
        stickHeld_ = false;
    } else if (stickWait_ <= 0) {
        // Held on, it repeats: slower at first.
        stickWait_ = stickHeld_ ? .15f : .4f;
        stickHeld_ = true;
        if (vertical) {
            const int dir = in.stickY > 0 ? -1 : 1;
            int row = selected_;
            do
                row = (row + dir + count) % count;
            while (rows()[static_cast<std::size_t>(row)].item == Item::none &&
                   !rows()[static_cast<std::size_t>(row)].lab);
            selected_ = row;
            hoverTick_ = 1;
        } else {
            take(step(selected_, in.stickX > 0 ? 1 : -1, values, time));
        }
    }
    if (in.a && !aHeld_)
        take(step(selected_, 1, values, time));
    aHeld_ = in.a;
    return action;
}

void LabMenu::draw(std::vector<Vertex>& out, const vr_settings::Values& values, int time) const {
    if (!open_)
        return;
    const Vec3 right = panel_.orientation.rotate({1, 0, 0}), up = panel_.orientation.rotate({0, 1, 0}),
               front = panel_.orientation.rotate({0, 0, 1});
    const float height = panelHeight();
    const Vec3 topLeft = panel_.position - right * (width / 2) + up * (height / 2);
    // A flat quad in the panel's plane, `lift` metres in front of it.
    const auto quad = [&](float x0, float down0, float x1, float down1, Vec3 color, float alpha, float lift) {
        const Vec3 a = topLeft + right * x0 - up * down0 + front * lift,
                   b = topLeft + right * x1 - up * down0 + front * lift,
                   c = topLeft + right * x1 - up * down1 + front * lift,
                   d = topLeft + right * x0 - up * down1 + front * lift;
        for (Vec3 p : {a, b, c, a, c, d})
            out.push_back({p, color, alpha});
    };
    quad(0, 0, width, height, {.02f, .025f, .04f}, .88f, 0);
    quad(0, 0, width, .006f, {.75f, .08f, .1f}, 1, .001f);
    TextStyle text;
    text.height = textHeight;
    TextStyle heading = text;
    heading.color = {.95f, .25f, .25f};
    TextStyle dim = text;
    dim.color = {.6f, .62f, .68f};
    const auto& all = rows();
    for (std::size_t i = 0; i < all.size(); ++i) {
        const auto& r = all[i];
        const float down = margin + static_cast<float>(i) * rowHeight;
        const Vec3 baseline = topLeft - up * (down + rowHeight * .72f) + front * .002f;
        const bool header = r.item == Item::none && !r.lab;
        const bool hovered = static_cast<int>(i) == selected_ || hits_[0].row == static_cast<int>(i) ||
                             hits_[1].row == static_cast<int>(i);
        if (hovered && !header)
            quad(.02f, down + .004f, width - .02f, down + rowHeight - .004f, {.18f, .22f, .32f}, 1, .001f);
        appendText(out, r.title, baseline + right * (header ? .04f : .07f), right, up,
                   header ? heading : text);
        const auto options = choices(r);
        if (!options.empty()) {
            const int c = chosen(r, values, time);
            const std::string label =
                options[static_cast<std::size_t>(std::clamp(c, 0, static_cast<int>(options.size()) - 1))];
            appendText(out, "<", baseline + right * (choiceX - .035f), right, up, dim);
            appendText(out, label, baseline + right * (choiceX + .01f), right, up, text);
            appendText(out, ">", baseline + right * (width - .06f), right, up, dim);
        }
    }
    // The selected row's help, in up to two lines.
    if (selected_ >= 0 && selected_ < static_cast<int>(all.size()) &&
        all[static_cast<std::size_t>(selected_)].help) {
        std::string help = all[static_cast<std::size_t>(selected_)].help, line;
        std::vector<std::string> lines;
        TextStyle small = dim;
        small.height = textHeight * .8f;
        std::size_t start = 0;
        while (start < help.size()) {
            auto end = help.find(' ', start);
            const std::string word =
                help.substr(start, end == std::string::npos ? std::string::npos : end - start);
            const std::string tryLine = line.empty() ? word : line + " " + word;
            if (textWidth(tryLine, small.height) > width - .12f && !line.empty()) {
                lines.push_back(line);
                line = word;
            } else {
                line = tryLine;
            }
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
        if (!line.empty())
            lines.push_back(line);
        for (std::size_t k = 0; k < lines.size() && k < static_cast<std::size_t>(helpLines); ++k) {
            const float down =
                margin + (static_cast<float>(all.size()) + static_cast<float>(k)) * rowHeight + .01f;
            appendText(out, lines[k], topLeft - up * (down + rowHeight * .6f) + right * .06f + front * .002f,
                       right, up, small);
        }
    }
    // Each pointer: a beam from the hand and a dot where it meets the panel.
    for (const auto& h : hits_)
        if (h.onPanel) {
            addBeam(out, h.from, h.point, .0025f, {.9f, .75f, .3f});
            const Vec3 r = right * .008f, u = up * .008f, p = h.point + front * .004f;
            for (Vec3 v : {p - r - u, p + r - u, p + r + u, p - r - u, p + r + u, p - r + u})
                out.push_back({v, {1, .85f, .3f}, 1});
        }
}
} // namespace spidy
