#include "spidy/city.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>

namespace spidy::city {
namespace {
// metres per tile across and up (plain surfaces); floors and bays per tile (facades).
constexpr MaterialInfo materials[materialCount] = {
    {"Facade018A", 0, 0, 6, 6},          {"Facade019A", 0, 0, 6, 6},
    {"Facade020A", 0, 0, 6, 6},          {"Facade006", 0, 0, 8, 14},
    {"Facade001", 0, 0, 10, 16},         {"Facade005", 0, 0, 24, 32},
    {"Facade017", 0, 0, 12, 12},         {"Bricks085", 2.4f, 1.2f, 0, 0},
    {"Bricks101", 2, 2, 0, 0},           {"Bricks094", 1.8f, .9f, 0, 0},
    {"Bricks066", 2.4f, 2.4f, 0, 0},     {"Concrete046", 2.4f, 2.4f, 0, 0},
    {"Asphalt020L", 4.6f, 4.6f, 0, 0},   {"PavingStones136", 5, 5, 0, 0},
    {"Gravel028", 6, 6, 0, 0},           {"CorrugatedSteel009", 2, 1, 0, 0},
    {"WoodSiding008", 2, 2, 0, 0},
};
// splitmix64: the same numbers on every compiler.
struct Random {
    std::uint64_t state;
    std::uint64_t next() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    float uniform() {
        return static_cast<float>(next() >> 40) * (1.f / 16777216.f);
    }
    float range(float a, float b) {
        return a + (b - a) * uniform();
    }
    int between(int a, int b) { // a..b inclusive
        return a + static_cast<int>(next() % static_cast<std::uint64_t>(b - a + 1));
    }
    bool chance(float p) {
        return uniform() < p;
    }
    template <class T, std::size_t N> T pick(const T (&options)[N]) {
        return options[next() % N];
    }
};
enum class Kind : std::uint8_t { Rowhouse, Warehouse, Prewar, Deco, Office, Glass };
struct Tier {
    float x0, z0, x1, z1, top;
};
struct Building {
    Kind kind{};
    Material wall{}, trim{};
    float ground{}, base{}; // sidewalk top, and the top of the ground floor (storefront or not)
    float floorHeight{}, bayWidth{};
    bool storefront{}, cornice{}, parapet{};
    std::uint8_t windowWidth{}, windowHeight{}, seed{};
    std::array<std::uint8_t, 3> tint{128, 128, 128};
    int bayOffset{};
    std::vector<Tier> tiers;
};
using Style = std::array<std::uint8_t, 4>;
using Tint = std::array<std::uint8_t, 4>;
constexpr Tint neutral{128, 128, 128, 0};
bool facade(Material m) {
    return materials[static_cast<std::size_t>(m)].bays > 0;
}
std::uint8_t byte(float v) {
    return static_cast<std::uint8_t>(std::clamp(v, 0.f, 255.f) + .5f);
}
} // namespace
const MaterialInfo& info(Material m) {
    return materials[static_cast<std::size_t>(m)];
}

class Generator {
  public:
    Generator(City& city, std::uint64_t seed, const Layout& layout)
        : city_(city), layout_(layout), random_{seed * 0x2545F4914F6CDD1Dull + 1} {}
    void run();

  private:
    struct Bucket {
        std::vector<CityVertex> vertices;
        std::vector<std::uint32_t> indices;
    };
    struct Corners {
        float u0, v0, u1, v1;         // texture at o and at o + R + U
        float b0 = 0, f0 = 0, b1 = 0, f1 = 0; // window cells likewise
    };

    float density(float x, float z) const {
        const auto bump = [&](float cx, float cz, float r) {
            const float dx = x - cx, dz = z - cz;
            return std::exp(-(dx * dx + dz * dz) / (2 * r * r));
        };
        return std::max(bump(0, 0, 240), .8f * bump(-60, -470, 190));
    }
    void collider(Vec3 lo, Vec3 hi, Vec3 color) {
        city_.boxes_.push_back({lo, hi, color, nextId_++});
    }
    // A quad from o along R and U (counter-clockwise seen from the side R x U points to).
    void quad(Material m, Vec3 o, Vec3 R, Vec3 U, const Corners& c, float contact, Tint tint, Style style,
              std::array<Vec3, 4> normals = {}) {
        auto& bucket = chunks_[chunk_][static_cast<std::size_t>(m)];
        const auto first = static_cast<std::uint32_t>(bucket.vertices.size());
        const Vec3 n = normalized(cross(R, U));
        const Vec3 at[4] = {o, o + R, o + R + U, o + U};
        const float us[4] = {c.u0, c.u1, c.u1, c.u0}, vs[4] = {c.v0, c.v0, c.v1, c.v1};
        const float bs[4] = {c.b0, c.b1, c.b1, c.b0}, fs[4] = {c.f0, c.f0, c.f1, c.f1};
        for (int i = 0; i < 4; ++i) {
            CityVertex v;
            v.position = at[i];
            v.normal = length(normals[i]) > .5f ? normals[i] : n;
            v.u = us[i];
            v.v = vs[i];
            v.bay = bs[i];
            v.floor = fs[i];
            v.contact = contact;
            v.tint = tint;
            v.style = style;
            bucket.vertices.push_back(v);
        }
        for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
            bucket.indices.push_back(first + i);
    }
    void triangle(Material m, Vec3 a, Vec3 b, Vec3 c, std::array<float, 6> uv, Tint tint) {
        auto& bucket = chunks_[chunk_][static_cast<std::size_t>(m)];
        const auto first = static_cast<std::uint32_t>(bucket.vertices.size());
        const Vec3 n = normalized(cross(b - a, c - a));
        const Vec3 at[3] = {a, b, c};
        for (int i = 0; i < 3; ++i) {
            CityVertex v;
            v.position = at[i];
            v.normal = n;
            v.u = uv[i * 2];
            v.v = uv[i * 2 + 1];
            v.contact = -1000;
            v.tint = tint;
            bucket.vertices.push_back(v);
        }
        for (std::uint32_t i : {0u, 1u, 2u})
            bucket.indices.push_back(first + i);
    }
    // A quad textured by world position: across along R, up along U.
    void surface(Material m, Vec3 o, Vec3 R, Vec3 U, float contact, Tint tint = neutral, Style style = {}) {
        const auto& mi = info(m);
        const Vec3 r = normalized(R), u = normalized(U);
        const float u0 = dot(o, r) / mi.metres, v0 = dot(o, u) / mi.up;
        quad(m, o, R, U, {u0, v0, u0 + length(R) / mi.metres, v0 + length(U) / mi.up}, contact, tint,
             style);
    }
    // The faces of a box textured by world position. faces: +x -x +z -z +y -y bits.
    void box(Material m, Vec3 lo, Vec3 hi, Tint tint = neutral, unsigned faces = 0x3f, float contact = -1000,
             Style style = {}) {
        const Vec3 d = hi - lo;
        if (faces & 1)
            surface(m, {hi.x, lo.y, hi.z}, {0, 0, -d.z}, {0, d.y, 0}, contact, tint, style);
        if (faces & 2)
            surface(m, {lo.x, lo.y, lo.z}, {0, 0, d.z}, {0, d.y, 0}, contact, tint, style);
        if (faces & 4)
            surface(m, {lo.x, lo.y, hi.z}, {d.x, 0, 0}, {0, d.y, 0}, contact, tint, style);
        if (faces & 8)
            surface(m, {hi.x, lo.y, lo.z}, {-d.x, 0, 0}, {0, d.y, 0}, contact, tint, style);
        if (faces & 16)
            surface(m, {lo.x, hi.y, hi.z}, {d.x, 0, 0}, {0, 0, -d.z}, contact, tint, style);
        if (faces & 32)
            surface(m, {lo.x, lo.y, lo.z}, {d.x, 0, 0}, {0, 0, d.z}, contact, tint, style);
    }
    // An upright cylinder of `sides` faces (smooth normals), and optionally its top.
    void cylinder(Material m, Vec3 base, float radius, float height, int sides, Tint tint, bool top) {
        const auto& mi = info(m);
        const float turn = 6.2831853f / static_cast<float>(sides);
        for (int i = 0; i < sides; ++i) {
            const float a = turn * static_cast<float>(i), b = a + turn;
            const Vec3 na{std::sin(a), 0, std::cos(a)}, nb{std::sin(b), 0, std::cos(b)};
            const Vec3 pa = base + na * radius, pb = base + nb * radius;
            const float ua = a * radius / mi.metres, ub = b * radius / mi.metres;
            quad(m, pa, pb - pa, {0, height, 0},
                 {ua, base.y / mi.up, ub, (base.y + height) / mi.up}, base.y, tint, {},
                 {na, nb, nb, na});
            if (top) {
                const Vec3 c = base + Vec3{0, height, 0};
                triangle(m, c, pa + Vec3{0, height, 0}, pb + Vec3{0, height, 0},
                         {c.x / mi.metres, c.z / mi.metres, pa.x / mi.metres, pa.z / mi.metres,
                          pb.x / mi.metres, pb.z / mi.metres},
                         tint);
            }
        }
    }
    void cone(Material m, Vec3 base, float radius, float height, int sides, Tint tint) {
        const auto& mi = info(m);
        const float turn = 6.2831853f / static_cast<float>(sides);
        const Vec3 apex = base + Vec3{0, height, 0};
        for (int i = 0; i < sides; ++i) {
            const float a = turn * static_cast<float>(i), b = a + turn;
            const Vec3 pa = base + Vec3{std::sin(a), 0, std::cos(a)} * radius,
                       pb = base + Vec3{std::sin(b), 0, std::cos(b)} * radius;
            const float ua = a * radius / mi.metres, ub = b * radius / mi.metres;
            triangle(m, pa, pb, apex, {ua, 0, ub, 0, (ua + ub) / 2, height / mi.up}, tint);
        }
    }

