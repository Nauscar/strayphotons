#pragma once

#include <ecs/Components.hh>

namespace ecs {
    struct Owner {
        enum class SystemId : size_t { INVALID = 0, SCENE_MANAGER, INPUT_MANAGER, XR_MANAGER, GUI_MANAGER };
        enum class OwnerType { INVALID = 0, SYSTEM, PLAYER, SCENE };

        Owner() : id(0), type(OwnerType::INVALID) {}
        Owner(SystemId id) : id((size_t)id), type(OwnerType::SYSTEM) {}
        Owner(OwnerType type, size_t id) : id(id), type(type) {}

        inline bool operator==(const Owner &other) const {
            return type == other.type && id == other.id;
        }

        size_t id;
        OwnerType type;
    };

    static Component<Owner> ComponentCreator("owner");
} // namespace ecs
