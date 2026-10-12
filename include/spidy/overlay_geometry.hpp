#pragma once
#include "math.hpp"
#include "vertex.hpp"
#include <vector>

namespace spidy {
// Flat-shaded overlay shapes in the Vertex stream every renderer draws: a box
// (each face a little lighter or darker, so its edges read without lighting),
// a square beam from a to b, and Spider-Man's glove on a tracked grip.
void addBox(std::vector<Vertex>& out, Vec3 min, Vec3 max, Vec3 color);
void addBeam(std::vector<Vertex>& out, Vec3 a, Vec3 b, float radius, Vec3 color);
void addTrackedHand(std::vector<Vertex>& out, Pose grip, unsigned hand, float squeeze, bool webGesture);
} // namespace spidy