    // The building's wall from o along R (bottom edge, seen from outside), from y0 to y1.
    void wall(const Building& b, Vec3 o, Vec3 R, float y0, float y1, Tint tint) {
        const float len = length(R);
        if (len < .05f || y1 - y0 < .05f)
            return;
        const int bays = std::max(1, static_cast<int>(std::lround(len / b.bayWidth)));
        const Vec3 at{o.x, y0, o.z}, U{0, y1 - y0, 0};
        const float f0 = (y0 - b.base) / b.floorHeight, f1 = (y1 - b.base) / b.floorHeight;
        const auto& mi = info(b.wall);
        if (facade(b.wall)) {
            const float u0 = static_cast<float>(b.bayOffset) / mi.bays;
            quad(b.wall, at, R, U,
                 {u0, f0 / mi.floors, u0 + static_cast<float>(bays) / mi.bays, f1 / mi.floors, 0, f0,
                  static_cast<float>(bays), f1},
                 b.ground, tint, {});
            return;
        }
        const Vec3 r = R * (1 / len);
        const float u0 = dot(at, r) / mi.metres;
        quad(b.wall, at, R, U,
             {u0, y0 / mi.up, u0 + len / mi.metres, y1 / mi.up, 0, f0, static_cast<float>(bays),
              f1},
             b.ground, tint,
             {static_cast<std::uint8_t>(Surface::Windows), b.windowWidth, b.windowHeight, 0});
    }
    void storefront(const Building& b, Vec3 o, Vec3 R, Tint tint) {
        const float len = length(R);
        if (len < .05f)
            return;
        const int bays = std::max(1, static_cast<int>(std::lround(len / 4.5f)));
        const Material m = facade(b.wall) ? b.trim : b.wall;
        const auto& mi = info(m);
        const Vec3 at{o.x, b.ground, o.z}, r = R * (1 / len);
        const float u0 = dot(at, r) / mi.metres;
        quad(m, at, R, {0, b.base - b.ground, 0},
             {u0, b.ground / mi.up, u0 + len / mi.metres, b.base / mi.up, 0, 0,
              static_cast<float>(bays), 1},
             b.ground, tint, {static_cast<std::uint8_t>(Surface::Storefront), 0, 0, 0});
    }
    // Four walls of [x0,x1]x[z0,z1] from y0 to y1, storefronts below b.base.
    void walls(const Building& b, float x0, float z0, float x1, float z1, float y0, float y1, Tint tint) {
        const Vec3 corners[4][2] = {{{x1, 0, z1}, {0, 0, z0 - z1}},
                                    {{x0, 0, z0}, {0, 0, z1 - z0}},
                                    {{x0, 0, z1}, {x1 - x0, 0, 0}},
                                    {{x1, 0, z0}, {x0 - x1, 0, 0}}};
        for (const auto& [o, R] : corners) {
            if (y0 < b.base - .01f) {
                if (b.storefront)
                    storefront(b, o, R, tint);
                else
                    wall(b, o, R, y0, b.base, tint);
                wall(b, o, R, b.base, y1, tint);
            } else {
                wall(b, o, R, y0, y1, tint);
            }
        }
    }
    void building(Building& b, Random& random, bool launch);
    void roofProps(const Building& b, const Tier& top, Random& random);
    void waterTower(Vec3 base, float radius, Random& random);
    void lamp(Vec3 foot, Vec3 out);
    void trafficLight(Vec3 foot, Vec3 out, bool green);
    void block(int i, int j, float bx0, float bz0, float bx1, float bz1, Random& random);
    void markings(float x0, float z0, float x1, float z1, bool avenue);
    void finish();

