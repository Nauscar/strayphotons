#include "InputBindings.hh"

#include "assets/Asset.hh"
#include "assets/AssetManager.hh"
#include "core/Common.hh"
#include "core/Logging.hh"
#include "ecs/EcsImpl.hh"
#include "game/Scene.hh"
#include "game/SceneManager.hh"
#include "input/BindingNames.hh"
#include "xr/openvr/OpenVrSystem.hh"

#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <openvr.h>
#include <picojson/picojson.h>

namespace sp::xr {
    static CVar<int> CVarForceHandPose("vr.ForceHandePose",
        0,
        "0: none, 1: bind pose, 2: open hand, 3: fist, 4: grip limit");

    InputBindings::InputBindings(OpenVrSystem &vrSystem, std::string actionManifestPath) : vrSystem(vrSystem) {
        // TODO: Create .vrmanifest file / register vr manifest with steam:
        // https://github.com/ValveSoftware/openvr/wiki/Action-manifest
        vr::EVRInputError error = vr::VRInput()->SetActionManifestPath(actionManifestPath.c_str());
        Assert(error == vr::EVRInputError::VRInputError_None, "Failed to initialize OpenVR input");

        auto actionManifest = Assets().Load(actionManifestPath, AssetType::External, true)->Get();
        Assertf(actionManifest, "Failed to load vr action manifest: %s", actionManifestPath);

        picojson::value root;
        string err = picojson::parse(root, actionManifest->String());
        if (!err.empty()) {
            Errorf("Failed to parse OpenVR action manifest file: %s", err);
            return;
        }

        auto actionSetList = root.get<picojson::object>()["action_sets"];
        if (actionSetList.is<picojson::array>()) {
            for (auto actionSet : actionSetList.get<picojson::array>()) {
                for (auto param : actionSet.get<picojson::object>()) {
                    if (param.first == "name") {
                        auto name = param.second.get<string>();
                        vr::VRActionSetHandle_t actionSetHandle;
                        error = vr::VRInput()->GetActionSetHandle(name.c_str(), &actionSetHandle);
                        Assertf(error == vr::EVRInputError::VRInputError_None,
                            "Failed to load OpenVR input action set: %s",
                            name);

                        actionSets.emplace_back(name, actionSetHandle);
                    }
                }
            }
        }

        auto actionList = root.get<picojson::object>()["actions"];
        if (actionList.is<picojson::array>()) {
            for (auto actionObj : actionList.get<picojson::array>()) {
                Action action;
                for (auto param : actionObj.get<picojson::object>()) {
                    if (param.first == "name") {
                        action.name = param.second.get<string>();
                        error = vr::VRInput()->GetActionHandle(action.name.c_str(), &action.handle);
                        Assertf(error == vr::EVRInputError::VRInputError_None,
                            "Failed to load OpenVR input action set: %s",
                            action.name);
                    } else if (param.first == "type") {
                        auto typeStr = param.second.get<string>();
                        to_lower(typeStr);
                        if (typeStr == "boolean") {
                            action.type = Action::DataType::Bool;
                        } else if (typeStr == "vector1") {
                            action.type = Action::DataType::Vec1;
                        } else if (typeStr == "vector2") {
                            action.type = Action::DataType::Vec2;
                        } else if (typeStr == "vector3") {
                            action.type = Action::DataType::Vec3;
                        } else if (typeStr == "vibration") {
                            action.type = Action::DataType::Haptic;
                        } else if (typeStr == "pose") {
                            action.type = Action::DataType::Pose;
                        } else if (typeStr == "skeleton") {
                            action.type = Action::DataType::Skeleton;
                        } else {
                            Errorf("OpenVR action manifest contains unknown action type: %s", typeStr);
                        }
                    }
                }
                if (!action.name.empty() && action.handle) {
                    auto it = std::find_if(actionSets.begin(), actionSets.end(), [&action](auto &set) {
                        return starts_with(action.name, set.name);
                    });
                    if (it != actionSets.end()) {
                        it->actions.push_back(action);
                    } else {
                        Logf("OpenVR Action has unknown set: %s", action.name);
                    }
                }
            }
        }

        GetSceneManager().QueueActionAndBlock(SceneAction::ApplySystemScene,
            "vr_input",
            [this](ecs::Lock<ecs::AddRemove> lock, std::shared_ptr<Scene> scene) {
                for (auto &actionSet : actionSets) {
                    for (auto &action : actionSet.actions) {
                        if (action.type == Action::DataType::Pose || action.type == Action::DataType::Skeleton) {
                            auto inputName = "vr" + action.name;
                            std::transform(inputName.begin(), inputName.end(), inputName.begin(), [](unsigned char c) {
                                return (c == ':' || c == '/') ? '_' : tolower(c);
                            });
                            action.poseEntity = ecs::Name("input", inputName);
                            auto ent = scene->NewSystemEntity(lock, scene, action.poseEntity.Name());
                            ent.Set<ecs::TransformTree>(lock);
                        }
                    }
                }
            });
    }

