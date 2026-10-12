#include "spidy/city_renderer.hpp"
#include <GLES3/gl3.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <stb_image.h>

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

namespace spidy::city {
namespace {
constexpr const char* cityVertex = R"(#version 300 es
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aUv;
layout(location = 3) in float aContact;
layout(location = 4) in vec4 aTint;
layout(location = 5) in vec4 aStyle;
uniform mat4 uViewProjection;
uniform mat4 uShadow;
out vec3 vPosition;
out vec3 vNormal;
out vec4 vUv;
out float vContact;
flat out vec4 vTint;
flat out vec4 vStyle;
out vec3 vShadow;
void main() {
    vPosition = aPosition;
    vNormal = aNormal;
    vUv = aUv;
    vContact = aContact;
    vTint = aTint;
    vStyle = aStyle;
    // A normal offset keeps a face out of its own shadow.
    vec4 s = uShadow * vec4(aPosition + aNormal * 0.15, 1.0);
    vShadow = s.xyz / s.w * 0.5 + 0.5;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)";
// The sky's functions, shared by the city and the sky.
#define SPIDY_SKY_GLSL                                                                                         \
    "const float PI = 3.14159265;\n"                                                                           \
    "vec2 skyUv(vec3 d) { return vec2(0.5 + atan(d.x, -d.z) / (2.0 * PI), acos(clamp(d.y, -1.0, 1.0)) / PI); }\n" \
    "vec3 sky(vec3 d, float lod) { return textureLod(uSky, skyUv(d), max(lod, 0.0)).rgb; }\n"                  \
    "vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }\n"
constexpr const char* cityFragment = R"(#version 300 es
precision highp float;
precision highp sampler2DShadow;
uniform sampler2D uAlbedo;
uniform sampler2D uSurface;
uniform sampler2D uSky;
uniform sampler2DShadow uShadowMap;
uniform vec3 uEye, uSun, uSunColor;
uniform float uSkyLevels, uExposure, uTime, uFogDensity;
in vec3 vPosition;
in vec3 vNormal;
in vec4 vUv;
in float vContact;
flat in vec4 vTint;
flat in vec4 vStyle;
in vec3 vShadow;
out vec4 outColor;
)" SPIDY_SKY_GLSL R"(
float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
float sunShadow() {
    if (any(lessThan(vShadow, vec3(0.0))) || any(greaterThan(vShadow, vec3(1.0))))
        return 1.0;
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float s = texture(uShadowMap, vec3(vShadow.xy + vec2(-0.6, -0.6) * texel, vShadow.z));
    s += texture(uShadowMap, vec3(vShadow.xy + vec2(0.6, -0.6) * texel, vShadow.z));
    s += texture(uShadowMap, vec3(vShadow.xy + vec2(-0.6, 0.6) * texel, vShadow.z));
    s += texture(uShadowMap, vec3(vShadow.xy + vec2(0.6, 0.6) * texel, vShadow.z));
    // Fade out at the map's edge rather than end in a line.
    float edge = max(abs(vShadow.x - 0.5), abs(vShadow.y - 0.5)) * 2.0;
    return mix(s * 0.25, 1.0, smoothstep(0.85, 1.0, edge));
}
// Inside a rectangle [lo, hi] of a cell, antialiased: 1 inside, 0 out. Where
// cells shrink towards a pixel both ways, the share of the cell it covers
// instead: far windows blend into a facade's tone rather than shimmer.
float rect(vec2 c, vec2 lo, vec2 hi, vec2 fw) {
    fw = max(fw, vec2(1e-4)); // smoothstep's edges must differ
    vec2 a = smoothstep(lo - fw, lo + fw, c) * (1.0 - smoothstep(hi - fw, hi + fw, c));
    a = mix(a, clamp(hi - lo, 0.0, 1.0), smoothstep(0.15, 0.45, min(fw.x, fw.y)));
    return a.x * a.y;
}
// Haze along the view: thick near the streets, thinning with height.
float hazeAt(vec3 p) {
    const float scale = 140.0;
    float y0 = max(uEye.y, 0.0), y1 = max(p.y, 0.0), dy = y1 - y0;
    float mean = abs(dy) < 0.5 ? exp(-y0 / scale) : (exp(-y0 / scale) - exp(-y1 / scale)) / (dy / scale);
    return 1.0 - exp(-length(p - uEye) * uFogDensity * mean);
}
void main() {
    int kind = int(vStyle.x + 0.5);
    vec3 n = normalize(vNormal);
    vec2 uv = vUv.xy;
    vec3 albedo = texture(uAlbedo, uv).rgb;
    vec4 surface = texture(uSurface, uv);
    float rough = surface.z, metal = surface.w;
    vec3 t = abs(n.y) > 0.9 ? vec3(1.0, 0.0, 0.0) : normalize(cross(vec3(0.0, 1.0, 0.0), n));
    vec3 b = cross(n, t);
    // Relief fades with distance: far bumps only sparkle.
    // Grounds and roofs read flatter: their grit sparkles otherwise.
    vec2 bump = (surface.xy * 2.0 - 1.0) * (1.0 - smoothstep(15.0, 90.0, length(vPosition - uEye)) * 0.85) *
                mix(1.0, 0.45, step(0.9, n.y));
    vec3 N = normalize(t * bump.x + b * bump.y + n * sqrt(max(1.0 - dot(bump, bump), 0.0)));
    vec3 tint = vTint.rgb * (255.0 / 128.0);
    // Per face, exact: the hashes below turn the least difference into another window.
    float seed = floor(vTint.a * 255.0 + 0.5);
    vec3 emission = vec3(0.0), room = vec3(0.0);
    float glass = 0.0;
    if (kind == 1) {
        // A window in each bay of each floor, a room behind each.
        albedo *= tint;
        vec2 cell = vUv.zw, c = fract(cell), id = floor(cell), fw = fwidth(cell);
        float w = vStyle.y / 255.0, h = vStyle.z / 255.0, sill = (1.0 - h) * 0.42;
        vec2 lo = vec2(0.5 - 0.5 * w, sill), hi = vec2(0.5 + 0.5 * w, sill + h);
        float opening = rect(c, lo, hi, fw);
        float pane = rect(c, lo + vec2(0.04, 0.05), hi - vec2(0.04, 0.05), fw);
        float mullion = w > 0.5 ? 1.0 - smoothstep(0.012, 0.012 + fw.x * 2.0, abs(c.x - 0.5)) : 0.0;
        float transom = 1.0 - smoothstep(0.01, 0.01 + fw.y * 2.0, abs(c.y - (sill + h * 0.62)));
        float stone = rect(c, vec2(lo.x - 0.06, lo.y - 0.07), vec2(hi.x + 0.06, lo.y), fw) +
                      rect(c, vec2(lo.x - 0.03, hi.y), vec2(hi.x + 0.03, hi.y + 0.06), fw);
        albedo = mix(albedo, vec3(0.5, 0.48, 0.44), clamp(stone, 0.0, 1.0) * (1.0 - opening));
        float r = hash(id + vec2(seed * 0.37, seed * 0.61));
        vec3 frame = seed > 128.0 ? vec3(0.55, 0.54, 0.5) : vec3(0.05, 0.05, 0.055);
        albedo = mix(albedo, frame, opening);
        rough = mix(rough, 0.45, opening);
        metal = mix(metal, 0.0, opening);
        glass = pane * (1.0 - max(mullion, transom));
        room = r < 0.08 ? vec3(1.0, 0.72, 0.42) * 0.5 : r < 0.4 ? vec3(0.62, 0.57, 0.48) * 0.16 : vec3(0.025, 0.028, 0.032);
        // Blinds part way down some.
        float blind = step(0.55, r) * step(r, 0.75) * step(sill + h * (1.0 - fract(r * 13.0) * 0.6), c.y);
        room = mix(room, vec3(0.5, 0.47, 0.4) * 0.35, blind);
    } else if (kind == 2) {
        // Shop windows between piers, a sign band above.
        albedo *= tint;
        vec2 cell = vUv.zw, c = vec2(fract(cell.x), cell.y), fw = fwidth(cell);
        float r = hash(vec2(floor(cell.x), seed));
        float pier = 1.0 - rect(c, vec2(0.05, -1.0), vec2(0.95, 2.0), fw);
        float door = step(0.66, r);
        float glassArea = rect(c, vec2(0.08, door > 0.5 ? 0.0 : 0.14), vec2(0.92, 0.7), fw);
        float frameArea = rect(c, vec2(0.06, door > 0.5 ? 0.0 : 0.12), vec2(0.94, 0.72), fw);
        float signArea = rect(c, vec2(0.06, 0.76), vec2(0.94, 0.93), fw);
        const vec3 signs[6] = vec3[6](vec3(0.55, 0.06, 0.05), vec3(0.04, 0.12, 0.3), vec3(0.05, 0.22, 0.1),
                                      vec3(0.08, 0.08, 0.08), vec3(0.6, 0.42, 0.08), vec3(0.75, 0.73, 0.68));
        vec3 sign = signs[int(r * 5.99)];
        float letters = step(0.5, hash(floor(vec2(c.x * 40.0, c.y * 9.0)) + r)) *
                        rect(c, vec2(0.25, 0.8), vec2(0.75, 0.89), fw) * step(0.3, r);
        albedo = mix(albedo, vec3(0.03), frameArea * (1.0 - pier));
        albedo = mix(albedo, sign, signArea);
        albedo = mix(albedo, vec3(0.9, 0.88, 0.8), letters);
        rough = mix(rough, 0.35, max(signArea, frameArea));
        metal = mix(metal, 0.0, max(signArea, frameArea));
        glass = glassArea * (1.0 - pier);
        // The shop inside: a lit ceiling fading down, shelves of goods, the
        // floor in shade; or a dark empty unit.
        float shelf = step(0.82, fract(c.y * 5.0 + 0.3)) * step(0.15, c.y);
        float goods = step(0.45, hash(floor(vec2(c.x * 14.0, c.y * 5.0 + 0.3)) + r)) * (1.0 - shelf) *
                      step(0.15, c.y) * step(c.y, 0.62);
        vec3 stock = mix(vec3(0.5, 0.2, 0.1), vec3(0.15, 0.3, 0.5), hash(floor(vec2(c.x * 14.0, c.y * 5.0)) + 7.0));
        vec3 lit = vec3(0.95, 0.8, 0.6) * mix(0.03, 0.16, smoothstep(0.1, 0.68, c.y));
        lit = mix(lit, stock * 0.12, goods * 0.8) + shelf * vec3(0.04);
        room = r < 0.6 ? lit : vec3(0.015, 0.015, 0.018);
    } else if (kind == 3) {
        // Road paint, worn where the asphalt shows through.
        float wear = clamp(dot(albedo, vec3(0.33)) * 3.5, 0.0, 1.0);
        albedo = toLinear(vTint.rgb) * mix(0.45, 1.0, wear);
        rough = 0.6;
        metal = 0.0;
    } else if (kind == 4) {
        emission = toLinear(vTint.rgb) * 3.0;
        albedo *= 0.1;
    } else if (kind == 5) {
        // The river: a few travelling waves and the sky in them.
        vec2 p = vPosition.xz;
        vec2 g = vec2(cos(p.x * 0.31 + uTime * 1.1), cos(p.y * 0.27 + uTime * 0.9)) * 0.04;
        g += vec2(0.8, 0.6) * cos(dot(p, vec2(0.8, 0.6)) * 1.2 + uTime * 1.9) * 0.025;
        g += vec2(-0.5, 0.86) * cos(dot(p, vec2(-0.5, 0.86)) * 2.3 + uTime * 2.6) * 0.015;
        N = normalize(vec3(-g.x, 1.0, -g.y));
        albedo = vec3(0.012, 0.025, 0.03);
        rough = 0.06;
        metal = 0.0;
    } else {
        albedo *= tint;
    }
    vec3 V = normalize(uEye - vPosition);
    float NdotV = max(dot(N, V), 1e-3);
    float NdotL = max(dot(N, uSun), 0.0) * step(0.0, dot(n, uSun));
    float shadow = NdotL > 0.0 ? sunShadow() : 0.0;
    // Darker where walls meet the ground or a roof, and down in the street canyons.
    float wallness = 1.0 - abs(n.y);
    float ao = vContact > -500.0 ? mix(1.0, mix(0.45, 1.0, smoothstep(0.0, 3.0, vPosition.y - vContact)), wallness) : 1.0;
    ao *= mix(0.55, 1.0, smoothstep(0.0, 80.0, vPosition.y));
    vec3 irradiance = sky(N, uSkyLevels - 2.0) * ao;
    vec3 R = reflect(-V, N);
    // Low in the streets a reflection sees the buildings across, not sky; below
    // the horizon, the city's grey rather than the sky's mirror image.
    float skyline = mix(0.45, 0.0, smoothstep(5.0, 140.0, vPosition.y));
    vec3 street = sky(normalize(vec3(R.x, 0.05, R.z)), uSkyLevels - 3.0) * 0.16;
    vec3 env = mix(street, sky(R, rough * (uSkyLevels - 3.0)), smoothstep(skyline - 0.15, skyline + 0.12, R.y));
    vec3 F0 = mix(vec3(0.04), albedo, metal);
    vec3 F = F0 + (max(vec3(1.0 - rough), F0) - F0) * pow(1.0 - NdotV, 5.0);
    vec3 H = normalize(V + uSun);
    float a = max(rough * rough, 0.002), NdotH = max(dot(N, H), 0.0);
    // Squared, not pow(): a base a hair below zero would give NaN.
    float q = max(NdotH * NdotH * (a * a - 1.0) + 1.0, 1e-4);
    float D = min(a * a / (PI * q * q), 300.0);
    float k = a * 0.5;
    float G = NdotV / (NdotV * (1.0 - k) + k) / max(NdotL * (1.0 - k) + k, 1e-3);
    vec3 Fs = F0 + (1.0 - F0) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    vec3 sunlight = uSunColor * NdotL * shadow;
    vec3 color = albedo * (1.0 - metal) * (sunlight + irradiance) + D * G * Fs * 0.25 * sunlight +
                 env * F * mix(ao, 1.0, 0.4);
    if (glass > 0.0) {
        // A flat pane: the room behind it, the sky (or the street) on it.
        float gv = max(dot(n, V), 1e-3);
        vec3 gr = reflect(-V, n);
        vec3 genv = mix(street, sky(gr, 0.5), smoothstep(skyline - 0.15, skyline + 0.12, gr.y));
        float fresnel = 0.04 + 0.96 * pow(1.0 - gv, 5.0);
        vec3 pane = room * (1.0 - fresnel) + genv * mix(0.3, 1.0, fresnel);
        float gh = max(dot(n, H), 0.0), ga = 0.0025;
        float gq = max(gh * gh * (ga * ga - 1.0) + 1.0, 1e-4);
        float gd = min(ga * ga / (PI * gq * gq), 300.0);
        pane += gd * 0.04 * uSunColor * max(dot(n, uSun), 0.0) * shadow;
        color = mix(color, pane, glass);
    }
    color += emission;
    float haze = hazeAt(vPosition);
    vec3 look = -V;
    vec3 hazeColor = sky(normalize(vec3(look.x, max(look.y, 0.02), look.z)), uSkyLevels - 4.0);
    color = mix(color, hazeColor, haze);
    outColor = vec4(aces(color * uExposure), 1.0);
}
)";
constexpr const char* depthVertex = R"(#version 300 es
layout(location = 0) in vec3 aPosition;
uniform mat4 uViewProjection;
void main() { gl_Position = uViewProjection * vec4(aPosition, 1.0); }
)";
constexpr const char* depthFragment = R"(#version 300 es
precision mediump float;
void main() {}
)";
constexpr const char* skyVertex = R"(#version 300 es
out vec2 vClip;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
    vClip = p;
    gl_Position = vec4(p, 1.0, 1.0);
}
)";
constexpr const char* skyFragment = R"(#version 300 es
precision highp float;
uniform sampler2D uSky;
uniform mat4 uInverse;
uniform vec3 uEye;
uniform float uExposure;
in vec2 vClip;
out vec4 outColor;
)" SPIDY_SKY_GLSL R"(
void main() {
    vec4 p = uInverse * vec4(vClip, 1.0, 1.0);
    vec3 d = normalize(p.xyz / p.w - uEye);
    // The picture's lower half mirrors its sky: under the horizon, the horizon's haze.
    vec3 c = d.y >= 0.02 ? sky(d, 0.0) : sky(normalize(vec3(d.x, 0.02, d.z)), 4.0);
    outColor = vec4(aces(c * uExposure), 1.0);
}
)";
constexpr const char* overlayVertex = R"(#version 300 es
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;
layout(location = 2) in float aAlpha;
uniform mat4 uViewProjection;
out vec3 vColor;
out float vAlpha;
out vec3 vPosition;
void main() {
    vColor = aColor;
    vAlpha = aAlpha;
    vPosition = aPosition;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)";
