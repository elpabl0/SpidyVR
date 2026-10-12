#pragma once
#include "city.hpp"
#include "tracking.hpp"
#include "vertex.hpp"
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

// Draws the city (city.hpp) with OpenGL ES 3.0: its photo materials lit by
// a sky HDRI (the sun found in it, the sky's light from its blurred levels,
// reflections), the sun's shadow around the viewer, windows, shop fronts
// and road paint drawn into their faces, haze with distance, and the lab's
// overlay geometry (props, hands, webs) on top. The Quest app and the Linux
// preview (apps/city_preview.cpp) share it.
//
// Matrices are math.hpp's (row-major, column vectors); projections from
// glProjection (OpenGL's depth range).
namespace spidy::city {
// Reads an asset by its path under the assets root ("city/Bricks085/color.jpg");
// empty when it is missing.
using ReadAsset = std::function<std::vector<std::uint8_t>(const std::string&)>;
Mat4 glProjection(EyeFov fov, float nearZ = .1f, float farZ = 4000);
class Renderer {
  public:
    Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    ~Renderer();
    // With a current OpenGL ES 3.0 context. Materials or a sky that cannot be
    // read draw in flat colours; `log` gets a line for each. Throws when a
    // shader does not build.
    void initialize(const CityMesh& mesh, const ReadAsset& read,
                    const std::function<void(const std::string&)>& log);
    // Once a frame, before the views: the sun's shadow map around `viewer`.
    void shadows(Vec3 viewer);
    // One view into the bound framebuffer (cleared here) at width x height.
    // overlay: lit-by-hand geometry, an alpha below 1 blended over the rest.
    void view(const Mat4& viewProjection, Vec3 eye, unsigned width, unsigned height,
              std::span<const Vertex> overlay, float seconds);
    Vec3 sunDirection() const {
        return sun_;
    }
    // Sky brightness to the display (1: as measured).
    float exposure = 1;

  private:
    struct Textures {
        unsigned albedo{}, surface{};
    };
    void loadSky(const ReadAsset& read, const std::function<void(const std::string&)>& log);
    void drawCity(const Mat4& viewProjection, bool shadowPass);
    unsigned cityProgram_{}, shadowProgram_{}, skyProgram_{}, overlayProgram_{};
    unsigned cityArray_{}, cityBuffer_{}, cityIndices_{};
    unsigned overlayArray_{}, overlayBuffer_{};
    std::size_t overlayCapacity_{};
    unsigned skyArray_{}, sky_{}, skyLevels_{};
    unsigned shadowMap_{}, shadowFramebuffer_{};
    unsigned shadowSize_ = 2048;
    float shadowReach_ = 170; // metres from the viewer each way
    Mat4 shadowMatrix_{};
    std::vector<Textures> materials_;
    std::vector<CityMesh::Draw> draws_;
    std::vector<Bounds> chunks_;
    Vec3 sun_{0, 1, 0}, sunColor_{}, fog_{};
    float anisotropy_{};
};
} // namespace spidy::city
