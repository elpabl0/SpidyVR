// Renders the Quest lab's city without a headset: an OpenGL ES 3 context
// with no window (EGL), the same renderer the app uses, a few cameras, and a
// picture of each (binary PPM). For checking the city's look on a desktop or
// in CI:  spidy_city_preview <assets dir> <out dir> [width height]
#include "spidy/city_renderer.hpp"
#include "spidy/overlay_geometry.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using namespace spidy;
using namespace spidy::city;
namespace {
void contextOrThrow() {
    EGLDisplay display = EGL_NO_DISPLAY;
    if (auto getPlatformDisplay =
            reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT")))
        display = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    if (display == EGL_NO_DISPLAY)
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr))
        throw std::runtime_error("No EGL display");
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint configAttributes[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE,
                                       EGL_PBUFFER_BIT, EGL_NONE};
    EGLConfig config{};
    EGLint count = 0;
    eglChooseConfig(display, configAttributes, &config, 1, &count);
    const EGLint contextAttributes[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
    EGLContext context =
        eglCreateContext(display, count ? config : EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, contextAttributes);
    if (context == EGL_NO_CONTEXT || !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
        throw std::runtime_error("No OpenGL ES 3 context");
    std::cout << "GL: " << glGetString(GL_RENDERER) << " / " << glGetString(GL_VERSION) << '\n';
}
// A camera at `eye` looking at `at`, as a pose (-z ahead).
Pose lookAt(Vec3 eye, Vec3 at) {
    const Vec3 f = normalized(at - eye);
    const float yaw = std::atan2(-f.x, -f.z), pitch = std::asin(std::clamp(f.y, -1.f, 1.f));
    return {eye, Quat::yaw(yaw) * Quat::around({1, 0, 0}, pitch)};
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 3 && argc != 5) {
        std::cerr << "Usage: spidy_city_preview <assets dir> <out dir> [width height]\n";
        return 2;
    }
    try {
        const std::string assets = argv[1], out = argv[2];
        const unsigned width = argc == 5 ? std::stoul(argv[3]) : 1280,
                       height = argc == 5 ? std::stoul(argv[4]) : 720;
        contextOrThrow();
        const auto started = std::chrono::steady_clock::now();
        const City city;
        Renderer renderer;
        renderer.initialize(
            city.mesh(),
            [&](const std::string& path) {
                std::ifstream f(assets + "/" + path, std::ios::binary);
                return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f), {});
            },
            [](const std::string& line) { std::cout << line << '\n'; });
        std::cout << "City and renderer ready in "
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count()
                  << " s: " << city.buildings() << " buildings, " << city.mesh().indices.size() / 3
                  << " triangles\n";
        unsigned framebuffer{}, color{}, depth{};
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glGenTextures(1, &color);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_SRGB8_ALPHA8, static_cast<GLsizei>(width),
                       static_cast<GLsizei>(height));
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        glGenRenderbuffers(1, &depth);
        glBindRenderbuffer(GL_RENDERBUFFER, depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, static_cast<GLsizei>(width),
                              static_cast<GLsizei>(height));
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Preview framebuffer incomplete");
        // The lab's crates and thugs, as boxes.
        std::vector<Vertex> overlay;
        for (const auto& p : city.props()) {
            const Vec3 half = p.kind == TargetKind::Character ? Vec3{.25f, .95f, .15f} : p.half;
            addBox(overlay, p.position - half, p.position + half, p.color);
        }
        const Vec3 roof = city.launch().feet + Vec3{0, 1.7f, 0};
        struct Shot {
            const char* name;
            Pose camera;
        };
        const Shot shots[] = {
            {"launch_roof", lookAt(roof + Vec3{0, 0, 8}, roof + Vec3{-10, -6, -60})},
            {"avenue", lookAt({city.launch().feet.x - 14, 1.7f, city.launch().feet.z + 20},
                              {city.launch().feet.x - 14, 12, city.launch().feet.z - 200})},
            {"skyline", lookAt({300, 160, 420}, {0, 60, 0})},
            {"swing", lookAt(roof + Vec3{-14, 25, -30}, roof + Vec3{-14, 10, -140})},
            {"street_corner", lookAt({city.launch().feet.x - 4, 1.7f, city.launch().feet.z + 26},
                                     {city.launch().feet.x + 30, 8, city.launch().feet.z + 10})},
            {"aerial", lookAt({-200, 420, 500}, {0, 0, -150})},
            {"shopfront", lookAt({city.launch().feet.x - 2, 1.7f, city.launch().feet.z + 24},
                                 {city.launch().feet.x - 2, 1.5f, city.launch().feet.z + 10})},
        };
        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        const EyeFov wide{std::atan(-std::tan(.6f) * aspect), std::atan(std::tan(.6f) * aspect), -.6f, .6f};
        std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
        const auto save = [&](const std::string& name, double ms) {
            glReadPixels(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height), GL_RGBA,
                         GL_UNSIGNED_BYTE, pixels.data());
            const std::string path = out + "/" + name + ".ppm";
            std::ofstream f(path, std::ios::binary);
            f << "P6\n" << width << ' ' << height << "\n255\n";
            for (unsigned y = height; y-- > 0;)
                for (unsigned x = 0; x < width; ++x)
                    f.write(
                        reinterpret_cast<const char*>(&pixels[(static_cast<std::size_t>(y) * width + x) * 4]),
                        3);
            std::cout << path << " (" << ms << " ms, GL error " << glGetError() << ")\n";
        };
        const std::pair<TimeOfDay, const char*> times[] = {
            {TimeOfDay::Day, "day"}, {TimeOfDay::Sunset, "sunset"}, {TimeOfDay::Night, "night"}};
        for (const auto& [time, label] : times) {
            renderer.timeOfDay(time);
            for (const auto& shot : shots) {
                const auto t0 = std::chrono::steady_clock::now();
                renderer.shadows(shot.camera.position);
                renderer.reflections(shot.camera.position, 6);
                glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
                const Mat4 vp = multiply(glProjection(wide), viewMatrix(shot.camera));
                renderer.view(vp, shot.camera.position, width, height, overlay, 3);
                save(
                    std::string(label) + "_" + shot.name,
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            }
        }
        // The reflection probe drawn as the sky from the last view's place:
        // it should match that view (a wrong cube face shows here).
        renderer.timeOfDay(TimeOfDay::Day);
        const auto& check = shots[0].camera;
        renderer.shadows(check.position);
        renderer.reflections(check.position, 6);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        const Mat4 vp = multiply(glProjection(wide), viewMatrix(check));
        renderer.showProbe = true;
        renderer.view(vp, check.position, width, height, {}, 3);
        renderer.showProbe = false;
        save("probe_check", 0);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Preview: " << e.what() << '\n';
        return 1;
    }
}
