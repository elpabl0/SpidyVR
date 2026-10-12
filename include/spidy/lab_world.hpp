#pragma once
#include "swing.hpp"
#include <algorithm>
#include <limits>
#include <utility>
namespace spidy {
struct Box {
    Vec3 min, max, color;
    std::uint64_t id;
};
// Where a ray from p along d (not normalized: t is in its lengths) enters the
// box [lo, hi] within maxT, and the face's outward normal; nothing when p is
// inside the box.
struct BoxIntersection {
    float t;
    Vec3 normal;
};
inline std::optional<BoxIntersection> intersectBox(Vec3 p, Vec3 d, Vec3 lo, Vec3 hi, float maxT) {
    float enter = -std::numeric_limits<float>::infinity(), exit = maxT;
    Vec3 normal{};
    const float ps[] = {p.x, p.y, p.z}, ds[] = {d.x, d.y, d.z}, ls[] = {lo.x, lo.y, lo.z},
                hs[] = {hi.x, hi.y, hi.z};
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(ds[axis]) < 1e-7f) {
            if (ps[axis] < ls[axis] || ps[axis] > hs[axis])
                return {};
            continue;
        }
        float a = (ls[axis] - ps[axis]) / ds[axis], b = (hs[axis] - ps[axis]) / ds[axis];
        float sign = -1;
        if (a > b) {
            std::swap(a, b);
            sign = 1;
        }
        if (a > enter) {
            enter = a;
            normal = {};
            if (axis == 0)
                normal.x = sign;
            else if (axis == 1)
                normal.y = sign;
            else
                normal.z = sign;
        }
        exit = std::min(exit, b);
        if (enter > exit)
            return {};
    }
    if (enter < 0 || enter > maxT || exit < 0)
        return {};
    return BoxIntersection{enter, normal};
}
// A sphere of `radius` swept from `from` to `to` against boxes, sliding along
// what it meets (up to four contacts). forEach(visit) calls visit(box) for
// every box that might be in the way.
template <class ForEach> MoveResult sweepBoxes(Vec3 from, Vec3 to, float radius, ForEach&& forEach) {
    Vec3 p = from, remaining = to - from, lastNormal{};
    bool collided = false;
    const Vec3 r{radius, radius, radius};
    for (int pass = 0; pass < 4 && length(remaining) > 1e-7f; ++pass) {
        std::optional<BoxIntersection> nearest;
        float fraction = 1;
        forEach([&](const Box& b) {
            if (auto hit = intersectBox(p, remaining, b.min - r, b.max + r, fraction)) {
                fraction = hit->t;
                nearest = hit;
            }
        });
        if (!nearest) {
            p += remaining;
            break;
        }
        collided = true;
        lastNormal = nearest->normal;
        p += remaining * fraction + lastNormal * .0001f;
        remaining = remaining * (1 - fraction);
        const float inward = dot(remaining, lastNormal);
        if (inward < 0)
            remaining -= lastNormal * inward;
    }
    return {p, lastNormal, collided};
}
class LabWorld final : public World {
  public:
    LabWorld();
    explicit LabWorld(std::vector<Box> boxes) : boxes_(std::move(boxes)) {}
    std::optional<RayHit> raycast(Vec3, Vec3, float) const override;
    MoveResult sweep(Vec3, Vec3, float) const override;
    bool exists(std::uint64_t) const override;
    const std::vector<Box>& boxes() const {
        return boxes_;
    }

  private:
    std::vector<Box> boxes_;
};
} // namespace spidy
