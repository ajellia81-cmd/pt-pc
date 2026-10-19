#pragma once

#include <glm/gtc/matrix_transform.hpp>

namespace pt {

inline glm::mat4 PointShadowView(glm::vec3 position) {
    return glm::translate(glm::mat4(1.0f), -position);
}

}
