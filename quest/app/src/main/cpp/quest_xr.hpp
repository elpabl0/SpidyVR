#pragma once
// OpenXR on the Quest itself: an EGL context for OpenGL ES 3, the session,
// the Touch controllers (the same actions and bindings as the Windows
// lab's), and one swapchain per eye drawn through 4x multisampling.
#include "spidy/tracking.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <jni.h>
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <array>
#include <functional>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <string>
#include <vector>

struct android_app;
namespace spidy::quest {
void log(const std::string& line);
class Xr {
  public:
    Xr() = default;
    Xr(const Xr&) = delete;
    Xr& operator=(const Xr&) = delete;
    ~Xr();
    // Makes the EGL context current on this thread; throws when the
    // runtime, the headset or OpenGL ES 3 is missing.
    void initialize(android_app* app);
    // The runtime's events; false once it wants the app gone.
    bool poll();
    bool running() const {
        return running_;
    }
    // One frame: update with the controllers and head (tracking space), then
    // draw each eye into its bound framebuffer (eye pose in tracking space).
    void frame(const std::function<void(const XrFrame&)>& update,
               const std::function<void(unsigned eye, const Pose& pose, const EyeFov& fov, unsigned width,
                                        unsigned height)>& draw);
    void haptic(int hand, float strength, float seconds = .025f);

  private:
    struct Eye {
        XrSwapchain swapchain{};
        unsigned width{}, height{};
        std::vector<XrSwapchainImageOpenGLESKHR> images;
        std::vector<GLuint> framebuffers;
        GLuint depth{};
    };
    XrPath path(const char* value);
    XrAction action(const char* name, const char* label, XrActionType type, bool perHand = true);
    void actions();
    void swapchains();
    bool locate(XrSpace space, XrTime time, Pose& out);
    XrFrame input(XrTime time);
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLConfig config_{};
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
    XrInstance instance_{};
    XrSystemId system_{};
    XrSession session_{};
    XrSpace space_{}, head_{};
    XrSessionState state_ = XR_SESSION_STATE_UNKNOWN;
    XrActionSet actionSet_{};
    XrAction aim_{}, grip_{}, trigger_{}, squeeze_{}, stick_{}, stickClick_{}, jump_{}, reset_{}, haptic_{};
    XrAction buttonB_{}, buttonX_{}, menu_{};
    std::array<XrPath, 2> handPaths_{};
    std::array<XrSpace, 2> aimSpaces_{}, gripSpaces_{};
    std::array<Eye, 2> eyes_{};
    XrTime previous_{};
    bool running_{}, exit_{};
    int samples_ = 1;
};
} // namespace spidy::quest