    City& city_;
    Layout layout_;
    Random random_;
    std::vector<std::array<Bucket, materialCount>> chunks_;
    std::uint32_t chunk_{};
    std::uint64_t nextId_ = 1;
};

namespace {
Tint shade(Tint t, float s) {
    return {byte(t[0] * s), byte(t[1] * s), byte(t[2] * s), t[3]};
}
} // namespace
void Generator::run() {
    const auto& L = layout_;
    const float pitchX = L.blockX + L.avenueRoad, pitchZ = L.blockZ + L.streetRoad;
    const float width = static_cast<float>(L.avenues) * pitchX, depth = static_cast<float>(L.streets) * pitchZ;
    const float ox = -width / 2, oz = -depth / 2;
    const auto blocks = static_cast<std::size_t>(L.avenues * L.streets);
    chunks_.resize(blocks + 1);
    // The island: streets on the ground at 0, an esplanade round it, the river below.
    constexpr float esplanade = 14, margin = 1400;
    const float ex0 = ox - esplanade, ez0 = oz - esplanade, ex1 = ox + width + esplanade,
                ez1 = oz + depth + esplanade;
    collider({ex0, -4, ez0}, {ex1, 0, ez1}, {.2f, .2f, .22f});
    collider({ex0 - margin, -8, ez0 - margin}, {ex1 + margin, -3.5f, ez1 + margin}, {.1f, .15f, .2f});
    city_.bounds_ = {{ex0, -3.5f, ez0}, {ex1, 0, ez1}};
    for (int j = 0; j < L.streets; ++j)
        for (int i = 0; i < L.avenues; ++i) {
            chunk_ = static_cast<std::uint32_t>(j * L.avenues + i);
            const float x0 = ox + static_cast<float>(i) * pitchX, z0 = oz + static_cast<float>(j) * pitchZ;
            surface(Material::Asphalt, {x0, 0, z0 + pitchZ}, {pitchX, 0, 0}, {0, 0, -pitchZ}, 0, {96, 96, 98, 0});
            const float bx0 = x0 + L.avenueRoad / 2, bz0 = z0 + L.streetRoad / 2;
            Random random{random_.next()};
            block(i, j, bx0, bz0, bx0 + L.blockX, bz0 + L.blockZ, random);
        }
    // Road paint, the avenues' segments (x = an avenue's centre) and the streets'.
    for (int j = 0; j <= L.streets; ++j)
        for (int i = 0; i <= L.avenues; ++i) {
            chunk_ = static_cast<std::uint32_t>(std::min(j, L.streets - 1) * L.avenues + std::min(i, L.avenues - 1));
            const float cx = ox + static_cast<float>(i) * pitchX, cz = oz + static_cast<float>(j) * pitchZ;
            if (j < L.streets)
                markings(cx - L.avenueRoad / 2, cz + L.streetRoad / 2, cx + L.avenueRoad / 2,
                         cz + L.streetRoad / 2 + L.blockZ, true);
            if (i < L.avenues)
                markings(cx + L.avenueRoad / 2, cz - L.streetRoad / 2, cx + L.avenueRoad / 2 + L.blockX,
                         cz + L.streetRoad / 2, false);
        }
    // The esplanade, its sea wall and railing, and the river.
    chunk_ = static_cast<std::uint32_t>(blocks);
    const float top = L.curb;
    const Bounds strips[4] = {{{ex0, 0, ez0}, {ex1, top, oz}},
                              {{ex0, 0, oz + depth}, {ex1, top, ez1}},
                              {{ex0, 0, oz}, {ox, top, oz + depth}},
                              {{ox + width, 0, oz}, {ex1, top, oz + depth}}};
    for (const auto& s : strips) {
        box(Material::Sidewalk, s.min, s.max, {104, 104, 106, 0}, 16, top);
        collider(s.min, s.max, {.5f, .5f, .48f});
    }
    const Tint rail{70, 74, 78, 0};
    const float wallBottom = City::waterLevel - 1.5f;
    surface(Material::Concrete, {ex1, wallBottom, ez1}, {0, 0, ez0 - ez1}, {0, top - wallBottom, 0}, wallBottom);
    surface(Material::Concrete, {ex0, wallBottom, ez0}, {0, 0, ez1 - ez0}, {0, top - wallBottom, 0}, wallBottom);
    surface(Material::Concrete, {ex0, wallBottom, ez1}, {ex1 - ex0, 0, 0}, {0, top - wallBottom, 0}, wallBottom);
    surface(Material::Concrete, {ex1, wallBottom, ez0}, {ex0 - ex1, 0, 0}, {0, top - wallBottom, 0}, wallBottom);
    const float railY = top + 1.05f, in = .4f;
    box(Material::Metal, {ex0 + in - .04f, railY, ez0 + in}, {ex0 + in + .04f, railY + .06f, ez1 - in}, rail);
    box(Material::Metal, {ex1 - in - .04f, railY, ez0 + in}, {ex1 - in + .04f, railY + .06f, ez1 - in}, rail);
    box(Material::Metal, {ex0 + in, railY, ez0 + in - .04f}, {ex1 - in, railY + .06f, ez0 + in + .04f}, rail);
    box(Material::Metal, {ex0 + in, railY, ez1 - in - .04f}, {ex1 - in, railY + .06f, ez1 - in + .04f}, rail);
    for (float x = ex0 + in; x <= ex1 - in; x += 6)
        for (float z : {ez0 + in, ez1 - in})
            box(Material::Metal, {x - .04f, top, z - .04f}, {x + .04f, railY, z + .04f}, rail, 15);
    for (float z = ez0 + in + 6; z <= ez1 - in - 6; z += 6)
        for (float x : {ex0 + in, ex1 - in})
            box(Material::Metal, {x - .04f, top, z - .04f}, {x + .04f, railY, z + .04f}, rail, 15);
    for (float x = ex0 + 12; x < ex1 - 6; x += 30)
        for (float z : {ez0 + 1.2f, ez1 - 1.2f})
            lamp({x, top, z}, {0, 0, z < 0 ? 1.f : -1.f});
    // The river: one sheet out to the haze.
    const Style water{static_cast<std::uint8_t>(Surface::Water), 0, 0, 0};
    const float wx0 = ex0 - margin, wz0 = ez0 - margin, wx1 = ex1 + margin, wz1 = ez1 + margin;
    const auto sheet = [&](float a0, float b0, float a1, float b1) {
        surface(Material::Concrete, {a0, City::waterLevel, b1}, {a1 - a0, 0, 0}, {0, 0, b0 - b1}, -1000, neutral,
                water);
    };
    sheet(wx0, wz0, wx1, ez0);
    sheet(wx0, ez1, wx1, wz1);
    sheet(wx0, ez0, ex0, ez1);
    sheet(ex1, ez0, wx1, ez1);
    finish();
}

void Generator::block(int i, int j, float bx0, float bz0, float bx1, float bz1, Random& random) {
    const auto& L = layout_;
    const float c = L.curb, sw = L.sidewalk;
    // The sidewalk: paving on top, concrete curbs.
    box(Material::Sidewalk, {bx0, 0, bz0}, {bx1, c, bz1}, {104, 104, 106, 0}, 16, c);
    box(Material::Concrete, {bx0, 0, bz0}, {bx1, c, bz1}, {150, 148, 144, 0}, 15, 0);
    collider({bx0, 0, bz0}, {bx1, c, bz1}, {.55f, .55f, .52f});
    // Street lamps along the curbs, traffic lights at the corners.
    const auto lamps = [&](Vec3 a, Vec3 b, Vec3 out) {
        const float len = length(b - a);
        const int n = std::max(1, static_cast<int>(len / 26));
        for (int k = 0; k < n; ++k) {
            const float t = (static_cast<float>(k) + .5f) / static_cast<float>(n);
            lamp(a + (b - a) * t, out);
        }
    };
    const float in = .7f;
    lamps({bx0 + 8, c, bz0 + in}, {bx1 - 8, c, bz0 + in}, {0, 0, -1});
    lamps({bx0 + 8, c, bz1 - in}, {bx1 - 8, c, bz1 - in}, {0, 0, 1});
    lamps({bx0 + in, c, bz0 + 8}, {bx0 + in, c, bz1 - 8}, {-1, 0, 0});
    lamps({bx1 - in, c, bz0 + 8}, {bx1 - in, c, bz1 - 8}, {1, 0, 0});
    trafficLight({bx0 + 1.2f, c, bz0 + 1.2f}, {0, 0, -1}, (i + j) % 2 == 0);
    trafficLight({bx1 - 1.2f, c, bz1 - 1.2f}, {0, 0, 1}, (i + j) % 2 == 0);
    trafficLight({bx0 + 1.2f, c, bz1 - 1.2f}, {-1, 0, 0}, (i + j) % 2 == 1);
    trafficLight({bx1 - 1.2f, c, bz0 + 1.2f}, {1, 0, 0}, (i + j) % 2 == 1);
    const bool launchBlock = i == L.avenues / 2 && j == L.streets / 2;
    const bool nearLaunch = std::abs(i - L.avenues / 2) <= 1 && std::abs(j - L.streets / 2) <= 1;
    const float ix0 = bx0 + sw, iz0 = bz0 + sw, ix1 = bx1 - sw, iz1 = bz1 - sw, mid = (iz0 + iz1) / 2;
    if (!nearLaunch && random.chance(.05f)) {
        // A plaza: paving, planters and a few lamps.
        box(Material::Sidewalk, {ix0, c, iz0}, {ix1, c + .05f, iz1}, {120, 120, 118, 0}, 16, c);
        for (int k = 0; k < 6; ++k) {
            const float px = random.range(ix0 + 4, ix1 - 8), pz = random.range(iz0 + 4, iz1 - 8);
            const float w = random.range(3, 6), d = random.range(2, 4);
            box(Material::Concrete, {px, c, pz}, {px + w, c + .8f, pz + d}, neutral, 0x3f, c);
            box(Material::RoofGravel, {px + .2f, c + .7f, pz + .2f}, {px + w - .2f, c + .81f, pz + d - .2f},
                {80, 100, 60, 0}, 16, c);
            collider({px, c, pz}, {px + w, c + .8f, pz + d}, {.6f, .6f, .6f});
        }
        lamps({ix0 + 10, c, mid}, {ix1 - 10, c, mid}, {0, 0, 1});
        return;
    }
    float x = ix0;
    bool first = true;
    while (ix1 - x > 1) {
        const float remaining = ix1 - x;
        const float d = density(x, (iz0 + iz1) / 2);
        Building north, south, whole;
        if (launchBlock && first) {
            // The launch building: a prewar roof at 43 m by the avenue, the lab's start.
            const float w = 24;
            south.kind = Kind::Prewar;
            south.tiers.push_back({x, mid, x + w, iz1, 0});
            building(south, random, true);
            north.kind = random.chance(.5f) ? Kind::Prewar : Kind::Office;
            north.tiers.push_back({x, iz0, x + w, mid, 0});
            building(north, random, false);
            city_.avenueBelow_ = {x - sw - L.avenueRoad / 2, 0, (mid + iz1) / 2};
            x += w;
            first = false;
            continue;
        }
        first = false;
        const bool tower = d > .4f && remaining >= 28 && random.chance(.5f + .4f * (d - .4f));
        float w = tower ? random.range(28, 46) : d < .25f && random.chance(.6f) ? random.range(7.5f, 13)
                                                                                 : random.range(14, 30);
        if (remaining - w < 7.5f)
            w = remaining;
        if (tower) {
            const Kind kinds[] = {Kind::Glass, Kind::Glass, Kind::Deco, Kind::Office, Kind::Deco};
            whole.kind = random.pick(kinds);
            whole.tiers.push_back({x, iz0, x + w, iz1, 0});
            building(whole, random, false);
        } else {
            for (Building* b : {&north, &south}) {
                if (w < 13) {
                    b->kind = random.chance(.88f) ? Kind::Rowhouse : Kind::Warehouse;
                } else if (d > .3f) {
                    const Kind kinds[] = {Kind::Prewar, Kind::Prewar, Kind::Office, Kind::Office, Kind::Deco,
                                          Kind::Glass, Kind::Prewar};
                    b->kind = random.pick(kinds);
                } else {
                    const Kind kinds[] = {Kind::Prewar, Kind::Prewar, Kind::Warehouse, Kind::Office,
                                          Kind::Rowhouse, Kind::Prewar, Kind::Warehouse};
                    b->kind = random.pick(kinds);
                }
                b->tiers.push_back(b == &north ? Tier{x, iz0, x + w, mid, 0} : Tier{x, mid, x + w, iz1, 0});
                building(*b, random, false);
            }
        }
        x += w;
    }
}

void Generator::building(Building& b, Random& random, bool launch) {
    const Tier lot = b.tiers.front();
    b.tiers.clear();
    const float minSide = std::min(lot.x1 - lot.x0, lot.z1 - lot.z0);
    const float d = density((lot.x0 + lot.x1) / 2, (lot.z0 + lot.z1) / 2);
    b.ground = layout_.curb;
    b.seed = static_cast<std::uint8_t>(random.next());
    const float light = random.range(.86f, 1.1f);
    for (auto& c : b.tint)
        c = byte(128 * light * random.range(.95f, 1.05f));
    int floors = 1;
    float storefronts = 0;
    switch (b.kind) {
    case Kind::Rowhouse: {
        const Material walls[] = {Material::RedBrick, Material::CleanBrick, Material::DarkBrick, Material::RedBrick};
        b.wall = random.pick(walls);
        b.trim = random.chance(.5f) ? Material::Limestone : Material::Concrete;
        b.floorHeight = 3.2f;
        b.bayWidth = random.range(2.8f, 3.4f);
        b.windowWidth = 110;
        b.windowHeight = 140;
        floors = random.between(3, 6);
        storefronts = .45f;
        b.cornice = true;
        break;
    }
    case Kind::Warehouse:
        b.wall = random.chance(.6f) ? Material::DarkBrick : Material::RedBrick;
        b.trim = Material::Concrete;
        b.floorHeight = 4.3f;
        b.bayWidth = 4.4f;
        b.windowWidth = 175;
        b.windowHeight = 150;
        floors = random.between(3, 6);
        storefronts = .2f;
        b.parapet = true;
        break;
    case Kind::Prewar: {
        const Material walls[] = {Material::BrickWindows, Material::BrickPiers, Material::RedBrick,
                                  Material::CleanBrick,   Material::Limestone,  Material::BrickWindows};
        b.wall = random.pick(walls);
        b.trim = Material::Limestone;
        b.floorHeight = 3.2f;
        b.bayWidth = facade(b.wall) ? 3.3f : random.range(3, 3.6f);
        b.windowWidth = 100;
        b.windowHeight = 148;
        floors = random.between(7, 13) + static_cast<int>(std::lround(d * 8));
        storefronts = .7f;
        b.cornice = b.parapet = true;
        break;
    }
    case Kind::Deco: {
        const Material walls[] = {Material::Limestone, Material::StonePiers, Material::Limestone, Material::DarkBrick};
        b.wall = random.pick(walls);
        b.trim = Material::Limestone;
        b.floorHeight = 3.5f;
        b.bayWidth = facade(b.wall) ? 3.3f : 2.8f;
        b.windowWidth = 88;
        b.windowHeight = 168;
        floors = random.between(18, 30) + static_cast<int>(std::lround(d * 30));
        storefronts = 1;
        b.cornice = true;
        break;
    }
    case Kind::Office: {
        const Material walls[] = {Material::OfficeGrid, Material::ConcreteGrid, Material::StonePiers,
                                  Material::OfficeGrid};
        b.wall = random.pick(walls);
        b.trim = Material::Concrete;
        b.floorHeight = 3.6f;
        b.bayWidth = b.wall == Material::OfficeGrid ? 2.06f : b.wall == Material::ConcreteGrid ? 3.4f : 3.3f;
        floors = random.between(12, 24) + static_cast<int>(std::lround(d * 24));
        storefronts = .85f;
        b.parapet = true;
        break;
    }
    case Kind::Glass:
        b.wall = random.chance(.6f) ? Material::GlassCurtain : Material::GreenGlass;
        b.trim = Material::Concrete;
        b.floorHeight = 3.9f;
        b.bayWidth = b.wall == Material::GlassCurtain ? 2.4f : 2.7f;
        floors = random.between(28, 40) + static_cast<int>(std::lround(d * 30));
        storefronts = 1;
        break;
    }
    if (launch) {
        b.wall = Material::BrickWindows;
        b.bayWidth = 3.3f;
        b.floorHeight = 3.2f;
        floors = 13;
        storefronts = 1;
        b.cornice = b.parapet = true;
    }
    b.storefront = random.chance(storefronts);
    b.base = b.ground + (b.storefront ? 4.6f : b.floorHeight);
    if (facade(b.wall))
        b.bayOffset = random.between(0, info(b.wall).bays - 1);
    floors = std::clamp(floors, 2, static_cast<int>((260 - b.base) / b.floorHeight) + 1);
    const auto floorTop = [&](int f) { return b.base + static_cast<float>(f - 1) * b.floorHeight; };
    const float top = floorTop(floors);
    const auto tier = [&](float inset, float y) {
        b.tiers.push_back({lot.x0 + inset, lot.z0 + inset, lot.x1 - inset, lot.z1 - inset, y});
    };
    if (b.kind == Kind::Prewar && !launch && floors > 11 && minSide > 18) {
        tier(0, floorTop(floors - 3));
        tier(2.5f, top);
    } else if (b.kind == Kind::Deco && minSide > 20) {
        const int steps = minSide > 30 ? 4 : 3;
        const float fractions[] = {.55f, .72f, .86f, 1};
        for (int k = 0; k < steps; ++k) {
            const int f = k + 1 == steps ? floors : static_cast<int>(std::lround(floors * fractions[k]));
            tier(std::min(3.f * static_cast<float>(k), minSide / 2 - 5), floorTop(std::max(f, k + 2)));
        }
    } else if (b.kind == Kind::Glass && minSide > 24 && random.chance(.7f)) {
        tier(0, floorTop(static_cast<int>(std::lround(floors * .7f))));
        tier(random.range(3, 6), top);
    } else {
        tier(0, top);
    }
    const Tint tint{b.tint[0], b.tint[1], b.tint[2], b.seed};
    const Tint trimTint = shade(tint, 1.05f);
    float y0 = b.ground;
    for (const auto& t : b.tiers) {
        walls(b, t.x0, t.z0, t.x1, t.z1, y0, t.top, tint);
        surface(Material::RoofGravel, {t.x0, t.top, t.z1}, {t.x1 - t.x0, 0, 0}, {0, 0, t.z0 - t.z1}, t.top,
                shade(neutral, random.range(.62f, .85f)));
        if (b.cornice) {
            // A ledge round the roof's edge, standing proud of the walls.
            const float o = .35f, w = .45f, y0c = t.top - .6f, y1c = t.top + .06f;
            box(b.trim, {t.x0 - o, y0c, t.z0 - o}, {t.x1 + o, y1c, t.z0 + w}, trimTint);
            box(b.trim, {t.x0 - o, y0c, t.z1 - w}, {t.x1 + o, y1c, t.z1 + o}, trimTint);
            box(b.trim, {t.x0 - o, y0c, t.z0 + w}, {t.x0 + w, y1c, t.z1 - w}, trimTint, 0x3e);
            box(b.trim, {t.x1 - w, y0c, t.z0 + w}, {t.x1 + o, y1c, t.z1 - w}, trimTint, 0x3d);
        }
        collider({t.x0, 0, t.z0}, {t.x1, t.top, t.z1}, {.4f, .3f, .3f});
        y0 = t.top;
    }
    // A string course over the shop windows.
    if (b.storefront) {
        const auto& t = b.tiers.front();
        box(b.trim, {t.x0 - .12f, b.base - .35f, t.z0 - .12f}, {t.x1 + .12f, b.base, t.z1 + .12f}, trimTint, 0x2f);
    }
    const Tier& roof = b.tiers.back();
    if (b.parapet) {
        const Material m = facade(b.wall) ? b.trim : b.wall;
        const float h = 1.05f, w = .3f, y = roof.top;
        const Bounds sides[] = {{{roof.x0, y, roof.z0}, {roof.x1, y + h, roof.z0 + w}},
                                {{roof.x0, y, roof.z1 - w}, {roof.x1, y + h, roof.z1}},
                                {{roof.x0, y, roof.z0 + w}, {roof.x0 + w, y + h, roof.z1 - w}},
                                {{roof.x1 - w, y, roof.z0 + w}, {roof.x1, y + h, roof.z1 - w}}};
        for (const auto& s : sides) {
            box(m, s.min, s.max, tint, 0x1f, y);
            collider(s.min, s.max, {.4f, .3f, .3f});
        }
    }
    if (launch) {
        city_.launch_ = {{(roof.x0 + roof.x1) / 2, roof.top, (roof.z0 + roof.z1) / 2}, 0};
        city_.launchRoof_ = roof.top;
    } else {
        roofProps(b, roof, random);
    }
    ++city_.buildings_;
}

void Generator::roofProps(const Building& b, const Tier& top, Random& random) {
    const float rx0 = top.x0 + 1.5f, rz0 = top.z0 + 1.5f, rx1 = top.x1 - 1.5f, rz1 = top.z1 - 1.5f, y = top.top;
    if (rx1 - rx0 < 3 || rz1 - rz0 < 3)
        return;
    std::vector<Bounds> taken;
    // A free place of w x d on the roof: its corner, or nothing after a few tries.
    const auto place = [&](float w, float d) -> std::optional<Vec3> {
        if (w > rx1 - rx0 || d > rz1 - rz0)
            return {};
        for (int attempt = 0; attempt < 8; ++attempt) {
            const Vec3 at{random.range(rx0, rx1 - w), y, random.range(rz0, rz1 - d)};
            const bool clear = std::none_of(taken.begin(), taken.end(), [&](const Bounds& t) {
                return at.x < t.max.x + .6f && at.x + w > t.min.x - .6f && at.z < t.max.z + .6f &&
                       at.z + d > t.min.z - .6f;
            });
            if (clear) {
                taken.push_back({at, at + Vec3{w, 0, d}});
                return at;
            }
        }
        return {};
    };
    const Tint metal = shade(neutral, random.range(.8f, 1.05f));
    const auto unit = [&] { // an air conditioner or fan housing
        const float w = random.range(1.5f, 3), d = random.range(1.2f, 2), h = random.range(1.1f, 1.8f);
        if (const auto at = place(w, d)) {
            box(Material::Metal, *at, *at + Vec3{w, h, d}, metal, 0x1f, y);
            collider(*at, *at + Vec3{w, h, d}, {.5f, .5f, .5f});
        }
    };
    const auto bulkhead = [&] { // the stair and elevator house
        const float w = random.range(3.5f, 5), d = random.range(3.5f, 5), h = 3.4f;
        if (const auto at = place(w, d)) {
            const Material m = facade(b.wall) ? b.trim : b.wall;
            box(m, *at, *at + Vec3{w, h, d}, {b.tint[0], b.tint[1], b.tint[2], b.seed}, 0xf, y);
            surface(Material::RoofGravel, *at + Vec3{0, h, d}, {w, 0, 0}, {0, 0, -d}, y + h);
            collider(*at, *at + Vec3{w, h, d}, {.4f, .3f, .3f});
        }
    };
    const auto tower = [&] {
        const float r = random.range(2.2f, 3);
        if (const auto at = place(2 * r + .6f, 2 * r + .6f))
            waterTower(*at + Vec3{r + .3f, 0, r + .3f}, r, random);
    };
    const float w = top.x1 - top.x0, d = top.z1 - top.z0;
    const Vec3 centre{(top.x0 + top.x1) / 2, y, (top.z0 + top.z1) / 2};
    switch (b.kind) {
    case Kind::Rowhouse:
        if (random.chance(.12f))
            tower();
        if (random.chance(.4f))
            bulkhead();
        if (random.chance(.5f))
            unit();
        break;
    case Kind::Warehouse:
        if (random.chance(.6f))
            bulkhead();
        for (int k = random.between(2, 4); k > 0; --k)
            unit();
        break;
    case Kind::Prewar:
        if (random.chance(.6f))
            tower();
        if (random.chance(.8f))
            bulkhead();
        if (random.chance(.5f))
            unit();
        break;
    case Kind::Deco:
        if (random.chance(.55f)) {
            // A stepped crown and a spire.
            float s = std::min(w, d) * .6f, base = y;
            for (int k = 0; k < 3 && s > 3; ++k) {
                const float h = 6.f - static_cast<float>(k);
                box(b.trim, centre + Vec3{-s / 2, base - y, -s / 2}, centre + Vec3{s / 2, base - y + h, s / 2},
                    {b.tint[0], b.tint[1], b.tint[2], b.seed}, 0x1f, base);
                collider(centre + Vec3{-s / 2, base - y, -s / 2}, centre + Vec3{s / 2, base - y + h, s / 2},
                         {.6f, .6f, .55f});
                base += h;
                s *= .65f;
            }
            const float spire = random.range(14, 30);
            box(Material::Metal, centre + Vec3{-.35f, base - y, -.35f}, centre + Vec3{.35f, base - y + spire, .35f},
                shade(metal, 1.2f), 0x1f);
            collider(centre + Vec3{-.35f, base - y, -.35f}, centre + Vec3{.35f, base - y + spire, .35f},
                     {.7f, .7f, .7f});
        } else {
            bulkhead();
        }
        break;
    case Kind::Office:
    case Kind::Glass: {
        // Mechanical floors in metal cladding, and maybe a mast.
        const float mw = w * random.range(.45f, .65f), md = d * random.range(.45f, .65f), h = random.range(4.5f, 6.5f);
        const Vec3 lo = centre - Vec3{mw / 2, 0, md / 2}, hi = centre + Vec3{mw / 2, h, md / 2};
        box(Material::Metal, lo, hi, metal, 0xf, y);
        surface(Material::RoofGravel, {lo.x, y + h, hi.z}, {mw, 0, 0}, {0, 0, -md}, y + h);
        collider(lo, hi, {.5f, .5f, .5f});
        taken.push_back({lo, hi});
        if (b.kind == Kind::Glass && random.chance(.45f)) {
            const float mast = random.range(18, 40);
            const Vec3 m0 = centre + Vec3{-.3f, h, -.3f}, m1 = centre + Vec3{.3f, h + mast, .3f};
            box(Material::Metal, m0, m1, shade(metal, .7f), 0x1f);
            collider(m0, m1, {.6f, .6f, .6f});
        }
        for (int k = random.between(0, 2); k > 0; --k)
            unit();
        break;
    }
    }
}

void Generator::waterTower(Vec3 base, float r, Random& random) {
    const Tint steel = shade(neutral, .55f), wood = shade(neutral, random.range(.8f, 1.1f));
    const float legs = 3.5f, tank = 4.6f;
    for (float sx : {-1.f, 1.f})
        for (float sz : {-1.f, 1.f}) {
            const Vec3 p = base + Vec3{sx * r * .7f, 0, sz * r * .7f};
            box(Material::Metal, p - Vec3{.15f, 0, .15f}, p + Vec3{.15f, legs, .15f}, steel, 0xf);
            collider(p - Vec3{.15f, 0, .15f}, p + Vec3{.15f, legs, .15f}, {.3f, .3f, .3f});
        }
    box(Material::Metal, base + Vec3{-r * 1.05f, legs - .2f, -r * 1.05f}, base + Vec3{r * 1.05f, legs, r * 1.05f},
        steel);
    const Vec3 t = base + Vec3{0, legs, 0};
    cylinder(Material::Wood, t, r, tank, 14, wood, false);
    for (float band : {.9f, 2.4f, 3.9f})
        cylinder(Material::Metal, t + Vec3{0, band, 0}, r + .03f, .14f, 14, steel, false);
    cone(Material::Metal, t + Vec3{0, tank, 0}, r + .2f, 1.9f, 14, shade(steel, 1.3f));
    collider(t - Vec3{r * .85f, 0, r * .85f}, t + Vec3{r * .85f, tank + 1, r * .85f}, {.5f, .35f, .2f});
}

void Generator::lamp(Vec3 foot, Vec3 out) {
    // Painted steel: the smooth concrete map, dark.
    const Tint pole{30, 33, 34, 0};
    box(Material::Concrete, foot - Vec3{.09f, 0, .09f}, foot + Vec3{.09f, 7.4f, .09f}, pole, 0x1f);
    const Vec3 a = foot + Vec3{0, 7.25f, 0}, tip = a + out * 1.9f;
    box(Material::Concrete, Vec3{std::min(a.x, tip.x) - .05f, a.y, std::min(a.z, tip.z) - .05f},
        Vec3{std::max(a.x, tip.x) + .05f, a.y + .1f, std::max(a.z, tip.z) + .05f}, pole, 0x1f);
    const Vec3 lo = tip + Vec3{-.32f, -.18f, -.32f}, hi = tip + Vec3{.32f, .08f, .32f};
    box(Material::Concrete, lo, hi, pole, 0x1f);
    surface(Material::Concrete, lo - Vec3{0, .01f, 0}, {hi.x - lo.x, 0, 0}, {0, 0, hi.z - lo.z}, -1000,
            {255, 226, 170, 0}, {static_cast<std::uint8_t>(Surface::Lamp), 0, 0, 0});
}

void Generator::trafficLight(Vec3 foot, Vec3 out, bool green) {
    const Tint pole{30, 33, 30, 0}, housing{70, 62, 22, 0};
    box(Material::Concrete, foot - Vec3{.1f, 0, .1f}, foot + Vec3{.1f, 5, .1f}, pole, 0x1f);
    const Vec3 lo = foot + Vec3{-.22f, 3.7f, -.22f}, hi = foot + Vec3{.22f, 5.0f, .22f};
    box(Material::Concrete, lo, hi, housing, 0x3f);
    const Vec3 up{0, 1, 0}, across = cross(up, out);
    const float lights[3] = {4.75f, 4.35f, 3.95f};
    for (int k = 0; k < 3; ++k) {
        const bool lit = green ? k == 2 : k == 0;
        const Tint colour = !lit ? Tint{30, 30, 28, 0} : green ? Tint{90, 255, 150, 0} : Tint{255, 50, 30, 0};
        const Vec3 centre = foot + Vec3{0, lights[k], 0} + out * .235f;
        surface(Material::Concrete, centre - across * .13f - up * .13f, across * .26f, up * .26f, -1000, colour,
                {static_cast<std::uint8_t>(lit ? Surface::Lamp : Surface::Plain), 0, 0, 0});
    }
}

void Generator::markings(float x0, float z0, float x1, float z1, bool avenue) {
    const Tint white{232, 232, 226, 0}, yellow{236, 184, 40, 0};
    const Style paint{static_cast<std::uint8_t>(Surface::Paint), 0, 0, 0};
    constexpr float y = .02f;
    const auto strip = [&](float ax0, float az0, float ax1, float az1, Tint t) {
        surface(Material::Asphalt, {ax0, y, az1}, {ax1 - ax0, 0, 0}, {0, 0, az0 - az1}, 0, t, paint);
    };
    if (avenue) {
        const float cx = (x0 + x1) / 2, za = z0 + 6, zb = z1 - 6;
        strip(cx - .32f, za, cx - .2f, zb, yellow);
        strip(cx + .2f, za, cx + .32f, zb, yellow);
        for (float off : {-5.f, 5.f})
            for (float z = za; z + 3 <= zb; z += 9)
                strip(cx + off - .06f, z, cx + off + .06f, z + 3, white);
        for (float bx = x0 + .8f; bx + .6f <= x1 - .8f; bx += 1.2f) {
            strip(bx, z0 + .8f, bx + .6f, z0 + 4.3f, white);
            strip(bx, z1 - 4.3f, bx + .6f, z1 - .8f, white);
        }
        strip(x0 + .5f, z0 + 4.9f, cx - .4f, z0 + 5.3f, white);
        strip(cx + .4f, z1 - 5.3f, x1 - .5f, z1 - 4.9f, white);
    } else {
        const float cz = (z0 + z1) / 2, xa = x0 + 6, xb = x1 - 6;
        for (float x = xa; x + 3 <= xb; x += 9)
            strip(x, cz - .06f, x + 3, cz + .06f, white);
        for (float bz = z0 + .8f; bz + .6f <= z1 - .8f; bz += 1.2f) {
            strip(x0 + .8f, bz, x0 + 4.3f, bz + .6f, white);
            strip(x1 - 4.3f, bz, x1 - .8f, bz + .6f, white);
        }
    }
}

void Generator::finish() {
    auto& mesh = city_.mesh_;
    constexpr float inf = std::numeric_limits<float>::infinity();
    mesh.chunks.assign(chunks_.size(), Bounds{{inf, inf, inf}, {-inf, -inf, -inf}});
    for (std::size_t c = 0; c < chunks_.size(); ++c)
        for (const auto& bucket : chunks_[c])
            for (const auto& v : bucket.vertices) {
                auto& b = mesh.chunks[c];
                b.min = {std::min(b.min.x, v.position.x), std::min(b.min.y, v.position.y),
                         std::min(b.min.z, v.position.z)};
                b.max = {std::max(b.max.x, v.position.x), std::max(b.max.y, v.position.y),
                         std::max(b.max.z, v.position.z)};
            }
    for (std::size_t m = 0; m < materialCount; ++m)
        for (std::size_t c = 0; c < chunks_.size(); ++c) {
            auto& bucket = chunks_[c][m];
            if (bucket.indices.empty())
                continue;
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            const auto first = static_cast<std::uint32_t>(mesh.indices.size());
            mesh.vertices.insert(mesh.vertices.end(), bucket.vertices.begin(), bucket.vertices.end());
            for (auto i : bucket.indices)
                mesh.indices.push_back(base + i);
            mesh.draws.push_back({static_cast<Material>(m), static_cast<std::uint32_t>(c), first,
                                  static_cast<std::uint32_t>(bucket.indices.size())});
            bucket = {};
        }
}

City::City(std::uint64_t seed, Layout layout) {
    Generator(*this, seed, layout).run();
}

std::vector<LabProp> City::props() const {
    const Vec3 wood{.55f, .36f, .18f}, paint{.72f, .22f, .08f}, steel{.12f, .32f, .17f};
    const Vec3 crate{.3f, .3f, .3f}, barrel{.3f, .4f, .3f};
    // Far above the city's own surface ids.
    std::uint64_t objects = 1'000'001, people = 2'000'001;
    std::vector<LabProp> out;
    const Vec3 roof = launch_.feet;
    out.push_back(labObject(objects++, roof + Vec3{-2.2f, 0, -3.5f}, crate, 25, .32f, wood));
    out.push_back(labObject(objects++, roof + Vec3{2, 0, -4.5f}, crate, 25, .32f, wood));
    out.push_back(labObject(objects++, roof + Vec3{3.8f, 0, -1.5f}, barrel, 40, .4f, paint));
    out.push_back(labThug(people++, roof + Vec3{-3.2f, 0, -5.8f}, 3.1416f, {.13f, .13f, .16f}));
    out.push_back(labThug(people++, roof + Vec3{3.2f, 0, -6.2f}, 3.1416f, {.3f, .1f, .1f}));
    // The avenue below: crates, barrels, a dumpster and a gang.
    const Vec3 street = avenueBelow_ + Vec3{0, 0, -8};
    out.push_back(labObject(objects++, street + Vec3{-3, 0, 4}, crate, 25, .32f, wood));
    out.push_back(labObject(objects++, street + Vec3{5, 0, -1}, crate, 25, .32f, wood));
    out.push_back(labObject(objects++, street + Vec3{2, 0, -12}, crate, 25, .32f, wood));
    out.push_back(labObject(objects++, street + Vec3{-7, 0, -6}, barrel, 40, .4f, paint));
    out.push_back(labObject(objects++, street + Vec3{7, 0, -10}, barrel, 40, .4f, paint));
    out.push_back(labObject(objects++, street + Vec3{-8, 0, -4}, {1, .6f, .55f}, 300, .6f, steel));
    const Vec3 feet[] = {{-5, 0, 2}, {4, 0, -3}, {0, 0, -9}, {-6, 0, -15}, {6, 0, -18}};
    for (unsigned i = 0; i < std::size(feet); ++i)
        out.push_back(labThug(people++, street + feet[i], 3.1416f + .3f * static_cast<float>(i % 3) - .3f,
                              i % 2 ? Vec3{.13f, .13f, .16f} : Vec3{.3f, .1f, .1f}));
    return out;
}

CityWorld::CityWorld(std::vector<Box> boxes, float cell) : boxes_(std::move(boxes)), cell_(cell) {
    constexpr float inf = std::numeric_limits<float>::infinity();
    float minX = inf, minZ = inf, maxX = -inf, maxZ = -inf;
    std::vector<bool> large(boxes_.size());
    for (std::size_t k = 0; k < boxes_.size(); ++k) {
        const auto& b = boxes_[k];
        const float cells = std::ceil((b.max.x - b.min.x) / cell_ + 1) * std::ceil((b.max.z - b.min.z) / cell_ + 1);
        large[k] = cells > 256;
        if (large[k]) {
            large_.push_back(static_cast<std::uint32_t>(k));
            continue;
        }
        minX = std::min(minX, b.min.x);
        minZ = std::min(minZ, b.min.z);
        maxX = std::max(maxX, b.max.x);
        maxZ = std::max(maxZ, b.max.z);
    }
    if (!(minX <= maxX))
        minX = maxX = minZ = maxZ = 0;
    x0_ = std::floor(minX) - 1;
    z0_ = std::floor(minZ) - 1;
    nx_ = std::max(1, static_cast<int>(std::ceil((maxX + 1 - x0_) / cell_)));
    nz_ = std::max(1, static_cast<int>(std::ceil((maxZ + 1 - z0_) / cell_)));
    const auto span = [&](const Box& b, auto&& each) {
        const int i0 = std::clamp(static_cast<int>(std::floor((b.min.x - x0_) / cell_)), 0, nx_ - 1),
                  i1 = std::clamp(static_cast<int>(std::floor((b.max.x - x0_) / cell_)), 0, nx_ - 1),
                  j0 = std::clamp(static_cast<int>(std::floor((b.min.z - z0_) / cell_)), 0, nz_ - 1),
                  j1 = std::clamp(static_cast<int>(std::floor((b.max.z - z0_) / cell_)), 0, nz_ - 1);
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i)
                each(static_cast<std::size_t>(j * nx_ + i));
    };
    cellStart_.assign(static_cast<std::size_t>(nx_ * nz_) + 1, 0);
    for (std::size_t k = 0; k < boxes_.size(); ++k)
        if (!large[k])
            span(boxes_[k], [&](std::size_t c) { ++cellStart_[c + 1]; });
    std::partial_sum(cellStart_.begin(), cellStart_.end(), cellStart_.begin());
    items_.resize(cellStart_.back());
    auto fill = cellStart_;
    for (std::size_t k = 0; k < boxes_.size(); ++k)
        if (!large[k])
            span(boxes_[k], [&](std::size_t c) { items_[fill[c]++] = static_cast<std::uint32_t>(k); });
    for (const auto& b : boxes_)
        ids_.push_back(b.id);
    std::sort(ids_.begin(), ids_.end());
    seen_.assign(boxes_.size(), 0);
}
std::uint32_t CityWorld::nextQuery() const {
    if (++query_ == 0) {
        std::fill(seen_.begin(), seen_.end(), 0);
        query_ = 1;
    }
    return query_;
}
template <class Visit> void CityWorld::boxesIn(Vec3 lo, Vec3 hi, Visit&& visit) const {
    const auto q = nextQuery();
    for (auto k : large_)
        visit(k);
    const float gx1 = x0_ + static_cast<float>(nx_) * cell_, gz1 = z0_ + static_cast<float>(nz_) * cell_;
    if (hi.x < x0_ || hi.z < z0_ || lo.x > gx1 || lo.z > gz1)
        return;
    const int i0 = std::clamp(static_cast<int>(std::floor((lo.x - x0_) / cell_)), 0, nx_ - 1),
              i1 = std::clamp(static_cast<int>(std::floor((hi.x - x0_) / cell_)), 0, nx_ - 1),
              j0 = std::clamp(static_cast<int>(std::floor((lo.z - z0_) / cell_)), 0, nz_ - 1),
              j1 = std::clamp(static_cast<int>(std::floor((hi.z - z0_) / cell_)), 0, nz_ - 1);
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) {
            const auto c = static_cast<std::size_t>(j * nx_ + i);
            for (auto n = cellStart_[c]; n < cellStart_[c + 1]; ++n)
                if (const auto k = items_[n]; seen_[k] != q) {
                    seen_[k] = q;
                    visit(k);
                }
        }
}
std::optional<RayHit> CityWorld::raycast(Vec3 o, Vec3 d, float range) const {
    const auto q = nextQuery();
    std::optional<RayHit> out;
    const auto test = [&](std::uint32_t k) {
        const Box& b = boxes_[k];
        if (auto hit = intersectBox(o, d, b.min, b.max, range)) {
            range = hit->t;
            out = RayHit{o + d * range, hit->normal, b.id, true};
        }
    };
    for (auto k : large_)
        test(k);
    // Walk the cells under the ray, nearest first, until a hit is nearer than the next cell.
    float t0 = 0, t1 = range;
    const auto clip = [&](float p, float v, float lo, float hi) {
        if (std::abs(v) < 1e-12f)
            return p >= lo && p <= hi;
        float a = (lo - p) / v, b = (hi - p) / v;
        if (a > b)
            std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        return t0 <= t1;
    };
    const float gx1 = x0_ + static_cast<float>(nx_) * cell_, gz1 = z0_ + static_cast<float>(nz_) * cell_;
    if (!clip(o.x, d.x, x0_, gx1) || !clip(o.z, d.z, z0_, gz1))
        return out;
    const Vec3 p = o + d * t0;
    int ix = std::clamp(static_cast<int>(std::floor((p.x - x0_) / cell_)), 0, nx_ - 1);
    int iz = std::clamp(static_cast<int>(std::floor((p.z - z0_) / cell_)), 0, nz_ - 1);
    constexpr float inf = std::numeric_limits<float>::infinity();
    const int sx = d.x > 0 ? 1 : -1, sz = d.z > 0 ? 1 : -1;
    const bool flatX = std::abs(d.x) < 1e-12f, flatZ = std::abs(d.z) < 1e-12f;
    float tx = flatX ? inf : (x0_ + static_cast<float>(ix + (sx > 0)) * cell_ - o.x) / d.x;
    float tz = flatZ ? inf : (z0_ + static_cast<float>(iz + (sz > 0)) * cell_ - o.z) / d.z;
    const float dx = flatX ? inf : cell_ / std::abs(d.x), dz = flatZ ? inf : cell_ / std::abs(d.z);
    for (;;) {
        const auto c = static_cast<std::size_t>(iz * nx_ + ix);
        for (auto n = cellStart_[c]; n < cellStart_[c + 1]; ++n)
            if (const auto k = items_[n]; seen_[k] != q) {
                seen_[k] = q;
                test(k);
            }
        const float next = std::min(tx, tz);
        if (next >= range || next > t1)
            break;
        if (tx < tz) {
            ix += sx;
            tx += dx;
            if (ix < 0 || ix >= nx_)
                break;
        } else {
            iz += sz;
            tz += dz;
            if (iz < 0 || iz >= nz_)
                break;
        }
    }
    return out;
}
MoveResult CityWorld::sweep(Vec3 from, Vec3 to, float radius) const {
    const Vec3 r{radius + .01f, radius + .01f, radius + .01f};
    const Vec3 lo{std::min(from.x, to.x), std::min(from.y, to.y), std::min(from.z, to.z)},
        hi{std::max(from.x, to.x), std::max(from.y, to.y), std::max(from.z, to.z)};
    candidates_.clear();
    boxesIn(lo - r, hi + r, [&](std::uint32_t k) { candidates_.push_back(k); });
    return sweepBoxes(from, to, radius, [&](auto&& visit) {
        for (auto k : candidates_)
            visit(boxes_[k]);
    });
}
bool CityWorld::exists(std::uint64_t id) const {
    return std::binary_search(ids_.begin(), ids_.end(), id);
}
} // namespace spidy::city
