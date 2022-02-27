#pragma once

#include <ecs/CHelpers.h>

#ifdef __cplusplus
namespace ecs {
    extern "C" {
#endif

    typedef struct Transform {
        GlmMat4x3 matrix;

#ifdef __cplusplus
    #ifndef SP_WASM_BUILD
        Transform() {}
        Transform(const glm::mat4x3 &matrix) : matrix(matrix) {}
        Transform(glm::vec3 pos, glm::quat orientation = glm::identity<glm::quat>());

        void Translate(const glm::vec3 &xyz);
        void Rotate(float radians, const glm::vec3 &axis);
        void Scale(const glm::vec3 &xyz);

        void SetPosition(const glm::vec3 &pos);
        void SetRotation(const glm::quat &quat);
        void SetScale(const glm::vec3 &xyz);

        const glm::vec3 &GetPosition() const;
        glm::quat GetRotation() const;
        glm::vec3 GetScale() const;
        glm::vec3 GetForward() const;
    #endif
#endif
    } Transform;

    // C accessors
    void transform_identity(Transform *out);
    void transform_from_pos(Transform *out, const GlmVec3 *pos);

    void transform_translate(Transform *t, const GlmVec3 *xyz);
    void transform_rotate(Transform *t, float radians, const GlmVec3 *axis);
    void transform_scale(Transform *t, const GlmVec3 *xyz);

    void transform_set_position(Transform *t, const GlmVec3 *pos);
    void transform_set_rotation(Transform *t, const GlmQuat *quat);
    void transform_set_scale(Transform *t, const GlmVec3 *xyz);

    void transform_get_position(GlmVec3 *out, const Transform *t);
    void transform_get_rotation(GlmQuat *out, const Transform *t);
    void transform_get_scale(GlmVec3 *out, const Transform *t);

    typedef Transform TransformSnapshot;

    typedef struct TransformTree {
        Transform pose;
        TecsEntity parent;

#ifdef __cplusplus
    #ifndef SP_WASM_BUILD
        TransformTree() {}
        TransformTree(const glm::mat4x3 &pose) : pose(pose) {}
        TransformTree(const Transform &pose) : pose(pose) {}
        TransformTree(glm::vec3 pos, glm::quat orientation = glm::identity<glm::quat>()) : pose(pos, orientation) {}

        // Returns a flattened Transform that includes all parent transforms.
        Transform GetGlobalTransform(Lock<Read<TransformTree>> lock) const;
        glm::quat GetGlobalRotation(Lock<Read<TransformTree>> lock) const;
    #endif
#endif
    } TransformTree;

#ifdef __cplusplus
    // If this changes, make sure it is the same in Rust and WASM
    static_assert(sizeof(Transform) == 48, "Wrong Transform size");
    } // extern "C"

    #ifndef SP_WASM_BUILD
    static Component<TransformTree> ComponentTransformTree("transform");

    template<>
    bool Component<Transform>::Load(sp::Scene *scene, Transform &dst, const picojson::value &src);
    template<>
    bool Component<TransformTree>::Load(sp::Scene *scene, TransformTree &dst, const picojson::value &src);
    template<>
    void Component<TransformTree>::ApplyComponent(Lock<ReadAll> src, Entity srcEnt, Lock<AddRemove> dst, Entity dstEnt);
    template<>
    void Component<TransformTree>::Apply(const TransformTree &src, Lock<AddRemove> lock, Entity dst);
    #endif
} // namespace ecs
#endif
