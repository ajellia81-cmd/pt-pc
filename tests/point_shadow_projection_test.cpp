#include <array>
#include <cmath>
#include <cstdio>

#include "engine/render/point_shadow_projection.h"

namespace {

int failures = 0;
int checks = 0;

void Check(bool value, const char* description) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL: %s\n", description);
    }
}

glm::vec3 Paraboloid(glm::vec3 relative, bool receiver, bool native_caster) {
    const float distance = glm::length(relative);
    const float shrink = receiver || native_caster ? 2046.0f / 2048.0f : 1.0f;
    const glm::vec2 uv = shrink * glm::vec2(-relative.x, relative.y) / (distance + std::abs(relative.z));
    const float k = 3.0f * 2048.0f / 2046.0f;
    return glm::vec3(uv, 1.0f - distance / k);
}

bool TriangleShadows(const std::array<glm::vec3, 3>& triangle, glm::vec3 receiver) {
    const glm::vec2 a(triangle[0]), b(triangle[1]), c(triangle[2]), q(receiver);
    const float denominator = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    const float w0 = ((b.y - c.y) * (q.x - c.x) + (c.x - b.x) * (q.y - c.y)) / denominator;
    const float w1 = ((c.y - a.y) * (q.x - c.x) + (a.x - c.x) * (q.y - c.y)) / denominator;
    const float w2 = 1.0f - w0 - w1;
    return w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f && receiver.z < w0 * triangle[0].z + w1 * triangle[1].z + w2 * triangle[2].z;
}

}

int main() {
    const glm::vec3 light(-8.3516644f, 2.3337026f, 25.6185924f);
    const glm::mat4 view = pt::PointShadowView(light);
    for (const glm::vec3 delta : std::array<glm::vec3, 8>{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {-1, -2, -3},
                                                        {2, 3, -4}, {3, -4, 2}, {0.25f, -0.8f, -1.4f}, {-0.5f, -0.5f, 0.001f}}}) {
        Check(glm::length(glm::vec3(view * glm::vec4(light + delta, 1.0f)) - delta) < 3e-6f,
              "point caster and receiver space is the native world-axis difference");
    }
    Check(glm::length(glm::vec3(view * glm::vec4(light, 1.0f))) < 3e-6f, "the light is the projection origin");
    Check(std::abs(glm::determinant(view) - 1.0f) < 1e-6f, "world handedness is retained");

    const std::array<glm::vec3, 3> vertices{{{-7.8064374f, 1.5225631f, 24.2402731f},
                                           {-8.8004017f, 1.5594401f, 24.2402734f},
                                           {-7.8433143f, 1.5594401f, 24.2402734f}}};
    const glm::vec3 receiver(-7.9736941f, 1.4765711f, 24.1752519f);
    std::array<glm::vec3, 3> expected, actual;
    for (size_t i = 0; i < vertices.size(); ++i) {
        expected[i] = Paraboloid(vertices[i] - light, false, true);
        actual[i] = Paraboloid(glm::vec3(view * glm::vec4(vertices[i], 1.0f)), false, false);
    }
    Check(!TriangleShadows(expected, Paraboloid(receiver - light, true, true)), "native primitive49 leaves the marked wall receiver lit");
    Check(!TriangleShadows(actual, Paraboloid(glm::vec3(view * glm::vec4(receiver, 1.0f)), true, false)),
          "point projection does not add the lower-rail depth interpolation shadow");
    std::printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
