#include "quest_xr.hpp"
#include <GLES2/gl2ext.h>
#include <algorithm>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <cstring>
#include <stdexcept>

namespace spidy::quest {
namespace {
void xr(XrResult result, const char* what) {
    if (XR_FAILED(result))
        throw std::runtime_error(std::string(what) + " failed (XrResult " + std::to_string(result) + ")");
}
Pose pose(const XrPosef& p) {
    return {{p.position.x, p.position.y, p.position.z},
            {p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w}};
}
XrPosef identity() {
    XrPosef p{};
    p.orientation.w = 1;
    return p;
}
PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC framebufferTextureMultisample{};
PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC renderbufferStorageMultisample{};
} // namespace

void log(const std::string& line) {
    __android_log_write(ANDROID_LOG_INFO, "SpidyCity", line.c_str());
}

Xr::~Xr() {
    for (auto& e : eyes_) {
        if (!e.framebuffers.empty())
            glDeleteFramebuffers(static_cast<GLsizei>(e.framebuffers.size()), e.framebuffers.data());
        if (e.depth)
            glDeleteRenderbuffers(1, &e.depth);
        if (e.swapchain)
            xrDestroySwapchain(e.swapchain);
    }
    if (session_)
        xrDestroySession(session_);
    if (instance_)
        xrDestroyInstance(instance_);
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface_ != EGL_NO_SURFACE)
            eglDestroySurface(display_, surface_);
        if (context_ != EGL_NO_CONTEXT)
            eglDestroyContext(display_, context_);
        eglTerminate(display_);
    }
}

