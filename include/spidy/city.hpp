#pragma once
#include "lab_props.hpp"
#include "lab_world.hpp"
#include <array>
#include <cstdint>
#include <vector>

// A procedural Manhattan-like city for the swinging lab: a grid of avenues
// (along z, north is -z) and cross streets, blocks of row houses, prewar
// apartments with water towers, art deco setback towers, office slabs and
// glass towers, taller towards downtown around the origin. The same seed
// always gives the same city on every platform (its own random numbers, no
// std distributions).
//
// It makes three things: the collision boxes the swing and the props move
// against (CityWorld queries them through a grid), a textured mesh for a
// renderer (CityMesh: photo materials by name, chunked per block for
// culling), and the launch rooftop with its crates and thugs.
namespace spidy::city {
// Photo materials, each a folder of maps under the assets' city/ directory
// (quest/tools/fetch_assets.py says where each comes from).
enum class Material : std::uint8_t {
    BrickWindows, // brick facade with its windows (ambientCG Facade018A)
    StonePiers,   // dark stone piers and glass bands (Facade019A)
    BrickPiers,   // brick piers and window columns (Facade020A)
    OfficeGrid,   // white mullion office grid (Facade006)
    GlassCurtain, // blue-grey curtain wall (Facade001)
    GreenGlass,   // green glass panels (Facade005)
    ConcreteGrid, // concrete columns and lit offices (Facade017)
    RedBrick,     // plain walls: the renderer adds the windows (Bricks085)
    CleanBrick,   // (Bricks101)
    DarkBrick,    // (Bricks094)
    Limestone,    // (Bricks066)
    Concrete,     // trim, cornices, curbs (Concrete046)
    Asphalt,      // roads, and their paint (Asphalt020L)
    Sidewalk,     // (PavingStones136)
    RoofGravel,   // (Gravel028)
    Metal,        // rooftop machinery, poles (CorrugatedSteel009)
    Wood,         // water tanks (WoodSiding008)
    Count
};
constexpr std::size_t materialCount = static_cast<std::size_t>(Material::Count);
struct MaterialInfo {
    const char* folder; // under city/: color.jpg, normal.jpg, roughness.jpg, and metalness.jpg when it has one
    // A plain surface's texture repeats every `metres` across and `up` up
    // (or along -z on the ground). A facade's texture holds `floors` storeys
    // and `bays` window bays (0 for a plain surface).
    float metres, up;
    std::uint8_t floors, bays;
};
const MaterialInfo& info(Material);

// How the renderer shades a face (CityVertex::style[0]).
enum class Surface : std::uint8_t {
    Plain,      // the material as it is
    Windows,    // a plain wall with a window in each bay of each floor
    Storefront, // a ground floor of shop windows under a sign band
    Paint,      // road paint: tint is its colour, the material its wear
    Lamp,       // a lamp's glowing glass: tint is its colour
    Water,      // the river round the island: waves and the sky in them
};
struct CityVertex {
    Vec3 position{}, normal{};
    float u{}, v{};        // texture coordinates
    float bay{}, floor{};  // window grid cell coordinates (Windows, Storefront)
    float contact{};       // height of the ground or roof this face stands on
    std::array<std::uint8_t, 4> tint{128, 128, 128, 0}; // rgb albedo scale (128: 1), a seed
    std::array<std::uint8_t, 4> style{};                // Surface, window width and height (of 255), spare
};
struct Bounds {
    Vec3 min{}, max{};
};
struct CityMesh {
    struct Draw {
        Material material{};
        std::uint32_t chunk{}, first{}, count{}; // indices[first, first + count) in chunk's bounds
    };
    std::vector<CityVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Bounds> chunks;
    std::vector<Draw> draws; // by material, then chunk
};
// Where the lab starts: the player's feet and facing (yaw, radians from -z,
// as Rig::yaw), on the launch rooftop.
struct Launch {
    Vec3 feet{};
    float yaw{};
};
struct Layout {
    int avenues = 9, streets = 14;           // blocks across (x) and along (z)
    float blockX = 110, blockZ = 70;         // a block with its sidewalks, metres
    float avenueRoad = 20, streetRoad = 12;  // road between blocks, curb to curb
    float sidewalk = 4.5f, curb = .15f;      // sidewalk width, and its height over the road
};
class City {
  public:
    explicit City(std::uint64_t seed = 1, Layout layout = {});
    const std::vector<Box>& boxes() const {
        return boxes_;
    }
    const CityMesh& mesh() const {
        return mesh_;
    }
    Launch launch() const {
        return launch_;
    }
    // Crates, barrels and thugs on the launch rooftop and in the avenue below.
    // The river round the island is this far below the streets: a body
    // lower than waterLevel has fallen in.
    std::vector<LabProp> props() const;
    static constexpr float waterLevel = -1.8f;
    Bounds bounds() const {
        return bounds_;
    }
    std::size_t buildings() const {
        return buildings_;
    }

  private:
    std::vector<Box> boxes_;
    CityMesh mesh_;
    Launch launch_{};
    Bounds bounds_{};
    Vec3 avenueBelow_{}; // the road beside the launch building, at its curb
    float launchRoof_{};
    std::size_t buildings_{};
    friend class Generator;
};

// The city's boxes as a World, through a uniform grid on the ground plane:
// a ray walks the cells it crosses, a sweep tests the cells its path covers.
// Same answers as LabWorld over the same boxes, without testing them all.
class CityWorld final : public World {
  public:
    explicit CityWorld(std::vector<Box> boxes, float cell = 24);
    std::optional<RayHit> raycast(Vec3 origin, Vec3 direction, float distance) const override;
    MoveResult sweep(Vec3 from, Vec3 to, float radius) const override;
    bool exists(std::uint64_t id) const override;
    const std::vector<Box>& boxes() const {
        return boxes_;
    }

  private:
    // Queries keep scratch state: one thread at a time.
    std::uint32_t nextQuery() const;
    template <class Visit> void boxesIn(Vec3 lo, Vec3 hi, Visit&& visit) const;
    std::vector<Box> boxes_;
    std::vector<std::uint32_t> large_;            // boxes over too many cells: always tested
    std::vector<std::uint32_t> cellStart_, items_; // cell c's boxes: items_[cellStart_[c] .. cellStart_[c + 1])
    std::vector<std::uint64_t> ids_;              // sorted
    mutable std::vector<std::uint32_t> seen_;     // per box: the query that tested it last
    mutable std::uint32_t query_{};
    mutable std::vector<std::uint32_t> candidates_;
    float cell_{}, x0_{}, z0_{};
    int nx_{}, nz_{};
};
} // namespace spidy::city
