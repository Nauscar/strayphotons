#pragma once

#include "Common.hh"
#include "graphics/vulkan/scene/GPUScene.hh"

namespace sp::vulkan::renderer {
    static const uint32 MAX_VOXEL_FRAGMENT_LISTS = 16;

    class Lighting;

    struct VoxelLayerInfo {
        std::string name;
        uint32_t layerIndex;
        uint32_t dirIndex;
    };

    class Voxels {
    public:
        Voxels(GPUScene &scene) : scene(scene) {}
        void LoadState(RenderGraph &graph, ecs::Lock<ecs::Read<ecs::VoxelArea, ecs::TransformSnapshot>> lock);

        void AddVoxelization(RenderGraph &graph, const Lighting &lighting);
        void AddVoxelization2(RenderGraph &graph, const Lighting &lighting);
        void AddDebugPass(RenderGraph &graph);

    private:
        GPUScene &scene;

        struct FragmentListSize {
            uint32 capacity, offset;
        };
        std::array<FragmentListSize, MAX_VOXEL_FRAGMENT_LISTS> fragmentListSizes;
        uint32 fragmentListCount;

        ecs::Transform voxelToWorld;
        glm::ivec3 voxelGridSize;
        uint32_t voxelLayerCount;

        static inline const std::array<glm::vec3, 6> directions = {
            glm::vec3(1, 0, 0),
            glm::vec3(0, 1, 0),
            glm::vec3(0, 0, 1),
            glm::vec3(-1, 0, 0),
            glm::vec3(0, -1, 0),
            glm::vec3(0, 0, -1),
        };
        static inline const size_t layerCount = directions.size() * MAX_VOXEL_FRAGMENT_LISTS;

        static inline std::array<VoxelLayerInfo, layerCount> generateVoxelLayerInfo() {
            std::array<VoxelLayerInfo, layerCount> layers;
            for (uint32_t i = 0; i < MAX_VOXEL_FRAGMENT_LISTS; i++) {
                for (uint32_t dir = 0; dir < directions.size(); dir++) {
                    layers[i * directions.size() + dir] = VoxelLayerInfo{
                        "VoxelLayer" + std::to_string(i) + "_" + std::to_string(dir),
                        i,
                        dir,
                    };
                }
            }
            return layers;
        }

    public:
        static inline const auto VoxelLayers = generateVoxelLayerInfo();
    };
} // namespace sp::vulkan::renderer