constexpr const char* overlayFragment = R"(#version 300 es
precision highp float;
uniform vec3 uEye, uFog;
uniform float uFogDensity;
uniform int uTranslucent;
in vec3 vColor;
in float vAlpha;
in vec3 vPosition;
out vec4 outColor;
void main() {
    if ((uTranslucent == 1) != (vAlpha < 0.999))
        discard;
    const float scale = 140.0;
    float y0 = max(uEye.y, 0.0), y1 = max(vPosition.y, 0.0), dy = y1 - y0;
    float mean = abs(dy) < 0.5 ? exp(-y0 / scale) : (exp(-y0 / scale) - exp(-y1 / scale)) / (dy / scale);
    float haze = 1.0 - exp(-length(vPosition - uEye) * uFogDensity * mean);
    outColor = vec4(mix(vColor, uFog, haze), vAlpha);
}
)";

unsigned compile(GLenum type, const char* source) {
    const unsigned shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096]{};
        glGetShaderInfoLog(shader, sizeof log, nullptr, log);
        glDeleteShader(shader);
        throw std::runtime_error(std::string("City shader: ") + log);
    }
    return shader;
}
unsigned program(const char* vertex, const char* fragment) {
    const unsigned v = compile(GL_VERTEX_SHADER, vertex), f = compile(GL_FRAGMENT_SHADER, fragment);
    const unsigned p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096]{};
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        glDeleteProgram(p);
        throw std::runtime_error(std::string("City program: ") + log);
    }
    return p;
}
int uniform(unsigned p, const char* name) {
    return glGetUniformLocation(p, name);
}
void matrix(unsigned p, const char* name, const Mat4& m) {
    glUniformMatrix4fv(uniform(p, name), 1, GL_TRUE, m.data());
}
void vec3(unsigned p, const char* name, Vec3 v) {
    glUniform3f(uniform(p, name), v.x, v.y, v.z);
}
// A general 4x4 inverse (row-major).
Mat4 inverse(const Mat4& m) {
    Mat4 inv{};
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::abs(det) < 1e-30f)
        return {};
    for (auto& v : inv)
        v /= det;
    return inv;
}
// True when the box is wholly outside one of the matrix's clip planes.
bool outside(const Mat4& m, const Bounds& b) {
    for (int plane = 0; plane < 6; ++plane) {
        const int row = plane / 2;
        const float sign = plane % 2 ? -1.f : 1.f;
        const float a = m[12] + sign * m[row * 4], bb = m[13] + sign * m[row * 4 + 1],
                    c = m[14] + sign * m[row * 4 + 2], d = m[15] + sign * m[row * 4 + 3];
        const Vec3 p{a >= 0 ? b.max.x : b.min.x, bb >= 0 ? b.max.y : b.min.y, c >= 0 ? b.max.z : b.min.z};
        if (a * p.x + bb * p.y + c * p.z + d < 0)
            return true;
    }
    return false;
}
struct Image {
    int width{}, height{};
    std::vector<std::uint8_t> pixels; // RGBA
};
Image decode(const std::vector<std::uint8_t>& bytes) {
    Image out;
    if (bytes.empty())
        return out;
    int channels = 0;
    if (auto* p = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &out.width, &out.height,
                                        &channels, 4)) {
        out.pixels.assign(p, p + static_cast<std::size_t>(out.width) * out.height * 4);
        stbi_image_free(p);
    }
    return out;
}
unsigned levels(int width, int height) {
    unsigned n = 1;
    while ((width | height) >> n)
        ++n;
    return n;
}
unsigned texture(GLenum format, int width, int height, const std::uint8_t* rgba, float anisotropy) {
    unsigned t{};
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexStorage2D(GL_TEXTURE_2D, static_cast<GLsizei>(levels(width, height)), format, width, height);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (anisotropy > 1)
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, anisotropy);
    return t;
}
bool hasExtension(const char* name) {
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i)
        if (const auto* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
            e && std::strcmp(e, name) == 0)
            return true;
    return false;
}
// Flat stand-ins for a material whose photos are missing (linear-ish sRGB bytes).
std::array<std::uint8_t, 4> flatColor(Material m) {
    switch (m) {
    case Material::RedBrick:
    case Material::BrickWindows:
    case Material::BrickPiers:
        return {140, 78, 62, 255};
    case Material::DarkBrick:
        return {92, 60, 52, 255};
    case Material::CleanBrick:
        return {160, 96, 74, 255};
    case Material::Limestone:
    case Material::StonePiers:
        return {176, 168, 150, 255};
    case Material::GlassCurtain:
    case Material::GreenGlass:
    case Material::OfficeGrid:
        return {84, 100, 110, 255};
    case Material::Asphalt:
        return {70, 70, 72, 255};
    case Material::Sidewalk:
        return {160, 156, 148, 255};
    case Material::RoofGravel:
        return {120, 118, 112, 255};
    case Material::Wood:
        return {96, 70, 50, 255};
    default:
        return {150, 150, 150, 255};
    }
}
} // namespace

