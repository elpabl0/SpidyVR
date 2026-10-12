#include "spidy/city.hpp"
#include "spidy/lab_props.hpp"
#include "spidy/swing.hpp"
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
// Deterministic numbers for the tests' rays.
struct Lcg {
    std::uint64_t s = 12345;
    float next() {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<float>(s >> 40) / 16777216.f;
    }
    float range(float a, float b) {
        return a + (b - a) * next();
    }
};
const City& theCity() {
    static const City c;
    return c;
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
    test("the same seed builds the same city", [] {
        const City again;
        check(again.boxes().size() == theCity().boxes().size(), "box counts differ");
        check(again.mesh().vertices.size() == theCity().mesh().vertices.size(), "vertex counts differ");
        for (std::size_t i = 0; i < again.boxes().size(); ++i)
            check(again.boxes()[i].max.y == theCity().boxes()[i].max.y, "a building's height differs");
        const City other(7);
        check(other.mesh().vertices.size() != theCity().mesh().vertices.size(), "another seed built the same city");
    });
    test("the city has variety: hundreds of buildings, towers and walk-ups", [] {
        check(theCity().buildings() > 500, "only " + std::to_string(theCity().buildings()) + " buildings");
        float tallest = 0;
        int low = 0, towers = 0;
        for (const auto& b : theCity().boxes()) {
            if (b.max.x - b.min.x > 1000)
                continue;
            tallest = std::max(tallest, b.max.y);
            towers += b.max.y > 120;
            low += b.max.y > 8 && b.max.y < 25;
        }
        check(tallest > 150 && tallest <= 310, "tallest " + std::to_string(tallest)); // masts and spires included
        check(towers > 20 && low > 50, "towers " + std::to_string(towers) + ", low " + std::to_string(low));
        bool used[materialCount]{};
        for (const auto& d : theCity().mesh().draws)
            used[static_cast<std::size_t>(d.material)] = true;
        for (std::size_t m = 0; m < materialCount; ++m)
            check(used[m], std::string("material ") + info(static_cast<Material>(m)).folder + " unused");
    });
    test("the mesh is whole: indices in range, draws cover them, finite normals", [] {
        const auto& m = theCity().mesh();
        std::size_t covered = 0;
        for (const auto& d : m.draws) {
            check(d.first == covered, "draws leave a gap");
            covered += d.count;
            check(d.chunk < m.chunks.size(), "a draw's chunk is missing");
        }
        check(covered == m.indices.size(), "draws do not cover the indices");
        for (auto i : m.indices)
            check(i < m.vertices.size(), "an index past the vertices");
        for (const auto& v : m.vertices) {
            check(finite(v.position) && std::abs(length(v.normal) - 1) < .01f, "a bad vertex");
            check(std::isfinite(v.u) && std::isfinite(v.v), "bad texture coordinates");
        }
        for (const auto& d : m.draws) {
            const auto& b = m.chunks[d.chunk];
            for (auto n = d.first; n < d.first + d.count; ++n) {
                const auto p = m.vertices[m.indices[n]].position;
                check(p.x >= b.min.x && p.y >= b.min.y && p.z >= b.min.z && p.x <= b.max.x &&
                          p.y <= b.max.y && p.z <= b.max.z,
                      "a vertex outside its chunk's bounds");
            }
        }
    });
    test("faces point out: a wall's normal faces away from its building", [] {
        // Every front face of a box is wound counter-clockwise seen from outside.
        const auto& m = theCity().mesh();
        for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
            const auto& a = m.vertices[m.indices[t]];
            const auto& b = m.vertices[m.indices[t + 1]];
            const auto& c = m.vertices[m.indices[t + 2]];
            const Vec3 n = cross(b.position - a.position, c.position - a.position);
            if (length(n) < 1e-6f)
                continue;
            check(dot(n, a.normal + b.normal + c.normal) > 0, "a triangle wound against its normal");
        }
    });
    test("the grid answers rays like testing every box", [] {
        const CityWorld grid(theCity().boxes());
        const LabWorld all(theCity().boxes());
        Lcg r;
        int hits = 0;
        for (int i = 0; i < 3000; ++i) {
            const Vec3 o{r.range(-650, 650), r.range(.5f, 220), r.range(-650, 650)};
            Vec3 d{r.range(-1, 1), r.range(-1, .6f), r.range(-1, 1)};
            if (length(d) < .1f)
                continue;
            d = normalized(d);
            const float range = i % 3 ? 100 : 1500;
            const auto a = grid.raycast(o, d, range), b = all.raycast(o, d, range);
            check(a.has_value() == b.has_value(), "one found a hit, the other not, ray " + std::to_string(i));
            if (a) {
                ++hits;
                check(length(a->point - b->point) < 1e-3f, "hits differ, ray " + std::to_string(i));
            }
        }
        check(hits > 1000, "too few hits to mean anything");
        // Straight down, and along the axes.
        for (Vec3 d : {Vec3{0, -1, 0}, Vec3{1, 0, 0}, Vec3{0, 0, -1}, Vec3{-1, 0, 0}}) {
            const Vec3 o{3, 60, 5};
            const auto a = grid.raycast(o, d, 400), b = all.raycast(o, d, 400);
            check(a.has_value() == b.has_value() && (!a || length(a->point - b->point) < 1e-3f), "axis ray");
        }
    });
    test("the grid sweeps like testing every box", [] {
        const CityWorld grid(theCity().boxes());
        const LabWorld all(theCity().boxes());
        Lcg r;
        for (int i = 0; i < 3000; ++i) {
            const Vec3 from{r.range(-600, 600), r.range(.4f, 120), r.range(-600, 600)};
            const Vec3 to = from + Vec3{r.range(-2, 2), r.range(-2, 2), r.range(-2, 2)};
            const auto a = grid.sweep(from, to, .35f), b = all.sweep(from, to, .35f);
            check(a.hit == b.hit && length(a.position - b.position) < 1e-3f,
                  "sweeps differ, case " + std::to_string(i));
        }
    });
    test("every surface a ray hits exists", [] {
        const CityWorld grid(theCity().boxes());
        check(!grid.exists(0) && !grid.exists(999'999'999), "an unknown id exists");
        const auto hit = grid.raycast({0, 300, 0}, {0, -1, 0}, 400);
        check(hit && grid.exists(hit->surface), "the ground's id");
    });
    test("the launch roof is open and holds its crates and thugs", [] {
        const CityWorld world(theCity().boxes());
        const auto launch = theCity().launch();
        check(launch.feet.y > 35 && launch.feet.y < 50, "launch roof at " + std::to_string(launch.feet.y));
        // Standing on it: a sphere set just above stays there.
        const Vec3 body = launch.feet + Vec3{0, .3501f, 0};
        const auto fall = world.sweep(body, body - Vec3{0, 1, 0}, .35f);
        check(fall.hit && std::abs(fall.position.y - body.y) < .01f, "the roof does not hold the player");
        // Open sky above, and room to walk.
        check(!world.raycast(launch.feet + Vec3{0, 2, 0}, {0, 1, 0}, 200), "something over the start");
        for (const auto& p : theCity().props()) {
            const auto down = world.raycast(p.position, {0, -1, 0}, 3);
            check(down.has_value() && down->point.y < p.position.y, "a prop floats or is buried");
        }
        // Buildings tall enough to swing from within a web's reach.
        int anchors = 0;
        for (int k = 0; k < 16; ++k) {
            const float a = static_cast<float>(k) * .3927f;
            const Vec3 d = normalized(Vec3{std::sin(a), .5f, -std::cos(a)});
            anchors += world.raycast(launch.feet + Vec3{0, 1.6f, 0}, d, 100).has_value();
        }
        check(anchors >= 8, "only " + std::to_string(anchors) + " of 16 directions reach a wall");
    });
    test("a swing in the city keeps hold and moves the player", [] {
        const CityWorld world(theCity().boxes());
        Swing swing;
        const auto launch = theCity().launch();
        swing.reset({launch.feet + Vec3{0, .3501f, 0}, {}, true});
        Input in;
        in.hands[1].tracked = true;
        in.hands[1].grip = 1;
        in.hands[1].aim = {launch.feet + Vec3{.3f, 1.4f, -.3f}, arc({0, 0, -1}, normalized(Vec3{.2f, .6f, -1}))};
        in.hands[1].gripRelativeToHead = {.3f, -.3f, -.3f};
        // Walk off the roof's edge holding a web.
        const Vec3 start = swing.body().position;
        for (int i = 0; i < 400; ++i) {
            in.move = {0, 0, -1};
            swing.update(1.f / 90, in, world);
        }
        check(length(swing.body().position - start) > 3, "the player did not move");
        check(finite(swing.body().position) && swing.body().position.y > -1, "the player fell through");
    });
    std::cout << total - failed << '/' << total << " tests passed\n";
    return failed ? 1 : 0;
}