void Xr::initialize(android_app* app) {
    // The loader finds the headset's runtime through the app's Java VM.
    PFN_xrInitializeLoaderKHR initializeLoader{};
    xr(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                             reinterpret_cast<PFN_xrVoidFunction*>(&initializeLoader)),
       "Find the OpenXR loader");
    XrLoaderInitInfoAndroidKHR loader{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    loader.applicationVM = app->activity->vm;
    loader.applicationContext = app->activity->clazz;
    xr(initializeLoader(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loader)),
       "Start the OpenXR loader");
    const char* extensions[] = {XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
                                XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfoAndroidKHR android{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    android.applicationVM = app->activity->vm;
    android.applicationActivity = app->activity->clazz;
    XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};
    create.next = &android;
    std::strncpy(create.applicationInfo.applicationName, "Spidy City Lab", XR_MAX_APPLICATION_NAME_SIZE - 1);
    std::strncpy(create.applicationInfo.engineName, "Spidy", XR_MAX_ENGINE_NAME_SIZE - 1);
    create.applicationInfo.applicationVersion = 1;
    create.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    create.enabledExtensionCount = 2;
    create.enabledExtensionNames = extensions;
    xr(xrCreateInstance(&create, &instance_), "Create the OpenXR instance");
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    xr(xrGetSystem(instance_, &systemInfo, &system_), "Find the headset");
    XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};
    xr(xrGetSystemProperties(instance_, system_, &properties), "Read the headset");
    log(std::string("Headset: ") + properties.systemName);

    // OpenGL ES 3 on a tiny pbuffer: the swapchains are the real targets.
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr))
        throw std::runtime_error("No EGL display");
    const EGLint configAttributes[] = {EGL_RED_SIZE,
                                       8,
                                       EGL_GREEN_SIZE,
                                       8,
                                       EGL_BLUE_SIZE,
                                       8,
                                       EGL_ALPHA_SIZE,
                                       8,
                                       EGL_DEPTH_SIZE,
                                       0,
                                       EGL_SAMPLES,
                                       0,
                                       EGL_RENDERABLE_TYPE,
                                       EGL_OPENGL_ES3_BIT_KHR,
                                       EGL_SURFACE_TYPE,
                                       EGL_PBUFFER_BIT,
                                       EGL_NONE};
    EGLint count = 0;
    if (!eglChooseConfig(display_, configAttributes, &config_, 1, &count) || count < 1)
        throw std::runtime_error("No OpenGL ES 3 configuration");
    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, contextAttributes);
    const EGLint surfaceAttributes[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    surface_ = eglCreatePbufferSurface(display_, config_, surfaceAttributes);
    if (context_ == EGL_NO_CONTEXT || surface_ == EGL_NO_SURFACE ||
        !eglMakeCurrent(display_, surface_, surface_, context_))
        throw std::runtime_error("No OpenGL ES 3 context");
    log(std::string("GL: ") + reinterpret_cast<const char*>(glGetString(GL_RENDERER)) + " / " +
        reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    PFN_xrGetOpenGLESGraphicsRequirementsKHR requirements{};
    xr(xrGetInstanceProcAddr(instance_, "xrGetOpenGLESGraphicsRequirementsKHR",
                             reinterpret_cast<PFN_xrVoidFunction*>(&requirements)),
       "Find the OpenGL ES requirements");
    XrGraphicsRequirementsOpenGLESKHR needs{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    xr(requirements(instance_, system_, &needs), "Read the OpenGL ES requirements");
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (XR_MAKE_VERSION(major, minor, 0) < needs.minApiVersionSupported)
        throw std::runtime_error("The runtime needs a newer OpenGL ES");

    XrGraphicsBindingOpenGLESAndroidKHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    binding.display = display_;
    binding.config = config_;
    binding.context = context_;
    XrSessionCreateInfo session{XR_TYPE_SESSION_CREATE_INFO};
    session.next = &binding;
    session.systemId = system_;
    xr(xrCreateSession(instance_, &session, &session_), "Create the session");

    // Standing space on the floor; LOCAL at a standing height without a boundary.
    uint32_t spaceCount = 0;
    xr(xrEnumerateReferenceSpaces(session_, 0, &spaceCount, nullptr), "Count spaces");
    std::vector<XrReferenceSpaceType> spaces(spaceCount);
    xr(xrEnumerateReferenceSpaces(session_, spaceCount, &spaceCount, spaces.data()), "Read spaces");
    XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    space.poseInReferenceSpace = identity();
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    if (std::find(spaces.begin(), spaces.end(), XR_REFERENCE_SPACE_TYPE_STAGE) == spaces.end()) {
        space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        space.poseInReferenceSpace.position.y = -1.7f;
        log("No boundary (STAGE): standing 1.7 m above LOCAL");
    }
    xr(xrCreateReferenceSpace(session_, &space, &space_), "Create the tracking space");
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    space.poseInReferenceSpace = identity();
    xr(xrCreateReferenceSpace(session_, &space, &head_), "Create the head space");
    actions();
    swapchains();
}

XrPath Xr::path(const char* value) {
    XrPath p{};
    xr(xrStringToPath(instance_, value, &p), value);
    return p;
}

XrAction Xr::action(const char* name, const char* label, XrActionType type, bool perHand) {
    XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
    std::strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
    std::strncpy(info.localizedActionName, label, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    info.actionType = type;
    if (perHand) {
        info.countSubactionPaths = 2;
        info.subactionPaths = handPaths_.data();
    }
    XrAction out{};
    xr(xrCreateAction(actionSet_, &info, &out), name);
    return out;
}

void Xr::actions() {
    handPaths_ = {path("/user/hand/left"), path("/user/hand/right")};
    XrActionSetCreateInfo set{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strncpy(set.actionSetName, "swinging", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    std::strncpy(set.localizedActionSetName, "Spidy Swinging", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    xr(xrCreateActionSet(instance_, &set, &actionSet_), "Create actions");
    aim_ = action("aim", "Aim web", XR_ACTION_TYPE_POSE_INPUT);
    grip_ = action("hand", "Tracked hand", XR_ACTION_TYPE_POSE_INPUT);
    trigger_ = action("trigger", "Reel web", XR_ACTION_TYPE_FLOAT_INPUT);
    squeeze_ = action("grip", "Shoot and hold web", XR_ACTION_TYPE_FLOAT_INPUT);
    stick_ = action("stick", "Move / turn", XR_ACTION_TYPE_VECTOR2F_INPUT);
    stickClick_ = action("stick_click", "Stick click", XR_ACTION_TYPE_BOOLEAN_INPUT);
    jump_ = action("jump", "Jump / point launch", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
    reset_ = action("reset", "Back to the launch roof", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
    buttonB_ = action("button_b", "B", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
    buttonX_ = action("button_x", "X", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
    menu_ = action("menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
    haptic_ = action("haptic", "Web feedback", XR_ACTION_TYPE_VIBRATION_OUTPUT);
    std::vector<XrActionSuggestedBinding> bindings;
    for (const char* hand : {"left", "right"}) {
        const std::string prefix = std::string("/user/hand/") + hand;
        const auto bind = [&](XrAction a, const char* suffix) {
            bindings.push_back({a, path((prefix + suffix).c_str())});
        };
        bind(aim_, "/input/aim/pose");
        bind(grip_, "/input/grip/pose");
        bind(trigger_, "/input/trigger/value");
        bind(squeeze_, "/input/squeeze/value");
        bind(stick_, "/input/thumbstick");
        bind(stickClick_, "/input/thumbstick/click");
        bind(haptic_, "/output/haptic");
    }
    bindings.push_back({jump_, path("/user/hand/right/input/a/click")});
    bindings.push_back({reset_, path("/user/hand/left/input/y/click")});
    bindings.push_back({buttonB_, path("/user/hand/right/input/b/click")});
    bindings.push_back({buttonX_, path("/user/hand/left/input/x/click")});
    bindings.push_back({menu_, path("/user/hand/left/input/menu/click")});
    XrInteractionProfileSuggestedBinding suggest{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggest.interactionProfile = path("/interaction_profiles/oculus/touch_controller");
    suggest.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
    suggest.suggestedBindings = bindings.data();
    xr(xrSuggestInteractionProfileBindings(instance_, &suggest), "Suggest the Touch bindings");
    for (int i = 0; i < 2; ++i) {
        XrActionSpaceCreateInfo info{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        info.subactionPath = handPaths_[i];
        info.poseInActionSpace = identity();
        info.action = aim_;
        xr(xrCreateActionSpace(session_, &info, &aimSpaces_[i]), "Create aim space");
        info.action = grip_;
        xr(xrCreateActionSpace(session_, &info, &gripSpaces_[i]), "Create grip space");
    }
    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &actionSet_;
    xr(xrAttachSessionActionSets(session_, &attach), "Attach actions");
}

void Xr::swapchains() {
    uint32_t count = 0;
    xr(xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                                         &count, nullptr),
       "Count views");
    if (count != 2)
        throw std::runtime_error("Two eyes expected");
    std::array<XrViewConfigurationView, 2> views{
        {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}}};
    xr(xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2,
                                         &count, views.data()),
       "Read views");
    xr(xrEnumerateSwapchainFormats(session_, 0, &count, nullptr), "Count formats");
    std::vector<int64_t> formats(count);
    xr(xrEnumerateSwapchainFormats(session_, count, &count, formats.data()), "Read formats");
    // sRGB: the renderer writes linear colour and the hardware encodes it.
    int64_t format = 0;
    for (int64_t wanted : {static_cast<int64_t>(GL_SRGB8_ALPHA8), static_cast<int64_t>(GL_RGBA8)})
        if (!format && std::find(formats.begin(), formats.end(), wanted) != formats.end())
            format = wanted;
    if (!format)
        throw std::runtime_error("No usable swapchain format");
    if (format != GL_SRGB8_ALPHA8)
        log("No sRGB swapchain: colours will look dark");
    // 4x MSAA resolved on the tiles, where the Quest's GPU supports it.
    const auto* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (extensions && std::strstr(extensions, "GL_EXT_multisampled_render_to_texture")) {
        framebufferTextureMultisample = reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC>(
            eglGetProcAddress("glFramebufferTexture2DMultisampleEXT"));
        renderbufferStorageMultisample = reinterpret_cast<PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC>(
            eglGetProcAddress("glRenderbufferStorageMultisampleEXT"));
        if (framebufferTextureMultisample && renderbufferStorageMultisample)
            samples_ = 4;
    }
    for (unsigned i = 0; i < 2; ++i) {
        auto& e = eyes_[i];
        e.width = views[i].recommendedImageRectWidth;
        e.height = views[i].recommendedImageRectHeight;
        XrSwapchainCreateInfo sc{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        sc.format = format;
        sc.sampleCount = 1;
        sc.width = e.width;
        sc.height = e.height;
        sc.faceCount = sc.arraySize = sc.mipCount = 1;
        xr(xrCreateSwapchain(session_, &sc, &e.swapchain), "Create an eye's swapchain");
        xr(xrEnumerateSwapchainImages(e.swapchain, 0, &count, nullptr), "Count eye images");
        e.images.assign(count, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
        xr(xrEnumerateSwapchainImages(e.swapchain, count, &count,
                                      reinterpret_cast<XrSwapchainImageBaseHeader*>(e.images.data())),
           "Read eye images");
        glGenRenderbuffers(1, &e.depth);
        glBindRenderbuffer(GL_RENDERBUFFER, e.depth);
        if (samples_ > 1)
            renderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_DEPTH_COMPONENT24,
                                           static_cast<GLsizei>(e.width), static_cast<GLsizei>(e.height));
        else
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, static_cast<GLsizei>(e.width),
                                  static_cast<GLsizei>(e.height));
        e.framebuffers.resize(count);
        glGenFramebuffers(static_cast<GLsizei>(count), e.framebuffers.data());
        for (uint32_t j = 0; j < count; ++j) {
            glBindFramebuffer(GL_FRAMEBUFFER, e.framebuffers[j]);
            if (samples_ > 1)
                framebufferTextureMultisample(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                              e.images[j].image, 0, samples_);
            else
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, e.images[j].image,
                                       0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, e.depth);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                throw std::runtime_error("An eye's framebuffer is incomplete");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        log("Eye " + std::to_string(i) + ": " + std::to_string(e.width) + "x" + std::to_string(e.height) +
            ", " + std::to_string(count) + " images, " + std::to_string(samples_) + "x MSAA");
    }
}

bool Xr::poll() {
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(instance_, &event) == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            state_ = reinterpret_cast<XrEventDataSessionStateChanged*>(&event)->state;
            if (state_ == XR_SESSION_STATE_READY && !running_) {
                XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                xr(xrBeginSession(session_, &begin), "Begin the session");
                running_ = true;
                previous_ = 0;
            } else if (state_ == XR_SESSION_STATE_STOPPING && running_) {
                xr(xrEndSession(session_), "End the session");
                running_ = false;
            } else if (state_ == XR_SESSION_STATE_EXITING || state_ == XR_SESSION_STATE_LOSS_PENDING) {
                exit_ = true;
            }
        } else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            exit_ = true;
        }
        event = {XR_TYPE_EVENT_DATA_BUFFER};
    }
    return !exit_;
}

bool Xr::locate(XrSpace source, XrTime time, Pose& out) {
    XrSpaceLocation l{XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(xrLocateSpace(source, space_, time, &l)))
        return false;
    constexpr XrSpaceLocationFlags valid =
        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if ((l.locationFlags & valid) != valid)
        return false;
    out = pose(l.pose);
    return true;
}

XrFrame Xr::input(XrTime time) {
    XrFrame frame;
    frame.focused = state_ == XR_SESSION_STATE_FOCUSED;
    frame.valid = locate(head_, time, frame.head);
    frame.predictedDisplayTime = time;
    if (previous_)
        frame.seconds =
            std::clamp(static_cast<float>(static_cast<double>(time - previous_) / 1e9), .001f, .1f);
    previous_ = time;
    if (!frame.focused)
        return frame;
    XrActiveActionSet active{actionSet_, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    if (xrSyncActions(session_, &sync) != XR_SUCCESS) {
        frame.focused = false;
        return frame;
    }
    for (int i = 0; i < 2; ++i) {
        auto& hand = frame.hands[i];
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.subactionPath = handPaths_[i];
        XrActionStatePose a{XR_TYPE_ACTION_STATE_POSE}, g{XR_TYPE_ACTION_STATE_POSE};
        get.action = aim_;
        xrGetActionStatePose(session_, &get, &a);
        get.action = grip_;
        xrGetActionStatePose(session_, &get, &g);
        hand.valid = a.isActive && g.isActive && locate(aimSpaces_[i], time, hand.aim) &&
                     locate(gripSpaces_[i], time, hand.grip);
        XrActionStateFloat f{XR_TYPE_ACTION_STATE_FLOAT};
        get.action = trigger_;
        xrGetActionStateFloat(session_, &get, &f);
        hand.trigger = f.isActive ? f.currentState : 0;
        get.action = squeeze_;
        xrGetActionStateFloat(session_, &get, &f);
        hand.squeeze = f.isActive ? f.currentState : 0;
        XrActionStateVector2f v{XR_TYPE_ACTION_STATE_VECTOR2F};
        get.action = stick_;
        xrGetActionStateVector2f(session_, &get, &v);
        if (v.isActive) {
            hand.stickX = v.currentState.x;
            hand.stickY = v.currentState.y;
        }
        XrActionStateBoolean click{XR_TYPE_ACTION_STATE_BOOLEAN};
        get.action = stickClick_;
        xrGetActionStateBoolean(session_, &get, &click);
        hand.stickClick = click.isActive && click.currentState;
    }
    const auto boolean = [&](XrAction action) {
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = action;
        XrActionStateBoolean b{XR_TYPE_ACTION_STATE_BOOLEAN};
        xrGetActionStateBoolean(session_, &get, &b);
        return b.isActive && b.currentState;
    };
    frame.jump = boolean(jump_);
    frame.reset = boolean(reset_);
    frame.buttons = (frame.jump ? buttonA : 0u) | (boolean(buttonB_) ? buttonB : 0u) |
                    (boolean(buttonX_) ? buttonX : 0u) | (frame.reset ? buttonY : 0u) |
                    (boolean(menu_) ? buttonMenu : 0u);
    return frame;
}

void Xr::frame(const std::function<void(const XrFrame&)>& update,
               const std::function<void(unsigned, const Pose&, const EyeFov&, unsigned, unsigned)>& draw) {
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState state{XR_TYPE_FRAME_STATE};
    xr(xrWaitFrame(session_, &wait, &state), "Wait for a frame");
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    xr(xrBeginFrame(session_, &begin), "Begin a frame");
    std::array<XrView, 2> views{{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
    std::array<XrCompositionLayerProjectionView, 2> projected{
        {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}}};
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    const XrCompositionLayerBaseHeader* layers[] = {reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer)};
    bool submit = false;
    XrFrame input = this->input(state.predictedDisplayTime);
    if (state.shouldRender) {
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
        locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime = state.predictedDisplayTime;
        locate.space = space_;
        XrViewState viewState{XR_TYPE_VIEW_STATE};
        uint32_t count = 0;
        xr(xrLocateViews(session_, &locate, &viewState, 2, &count, views.data()), "Locate the eyes");
        submit = (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) &&
                 (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT);
        for (unsigned i = 0; i < 2; ++i)
            input.eyes[i] = {pose(views[i].pose),
                             {views[i].fov.angleLeft, views[i].fov.angleRight, views[i].fov.angleDown,
                              views[i].fov.angleUp},
                             eyes_[i].width,
                             eyes_[i].height};
    }
    update(input);
    if (submit) {
        for (unsigned i = 0; i < 2; ++i) {
            auto& e = eyes_[i];
            uint32_t index = 0;
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            xr(xrAcquireSwapchainImage(e.swapchain, &acquire, &index), "Acquire an eye image");
            XrSwapchainImageWaitInfo waitImage{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            waitImage.timeout = XR_INFINITE_DURATION;
            xr(xrWaitSwapchainImage(e.swapchain, &waitImage), "Wait for an eye image");
            glBindFramebuffer(GL_FRAMEBUFFER, e.framebuffers[index]);
            draw(i, input.eyes[i].pose, input.eyes[i].fov, e.width, e.height);
            // The depth never leaves the tiles.
            const GLenum discard = GL_DEPTH_ATTACHMENT;
            glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, &discard);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            xr(xrReleaseSwapchainImage(e.swapchain, &release), "Release an eye image");
            projected[i].pose = views[i].pose;
            projected[i].fov = views[i].fov;
            projected[i].subImage.swapchain = e.swapchain;
            projected[i].subImage.imageRect = {
                {0, 0}, {static_cast<int32_t>(e.width), static_cast<int32_t>(e.height)}};
        }
        layer.space = space_;
        layer.viewCount = 2;
        layer.views = projected.data();
    }
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = state.predictedDisplayTime;
    end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end.layerCount = submit ? 1 : 0;
    end.layers = submit ? layers : nullptr;
    xr(xrEndFrame(session_, &end), "End a frame");
}

void Xr::haptic(int hand, float strength, float seconds) {
    if (hand < 0 || hand > 1 || state_ != XR_SESSION_STATE_FOCUSED)
        return;
    XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
    info.action = haptic_;
    info.subactionPath = handPaths_[hand];
    XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
    vibration.amplitude = std::clamp(strength, 0.f, 1.f);
    vibration.duration = static_cast<XrDuration>(std::clamp(seconds, .005f, .5f) * 1e9f);
    vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
    xrApplyHapticFeedback(session_, &info, reinterpret_cast<XrHapticBaseHeader*>(&vibration));
}
} // namespace spidy::quest