Mat4 glProjection(EyeFov fov, float nearZ, float farZ) {
    const float l = std::tan(fov.left), r = std::tan(fov.right), d = std::tan(fov.down), u = std::tan(fov.up);
    return {2 / (r - l), 0, (r + l) / (r - l), 0, 0, 2 / (u - d), (u + d) / (u - d), 0, 0, 0,
            -(farZ + nearZ) / (farZ - nearZ), -2 * farZ * nearZ / (farZ - nearZ), 0, 0, -1, 0};
}

Renderer::~Renderer() {
    // The context may be gone already; the app tears it down with the session.
}

void Renderer::initialize(const CityMesh& mesh, const ReadAsset& read,
                          const std::function<void(const std::string&)>& log) {
    cityProgram_ = program(cityVertex, cityFragment);
    shadowProgram_ = program(depthVertex, depthFragment);
    skyProgram_ = program(skyVertex, skyFragment);
    overlayProgram_ = program(overlayVertex, overlayFragment);
    if (hasExtension("GL_EXT_texture_filter_anisotropic")) {
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &anisotropy_);
        anisotropy_ = std::min(anisotropy_, 8.f);
    }
    // The city's one vertex and index buffer.
    glGenVertexArrays(1, &cityArray_);
    glBindVertexArray(cityArray_);
    glGenBuffers(1, &cityBuffer_);
    glBindBuffer(GL_ARRAY_BUFFER, cityBuffer_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(CityVertex)),
                 mesh.vertices.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &cityIndices_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cityIndices_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)),
                 mesh.indices.data(), GL_STATIC_DRAW);
    const auto stride = static_cast<GLsizei>(sizeof(CityVertex));
    const auto at = [](std::size_t offset) { return reinterpret_cast<const void*>(offset); };
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, at(offsetof(CityVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, at(offsetof(CityVertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, at(offsetof(CityVertex, u)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, at(offsetof(CityVertex, contact)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, at(offsetof(CityVertex, tint)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_FALSE, stride, at(offsetof(CityVertex, style)));
    glBindVertexArray(0);
    draws_ = mesh.draws;
    chunks_ = mesh.chunks;
    // Overlay stream.
    glGenVertexArrays(1, &overlayArray_);
    glBindVertexArray(overlayArray_);
    glGenBuffers(1, &overlayBuffer_);
    glBindBuffer(GL_ARRAY_BUFFER, overlayBuffer_);
    const auto overlayStride = static_cast<GLsizei>(sizeof(Vertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, overlayStride, at(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, overlayStride, at(offsetof(Vertex, color)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, overlayStride, at(offsetof(Vertex, alpha)));
    glBindVertexArray(0);
    glGenVertexArrays(1, &skyArray_);
    // Materials: the photo, and its normal, roughness and metalness packed in one.
    for (std::size_t m = 0; m < materialCount; ++m) {
        const std::string folder = std::string("city/") + info(static_cast<Material>(m)).folder + "/";
        const Image color = decode(read(folder + "color.jpg"));
        Textures material;
        if (color.pixels.empty()) {
            log("City: no photo for " + folder + ", drawing it flat");
            const auto flat = flatColor(static_cast<Material>(m));
            const std::uint8_t plain[4] = {128, 128, 200, 0};
            material.albedo = texture(GL_SRGB8_ALPHA8, 1, 1, flat.data(), 0);
            material.surface = texture(GL_RGBA8, 1, 1, plain, 0);
            materials_.push_back(material);
            continue;
        }
        material.albedo = texture(GL_SRGB8_ALPHA8, color.width, color.height, color.pixels.data(), anisotropy_);
        const Image normal = decode(read(folder + "normal.jpg")), rough = decode(read(folder + "roughness.jpg")),
                    metal = decode(read(folder + "metalness.jpg"));
        std::vector<std::uint8_t> packed(color.pixels.size());
        const auto sample = [&](const Image& image, int x, int y, int channel, std::uint8_t otherwise) {
            if (image.pixels.empty())
                return otherwise;
            const int sx = x * image.width / color.width, sy = y * image.height / color.height;
            return image.pixels[(static_cast<std::size_t>(sy) * image.width + sx) * 4 + channel];
        };
        for (int y = 0; y < color.height; ++y)
            for (int x = 0; x < color.width; ++x) {
                auto* p = &packed[(static_cast<std::size_t>(y) * color.width + x) * 4];
                p[0] = sample(normal, x, y, 0, 128);
                p[1] = sample(normal, x, y, 1, 128);
                p[2] = sample(rough, x, y, 0, 200);
                p[3] = sample(metal, x, y, 0, 0);
            }
        material.surface = texture(GL_RGBA8, color.width, color.height, packed.data(), anisotropy_);
        materials_.push_back(material);
    }
    loadSky(read, log);
    // The sun's shadow map.
    glGenTextures(1, &shadowMap_);
    glBindTexture(GL_TEXTURE_2D, shadowMap_);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT24, static_cast<GLsizei>(shadowSize_),
                   static_cast<GLsizei>(shadowSize_));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &shadowFramebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFramebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMap_, 0);
    const GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("City: the shadow map's framebuffer is incomplete");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::loadSky(const ReadAsset& read, const std::function<void(const std::string&)>& log) {
    const auto bytes = read("city/sky.hdr");
    int width = 0, height = 0, channels = 0;
    float* hdr = bytes.empty() ? nullptr
                               : stbi_loadf_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width,
                                                        &height, &channels, 3);
    std::vector<float> level;
    if (hdr) {
        level.assign(hdr, hdr + static_cast<std::size_t>(width) * height * 3);
        stbi_image_free(hdr);
    } else {
        // A plain gradient sky: blue overhead, pale at the horizon.
        log("City: no sky.hdr, drawing a plain sky");
        width = 256;
        height = 128;
        level.resize(static_cast<std::size_t>(width) * height * 3);
        for (int y = 0; y < height; ++y) {
            const float up = std::cos(3.14159265f * (static_cast<float>(y) + .5f) / static_cast<float>(height));
            const float t = std::clamp(up, 0.f, 1.f);
            const Vec3 c = Vec3{.85f, .9f, 1} * (1 - t) + Vec3{.3f, .5f, .9f} * t;
            for (int x = 0; x < width; ++x) {
                const float sun = std::pow(std::max(0.f, std::cos((static_cast<float>(x) / width - .6f) * 6.283f) *
                                                             std::cos((static_cast<float>(y) / height - .27f) * 3.14f)),
                                           4000.f) * 3000;
                float* p = &level[(static_cast<std::size_t>(y) * width + x) * 3];
                p[0] = c.x + sun;
                p[1] = c.y + sun;
                p[2] = c.z + sun;
            }
        }
    }
    glGenTextures(1, &sky_);
    glBindTexture(GL_TEXTURE_2D, sky_);
    skyLevels_ = levels(width, height);
    glTexStorage2D(GL_TEXTURE_2D, static_cast<GLsizei>(skyLevels_), GL_RGB9_E5, width, height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Each level a box filter of the one above; the sun found where a
    // 64-pixel-wide level is brightest, the sky's mean from the upper half.
    double skyMean = 0;
    std::size_t skyCount = 0;
    for (int y = 0; y < height / 2; ++y)
        for (int x = 0; x < width; ++x) {
            const float* p = &level[(static_cast<std::size_t>(y) * width + x) * 3];
            skyMean += std::min(.2126f * p[0] + .7152f * p[1] + .0722f * p[2], 50.f);
            ++skyCount;
        }
    skyMean /= static_cast<double>(std::max<std::size_t>(skyCount, 1));
    int w = width, h = height;
    bool sunFound = false;
    for (unsigned l = 0; l < skyLevels_; ++l) {
        glTexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(l), 0, 0, w, h, GL_RGB, GL_FLOAT, level.data());
        if (!sunFound && w <= 64) {
            float best = -1;
            int bx = 0, by = 0;
            for (int y = 0; y < h / 2; ++y)
                for (int x = 0; x < w; ++x) {
                    const float* p = &level[(static_cast<std::size_t>(y) * w + x) * 3];
                    const float lum = .2126f * p[0] + .7152f * p[1] + .0722f * p[2];
                    if (lum > best) {
                        best = lum;
                        bx = x;
                        by = y;
                    }
                }
            const float u = (static_cast<float>(bx) + .5f) / static_cast<float>(w),
                        v = (static_cast<float>(by) + .5f) / static_cast<float>(h);
            const float theta = v * 3.14159265f, phi = (u - .5f) * 6.2831853f;
            sun_ = normalized(Vec3{std::sin(theta) * std::sin(phi), std::cos(theta), -std::sin(theta) * std::cos(phi)});
            // The sun's light to the sky's, as on a clear-ish day, in the sun's own colour.
            const float* p = &level[(static_cast<std::size_t>(by) * w + bx) * 3];
            const Vec3 c{p[0], p[1], p[2]};
            const float lum = std::max(.2126f * c.x + .7152f * c.y + .0722f * c.z, 1e-4f);
            const Vec3 hue = c * (1 / lum);
            sunColor_ = Vec3{std::clamp(hue.x, .6f, 1.4f), std::clamp(hue.y, .6f, 1.4f), std::clamp(hue.z, .6f, 1.4f)} *
                        static_cast<float>(4.5 * skyMean);
            sunFound = true;
        }
        if (w == 1 && h == 1)
            break;
        const int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
        std::vector<float> next(static_cast<std::size_t>(nw) * nh * 3);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
                for (int c = 0; c < 3; ++c) {
                    float sum = 0;
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx) {
                            const int sx = std::min(x * 2 + dx, w - 1), sy = std::min(y * 2 + dy, h - 1);
                            sum += level[(static_cast<std::size_t>(sy) * w + sx) * 3 + c];
                        }
                    next[(static_cast<std::size_t>(y) * nw + x) * 3 + c] = sum / 4;
                }
        level.swap(next);
        w = nw;
        h = nh;
    }
    if (sun_.y < .15f)
        sun_ = normalized(Vec3{sun_.x, .15f, sun_.z});
    // A display that shows the sky's mean as a mid grey.
    exposure = static_cast<float>(.55 / std::max(skyMean, 1e-3));
    fog_ = Vec3{.62f, .68f, .74f};
}

void Renderer::drawCity(const Mat4& vp, bool shadowPass) {
    glBindVertexArray(cityArray_);
    std::vector<bool> culled(chunks_.size());
    for (std::size_t c = 0; c < chunks_.size(); ++c)
        culled[c] = outside(vp, chunks_[c]);
    int bound = -1;
    for (const auto& d : draws_) {
        if (culled[d.chunk])
            continue;
        if (!shadowPass && static_cast<int>(d.material) != bound) {
            bound = static_cast<int>(d.material);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, materials_[bound].albedo);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, materials_[bound].surface);
        }
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(d.count), GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(static_cast<std::size_t>(d.first) * sizeof(std::uint32_t)));
    }
    glBindVertexArray(0);
}

void Renderer::shadows(Vec3 viewer) {
    // An orthographic view down the sun's rays, centred on the viewer and
    // moved in whole texels so the edges of shadows do not crawl.
    const Vec3 z = sun_, x = normalized(cross(std::abs(sun_.y) > .99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0}, z)),
               y = cross(z, x);
    const float texel = 2 * shadowReach_ / static_cast<float>(shadowSize_);
    Vec3 centre = viewer;
    const float cx = std::round(dot(centre, x) / texel) * texel, cy = std::round(dot(centre, y) / texel) * texel;
    centre = x * cx + y * cy + z * dot(centre, z);
    const Vec3 eye = centre + z * 1200;
    const Mat4 view{x.x, x.y, x.z, -dot(x, eye), y.x, y.y, y.z, -dot(y, eye),
                    z.x, z.y, z.z, -dot(z, eye), 0,   0,   0,   1};
    const float r = shadowReach_, n = 1, f = 2600;
    const Mat4 ortho{1 / r, 0, 0, 0, 0, 1 / r, 0, 0, 0, 0, -2 / (f - n), -(f + n) / (f - n), 0, 0, 0, 1};
    shadowMatrix_ = multiply(ortho, view);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFramebuffer_);
    glViewport(0, 0, static_cast<GLsizei>(shadowSize_), static_cast<GLsizei>(shadowSize_));
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.f, 4.f);
    glUseProgram(shadowProgram_);
    matrix(shadowProgram_, "uViewProjection", shadowMatrix_);
    drawCity(shadowMatrix_, true);
    glDisable(GL_POLYGON_OFFSET_FILL);
}

