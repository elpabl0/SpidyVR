#pragma once
#include "city.hpp"
#include "tracking.hpp"
#include "vertex.hpp"
#include <array>
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
// The sky and the lights: a clear day, a low golden sun, or night under the
// moon with the street lamps on and many windows lit.
enum class TimeOfDay : std::uint8_t { Day, Sunset, Night };
class Renderer {
  public:
    Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    ~Renderer();
    // With a current OpenGL ES 3.0 context. Materials or skies that cannot be
    // read draw in flat colours; `log` gets a line for each. Throws when a
    // shader does not build.
    void initialize(const CityMesh& mesh, const ReadAsset& read,
                    const std::function<void(const std::string&)>& log);
    void timeOfDay(TimeOfDay time);
    TimeOfDay timeOfDay() const {
        return time_;
    }
    // Once a frame, before the views: the sun's (or moon's) shadow map around `viewer`.
    void shadows(Vec3 viewer);
    // The city's reflection, seen from `at`: `faces` more of its six cube
    // faces drawn (one a frame keeps it cheap; six for a still picture).
    // Without it (or where the GPU cannot draw floating point colour)
    // reflections see only the sky.
    void reflections(Vec3 at, unsigned faces = 1);
    // One view into the bound framebuffer (cleared here) at width x height.
    // overlay: lit-by-hand geometry, an alpha below 1 blended over the rest.
    void view(const Mat4& viewProjection, Vec3 eye, unsigned width, unsigned height,
              std::span<const Vertex> overlay, float seconds);
    // The reflection probe drawn as the sky, to check it against a view.
    bool showProbe = false;
    Vec3 sunDirection() const {
        return skies_[static_cast<std::size_t>(time_)].sun;
    }
    // Brightness to the display on top of the time of day's own (1: as tuned).
    float brightness = 1;

  private:
    struct Textures {
        unsigned albedo{}, surface{};
        bool facade{};
    };
    struct Sky {
        unsigned texture{}, levels{};
        Vec3 sun{0, 1, 0}, colour{}; // towards the sun (or moon), and its light
        float mean = 1;              // the upper half's mean luminance, as stored
    };
    Sky loadSky(const std::string& path, const ReadAsset& read,
                const std::function<void(const std::string&)>& log);
    void lampMap(const std::vector<Vec3>& lamps);
    void cityUniforms(const Mat4& viewProjection, Vec3 eye, float seconds, bool capture);
    void drawCity(const Mat4& viewProjection, bool shadowPass);
    void drawSky(const Mat4& viewProjection, Vec3 eye, bool capture);
    unsigned cityProgram_{}, shadowProgram_{}, skyProgram_{}, overlayProgram_{};
    unsigned cityArray_{}, cityBuffer_{}, cityIndices_{};
    unsigned overlayArray_{}, overlayBuffer_{};
    std::size_t overlayCapacity_{};
    unsigned skyArray_{};
    std::array<Sky, 3> skies_{};
    TimeOfDay time_ = TimeOfDay::Day;
    float skyScale_ = 1, exposure_ = 1, lampLevel_{}, windowsLit_{}, light_ = 1;
    unsigned lamps_{};
    Vec3 lampOrigin_{}, lampSize_{1, 1, 1};
    unsigned shadowMap_{}, shadowFramebuffer_{};
    unsigned shadowSize_ = 2048;
    float shadowReach_ = 170; // metres from the viewer each way
    Mat4 shadowMatrix_{};
    unsigned probe_{}, probeDepth_{}, probeFramebuffer_{}, probeLevels_{}, probeFace_{};
    unsigned probeSize_ = 256;
    int facadeUniform_ = -1;
    std::vector<Textures> materials_;
    std::vector<CityMesh::Draw> draws_;
    std::vector<Bounds> chunks_;
    float anisotropy_{};
};
} // namespace spidy::city
