#include "spidy/lab_world.hpp"
#include <limits>
namespace spidy {
LabWorld::LabWorld() {
    boxes_.push_back({{-250, -3, -450}, {250, 0, 150}, {.08f, .11f, .16f}, 1});
    std::uint64_t id = 2;
    for (int row = 0; row < 7; ++row)
        for (int side : {-1, 1}) {
            const float x = static_cast<float>(side) * 27, z = -static_cast<float>(row) * 48;
            const float h = 25 + static_cast<float>((row * 13 + (side + 1) * 7) % 40);
            boxes_.push_back(
                {{x - 10, 0, z - 17}, {x + 10, h, z + 17}, {.15f + row * .025f, .22f, .32f}, id++});
        }
    // Launch rooftop centered under the initial player.
    boxes_.push_back({{-6, 0, 12}, {6, 18, 26}, {.28f, .16f, .19f}, id});
}
std::optional<RayHit> LabWorld::raycast(Vec3 o, Vec3 d, float range) const {
    std::optional<RayHit> out;
    for (const auto& b : boxes_)
        if (auto hit = intersectBox(o, d, b.min, b.max, range)) {
            range = hit->t;
            out = RayHit{o + d * range, hit->normal, b.id, true};
        }
    return out;
}
MoveResult LabWorld::sweep(Vec3 from, Vec3 to, float radius) const {
    return sweepBoxes(from, to, radius, [&](auto&& visit) {
        for (const auto& b : boxes_)
            visit(b);
    });
}
bool LabWorld::exists(std::uint64_t id) const {
    return std::any_of(boxes_.begin(), boxes_.end(), [id](const Box& b) { return b.id == id; });
}
} // namespace spidy
