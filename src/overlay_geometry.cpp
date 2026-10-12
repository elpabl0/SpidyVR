#include "spidy/overlay_geometry.hpp"
#include <algorithm>
#include <cmath>
namespace spidy {
namespace {
void triangle(std::vector<Vertex>& v, Vec3 a, Vec3 b, Vec3 c, Vec3 color) {
    v.push_back({a, color});
    v.push_back({b, color});
    v.push_back({c, color});
}
} // namespace
void addBox(std::vector<Vertex>& out, Vec3 lo, Vec3 hi, Vec3 color) {
    Vec3 p[] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
    const int faces[][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7},
                            {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0}};
    for (int f = 0; f < 6; ++f) {
        const auto& v = faces[f];
        const auto c = color * (.65f + .07f * f);
        triangle(out, p[v[0]], p[v[1]], p[v[2]], c);
        triangle(out, p[v[0]], p[v[2]], p[v[3]], c);
    }
}
void addBeam(std::vector<Vertex>& out, Vec3 a, Vec3 b, float r, Vec3 color) {
    const Vec3 d = normalized(b - a);
    if (length(d) < .1f)
        return;
    const Vec3 u = normalized(cross(d, std::abs(d.y) > .95f ? Vec3{1, 0, 0} : Vec3{0, 1, 0})) * r,
               v = normalized(cross(d, u)) * r;
    Vec3 ring[] = {u + v, -u + v, -u - v, u - v};
    for (int i = 0; i < 4; ++i) {
        const auto j = (i + 1) % 4;
        triangle(out, a + ring[i], b + ring[i], b + ring[j], color);
        triangle(out, a + ring[i], b + ring[j], a + ring[j], color);
    }
}
void addTrackedHand(std::vector<Vertex>& out, Pose grip, unsigned hand, float squeeze, bool webGesture) {
    const size_t begin = out.size();
    const Vec3 red{.65f, .025f, .035f}, seam{.025f, .015f, .02f}, cuff{.035f, .06f, .24f};
    addBox(out, {-.041f, -.02f, -.05f}, {.041f, .02f, .042f}, red);
    addBox(out, {-.035f, -.024f, .035f}, {.035f, .024f, .09f}, cuff);
    for (int finger = 0; finger < 4; ++finger) {
        const float x = -.031f + finger * .021f;
        const float bend = webGesture && (finger == 0 || finger == 3) ? 0.f : std::clamp(squeeze, 0.f, 1.f);
        const float size = (finger == 3 ? .017f : .022f);
        Vec3 from{x, 0, -.049f};
        for (int joint = 0; joint < 3; ++joint) {
            const float angle = bend * (.45f + joint * .8f);
            const Vec3 to = from + Vec3{0, -std::sin(angle) * size, -std::cos(angle) * size};
            addBeam(out, from, to, .0085f, red);
            addBox(out, to - Vec3{.0087f, .0015f, .0015f}, to + Vec3{.0087f, .0015f, .0015f}, seam);
            from = to;
        }
    }
    const float side = hand ? -1.f : 1.f;
    addBeam(out, {side * .035f, -.005f, .025f}, {side * .065f, -.012f, -.007f}, .011f, red);
    addBeam(out, {side * .065f, -.012f, -.007f}, {side * .061f, -.018f, -.036f}, .009f, red);
    for (int line = -1; line <= 1; ++line)
        addBeam(out, {line * .022f, .0205f, .035f}, {line * .022f, .0205f, -.045f}, .0009f, seam);
    for (size_t i = begin; i < out.size(); ++i)
        out[i].position = grip.position + grip.orientation.rotate(out[i].position);
}
} // namespace spidy