    InputBindings::~InputBindings() {
        GetSceneManager().QueueActionAndBlock(SceneAction::RemoveScene, "vr_input");
    }

    void InputBindings::Frame() {
        std::array<vr::VRInputValueHandle_t, vr::k_unMaxTrackedDeviceCount> origins;

        static const std::array curlSuffix = {"_curl_thumb",
            "_curl_index",
            "_curl_middle",
            "_curl_ring",
            "_curl_pinky"};
        static const std::array splaySuffix = {"_splay_thumb_index",
            "_splay_index_middle",
            "_splay_middle_ring",
            "_splay_ring_pinky"};

        bool missingEntities = false;
        {
            ZoneScopedN("InputBindings Sync to ECS");
            auto lock = ecs::StartTransaction<ecs::SendEventsLock, ecs::Write<ecs::Signals, ecs::TransformTree>>();

            for (auto &actionSet : actionSets) {
                vr::VRActiveActionSet_t activeActionSet = {};
                activeActionSet.ulActionSet = actionSet.handle;

                vr::EVRInputError error = vr::VRInput()->UpdateActionState(&activeActionSet,
                    sizeof(vr::VRActiveActionSet_t),
                    1);
                Assertf(error == vr::EVRInputError::VRInputError_None,
                    "Failed to sync OpenVR actions for: %s",
                    actionSet.name);

                for (auto &action : actionSet.actions) {
                    error = vr::VRInput()->GetActionOrigins(actionSet.handle,
                        action.handle,
                        origins.data(),
                        origins.size());
                    Assertf(error == vr::EVRInputError::VRInputError_None,
                        "Failed to read OpenVR action sources for: %s",
                        action.name);

                    for (auto &originHandle : origins) {
                        if (!originHandle) continue;
                        vr::InputOriginInfo_t originInfo;
                        error = vr::VRInput()->GetOriginTrackedDeviceInfo(originHandle,
                            &originInfo,
                            sizeof(originInfo));
                        Assert(error == vr::EVRInputError::VRInputError_None, "Failed to read origin info");

                        std::string actionSignal = action.name;
                        std::transform(actionSignal.begin(),
                            actionSignal.end(),
                            actionSignal.begin(),
                            [](unsigned char c) {
                                return (c == ':' || c == '/') ? '_' : tolower(c);
                            });
                        if (actionSignal.size() > 0 && actionSignal.at(0) == '_') {
                            actionSignal = actionSignal.substr(1);
                        }

                        auto originEntity = vrSystem.GetEntityForDeviceIndex(originInfo.trackedDeviceIndex);
                        if (!originEntity) continue;

                        vr::InputDigitalActionData_t digitalActionData;
                        vr::InputAnalogActionData_t analogActionData;
                        vr::InputPoseActionData_t poseActionData;
                        vr::VRSkeletalSummaryData_t skeletalSummaryData;
                        vr::InputSkeletalActionData_t skeletalActionData;
                        switch (action.type) {
                        case Action::DataType::Bool:
                            error = vr::VRInput()->GetDigitalActionData(action.handle,
                                &digitalActionData,
                                sizeof(vr::InputDigitalActionData_t),
                                originInfo.devicePath);
                            Assertf(error == vr::EVRInputError::VRInputError_None,
                                "Failed to read OpenVR digital action: %s",
                                action.name);

                            if (digitalActionData.bActive && digitalActionData.bChanged) {
                                ecs::EventBindings::SendEvent(lock,
                                    originEntity,
                                    ecs::Event{action.name, originEntity, digitalActionData.bState});
                            }

                            if (digitalActionData.bActive) {
                                ecs::SignalRef(originEntity, actionSignal).SetValue(lock, digitalActionData.bState);
                            } else {
                                ecs::SignalRef(originEntity, actionSignal).ClearValue(lock);
                            }
                            break;
                        case Action::DataType::Vec1:
                        case Action::DataType::Vec2:
                        case Action::DataType::Vec3:
                            error = vr::VRInput()->GetAnalogActionData(action.handle,
                                &analogActionData,
                                sizeof(vr::InputAnalogActionData_t),
                                originInfo.devicePath);
                            Assertf(error == vr::EVRInputError::VRInputError_None,
                                "Failed to read OpenVR analog action: %s",
                                action.name);

                            if (analogActionData.bActive && (analogActionData.x != 0.0f || analogActionData.y != 0.0f ||
                                                                analogActionData.z != 0.0f)) {
                                switch (action.type) {
                                case Action::DataType::Vec1:
                                    ecs::EventBindings::SendEvent(lock,
                                        originEntity,
                                        ecs::Event{action.name, originEntity, analogActionData.x});
                                    break;
                                case Action::DataType::Vec2:
                                    ecs::EventBindings::SendEvent(lock,
                                        originEntity,
                                        ecs::Event{action.name,
                                            originEntity,
                                            glm::vec2(analogActionData.x, analogActionData.y)});
                                    break;
                                case Action::DataType::Vec3:
                                    ecs::EventBindings::SendEvent(lock,
                                        originEntity,
                                        ecs::Event{action.name,
                                            originEntity,
                                            glm::vec3(analogActionData.x, analogActionData.y, analogActionData.z)});
                                    break;
                                default:
                                    break;
                                }
                            }

                            if (analogActionData.bActive) {
                                switch (action.type) {
                                case Action::DataType::Vec3:
                                    ecs::SignalRef(originEntity, actionSignal + ".z")
                                        .SetValue(lock, analogActionData.z);
                                case Action::DataType::Vec2:
                                    ecs::SignalRef(originEntity, actionSignal + ".y")
                                        .SetValue(lock, analogActionData.y);
                                case Action::DataType::Vec1:
                                    ecs::SignalRef(originEntity, actionSignal + ".x")
                                        .SetValue(lock, analogActionData.x);
                                    break;
                                default:
                                    break;
                                }
                            } else {
                                ecs::SignalRef(originEntity, actionSignal + ".x").ClearValue(lock);
                                ecs::SignalRef(originEntity, actionSignal + ".y").ClearValue(lock);
                                ecs::SignalRef(originEntity, actionSignal + ".z").ClearValue(lock);
                            }
                            break;
                        case Action::DataType::Pose:
                            error = vr::VRInput()->GetPoseActionDataForNextFrame(action.handle,
                                vr::ETrackingUniverseOrigin::TrackingUniverseStanding,
                                &poseActionData,
                                sizeof(vr::InputPoseActionData_t),
                                originInfo.devicePath);
                            Assertf(error == vr::EVRInputError::VRInputError_None,
                                "Failed to read OpenVR pose action: %s",
                                action.name);

                            if (poseActionData.bActive) {
                                if (poseActionData.pose.bDeviceIsConnected && poseActionData.pose.bPoseIsValid) {
                                    ecs::Entity poseEntity = action.poseEntity.Get(lock);
                                    if (poseEntity.Has<ecs::TransformTree>(lock)) {
                                        auto &transform = poseEntity.Get<ecs::TransformTree>(lock);

                                        auto &poseMat = poseActionData.pose.mDeviceToAbsoluteTracking.m;
                                        transform.pose = glm::mat4(glm::transpose(glm::make_mat3x4((float *)poseMat)));
                                        transform.parent = vrSystem.vrOriginEntity;
                                    }
                                }
                            }
                            break;
                        case Action::DataType::Skeleton:
                            error = vr::VRInput()->GetSkeletalSummaryData(action.handle,
                                vr::EVRSummaryType::VRSummaryType_FromAnimation,
                                &skeletalSummaryData);
                            Assertf(error == vr::EVRInputError::VRInputError_None,
                                "Failed to read OpenVR skeletal summary for action: %s",
                                action.name);

                            error = vr::VRInput()->GetSkeletalActionData(action.handle,
                                &skeletalActionData,
                                sizeof(vr::InputSkeletalActionData_t));
                            Assertf(error == vr::EVRInputError::VRInputError_None,
                                "Failed to read OpenVR skeleton action: %s",
                                action.name);

                            for (size_t i = 0; i < vr::VRFinger_Count; i++) {
                                if (skeletalActionData.bActive) {
                                    ecs::SignalRef(originEntity, actionSignal + curlSuffix[i])
                                        .SetValue(lock, skeletalSummaryData.flFingerCurl[i]);
                                } else {
                                    ecs::SignalRef(originEntity, actionSignal + curlSuffix[i]).ClearValue(lock);
                                }
                            }
                            for (size_t i = 0; i < vr::VRFingerSplay_Count; i++) {
                                if (skeletalActionData.bActive) {
                                    ecs::SignalRef(originEntity, actionSignal + splaySuffix[i])
                                        .SetValue(lock, skeletalSummaryData.flFingerSplay[i]);
                                } else {
                                    ecs::SignalRef(originEntity, actionSignal + splaySuffix[i]).ClearValue(lock);
                                }
                            }

                            if (skeletalActionData.bActive) {
                                error = vr::VRInput()->GetPoseActionDataForNextFrame(action.handle,
                                    vr::ETrackingUniverseOrigin::TrackingUniverseStanding,
                                    &poseActionData,
                                    sizeof(vr::InputPoseActionData_t),
                                    vr::k_ulInvalidInputValueHandle);
                                Assertf(error == vr::EVRInputError::VRInputError_None,
                                    "Failed to read OpenVR pose action: %s",
                                    action.name);

                                if (poseActionData.bActive && poseActionData.pose.bDeviceIsConnected &&
                                    poseActionData.pose.bPoseIsValid) {
                                    ecs::Entity poseEntity = action.poseEntity.Get(lock);
                                    if (poseEntity.Has<ecs::TransformTree>(lock)) {
                                        auto &transform = poseEntity.Get<ecs::TransformTree>(lock);

                                        auto &poseMat = poseActionData.pose.mDeviceToAbsoluteTracking.m;
                                        transform.pose = glm::mat4(glm::transpose(glm::make_mat3x4((float *)poseMat)));
                                        transform.parent = vrSystem.vrOriginEntity;
                                    }

                                    uint32_t boneCount = 0;
                                    error = vr::VRInput()->GetBoneCount(action.handle, &boneCount);
                                    if (error != vr::EVRInputError::VRInputError_None) {
                                        Errorf("Failed to get bone count for action skeleton: %s", action.name);
                                        continue;
                                    }

                                    std::vector<vr::VRBoneTransform_t> boneTransforms(boneCount);
                                    if (CVarForceHandPose.Get() >= 1 && CVarForceHandPose.Get() <= 4) {
                                        error = vr::VRInput()->GetSkeletalReferenceTransforms(action.handle,
                                            vr::VRSkeletalTransformSpace_Parent,
                                            (vr::EVRSkeletalReferencePose)(CVarForceHandPose.Get() - 1),
                                            boneTransforms.data(),
                                            boneTransforms.size());
                                    } else {
                                        error = vr::VRInput()->GetSkeletalBoneData(action.handle,
                                            vr::VRSkeletalTransformSpace_Parent,
                                            vr::VRSkeletalMotionRange_WithoutController,
                                            boneTransforms.data(),
                                            boneTransforms.size());
                                    }
                                    Assertf(error == vr::EVRInputError::VRInputError_None,
                                        "Failed to read OpenVR bone transforms for action: %s",
                                        action.name);

                                    action.boneEntities.resize(boneCount);
                                    action.boneHierarchy.resize(boneCount);

                                    error = vr::VRInput()->GetBoneHierarchy(action.handle,
                                        action.boneHierarchy.data(),
                                        action.boneHierarchy.size());
                                    Assertf(error == vr::EVRInputError::VRInputError_None,
                                        "Failed to read OpenVR bone hierarchy for action: %s",
                                        action.name);

                                    for (size_t i = 0; i < boneCount; i++) {
                                        std::array<char, vr::k_unMaxBoneNameLength> boneNameChars;
                                        error = vr::VRInput()->GetBoneName(action.handle,
                                            i,
                                            boneNameChars.data(),
                                            boneNameChars.size());
                                        Assertf(error == vr::EVRInputError::VRInputError_None,
                                            "Failed to read OpenVR bone name %d for action: %s",
                                            i,
                                            action.name);

                                        std::string boneName(boneNameChars.begin(),
                                            std::find(boneNameChars.begin(), boneNameChars.end(), '\0'));
                                        auto entityName = action.poseEntity.Name();
                                        entityName.entity += "." + boneName;

                                        if (action.boneEntities[i].Name() != entityName) {
                                            action.boneEntities[i] = entityName;
                                            missingEntities = true;
                                        }
                                    }

                                    for (size_t i = 0; i < boneCount; i++) {
                                        ecs::Entity boneEntity = action.boneEntities[i].Get(lock);
                                        if (boneEntity.Has<ecs::TransformTree>(lock)) {
                                            auto &transform = boneEntity.Get<ecs::TransformTree>(lock);

                                            transform.pose.SetRotation(glm::quat(boneTransforms[i].orientation.w,
                                                boneTransforms[i].orientation.x,
                                                boneTransforms[i].orientation.y,
                                                boneTransforms[i].orientation.z));
                                            transform.pose.SetPosition(glm::make_vec3(boneTransforms[i].position.v));

                                            if ((size_t)action.boneHierarchy[i] < action.boneEntities.size()) {
                                                transform.parent = action.boneEntities[action.boneHierarchy[i]];
                                            } else {
                                                transform.parent = poseEntity;
                                            }
                                        }
                                    }
                                }
                            }
                            break;
                        default:
                            break;
                        }
                    }
                }
            }
        }

        if (missingEntities) {
            ZoneScopedN("InputBindings::AddMissingEntities");
            GetSceneManager().QueueActionAndBlock(SceneAction::ApplySystemScene,
                "vr_input",
                [this](ecs::Lock<ecs::AddRemove> lock, std::shared_ptr<Scene> scene) {
                    for (auto &actionSet : actionSets) {
                        for (auto &action : actionSet.actions) {
                            if (action.type == Action::DataType::Skeleton) {
                                for (auto &boneEnt : action.boneEntities) {
                                    if (scene->GetStagingEntity(boneEnt.Name())) continue;
                                    auto ent = scene->NewSystemEntity(lock, scene, boneEnt.Name());
                                    ent.Set<ecs::TransformTree>(lock);
                                }
                            }
                        }
                    }
                });
        }
    }
} // namespace sp::xr
