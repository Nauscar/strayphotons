#pragma once

#include "core/Common.hh"
#include "ecs/Components.hh"
#include "ecs/Ecs.hh"

#include <glm/glm.hpp>

namespace ecs {
    struct LaserEmitter {
        float intensity = 1.0f; // multiplier applied to color to produce the final luminance
        sp::color_t color = glm::vec3(0); // HDR value, added to laser_color_* signal
        bool on = true;
    };

    static Component<LaserEmitter> ComponentLaserEmitter("laser_emitter",
        ComponentField::New("insensity", &LaserEmitter::intensity),
        ComponentField::New("color", &LaserEmitter::color),
        ComponentField::New("on", &LaserEmitter::on));
} // namespace ecs