void Renderer::view(const Mat4& vp, Vec3 eye, unsigned width, unsigned height, std::span<const Vertex> overlay,
                    float seconds) {
    constexpr float fogDensity = .0016f;
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glClearColor(fog_.x, fog_.y, fog_.z, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(cityProgram_);
    matrix(cityProgram_, "uViewProjection", vp);
    matrix(cityProgram_, "uShadow", shadowMatrix_);
    vec3(cityProgram_, "uEye", eye);
    vec3(cityProgram_, "uSun", sun_);
    vec3(cityProgram_, "uSunColor", sunColor_);
    glUniform1f(uniform(cityProgram_, "uSkyLevels"), static_cast<float>(skyLevels_ - 1));
    glUniform1f(uniform(cityProgram_, "uExposure"), exposure);
    glUniform1f(uniform(cityProgram_, "uTime"), seconds);
    glUniform1f(uniform(cityProgram_, "uFogDensity"), fogDensity);
    glUniform1i(uniform(cityProgram_, "uAlbedo"), 0);
    glUniform1i(uniform(cityProgram_, "uSurface"), 1);
    glUniform1i(uniform(cityProgram_, "uSky"), 2);
    glUniform1i(uniform(cityProgram_, "uShadowMap"), 3);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, sky_);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, shadowMap_);
    drawCity(vp, false);
    // The lab's own geometry: solid, then see-through over it.
    if (!overlay.empty()) {
        glBindVertexArray(overlayArray_);
        glBindBuffer(GL_ARRAY_BUFFER, overlayBuffer_);
        const std::size_t bytes = overlay.size() * sizeof(Vertex);
        if (bytes > overlayCapacity_) {
            overlayCapacity_ = bytes * 2;
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(overlayCapacity_), nullptr, GL_STREAM_DRAW);
        }
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), overlay.data());
        glUseProgram(overlayProgram_);
        matrix(overlayProgram_, "uViewProjection", vp);
        vec3(overlayProgram_, "uEye", eye);
        vec3(overlayProgram_, "uFog", fog_);
        glUniform1f(uniform(overlayProgram_, "uFogDensity"), fogDensity);
        glDisable(GL_CULL_FACE);
        glUniform1i(uniform(overlayProgram_, "uTranslucent"), 0);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(overlay.size()));
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glUniform1i(uniform(overlayProgram_, "uTranslucent"), 1);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(overlay.size()));
        glDisable(GL_BLEND);
        glBindVertexArray(0);
    }
    // The sky last, only where nothing else drew.
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glUseProgram(skyProgram_);
    matrix(skyProgram_, "uInverse", inverse(vp));
    vec3(skyProgram_, "uEye", eye);
    glUniform1f(uniform(skyProgram_, "uExposure"), exposure);
    glUniform1i(uniform(skyProgram_, "uSky"), 2);
    glBindVertexArray(skyArray_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
}
} // namespace spidy::city
