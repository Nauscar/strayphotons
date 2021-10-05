#include "Renderer.hh"

#include "assets/Model.hh"
#include "core/Logging.hh"
#include "ecs/EcsImpl.hh"
#include "graphics/vulkan/GuiRenderer.hh"
#include "graphics/vulkan/core/CommandContext.hh"
#include "graphics/vulkan/core/DeviceContext.hh"
#include "graphics/vulkan/core/Image.hh"
#include "graphics/vulkan/core/Model.hh"
#include "graphics/vulkan/core/RenderGraph.hh"
#include "graphics/vulkan/core/Screenshot.hh"
#include "graphics/vulkan/core/Vertex.hh"

#ifdef SP_XR_SUPPORT
    #include "xr/XrSystem.hh"
#endif

namespace sp::vulkan {
    static CVar<string> CVarWindowViewTarget("r.WindowViewTarget", "GBuffer0", "Primary window's render target");
    static CVar<bool> CVarEnableShadows("r.EnableShadows", true, "Enable shadow mapping");

    Renderer::Renderer(DeviceContext &device) : device(device) {
        funcs.Register<string>("screenshot",
                               "Save screenshot to <path>, optionally specifying an image <resource>",
                               [&](string pathAndResource) {
                                   std::istringstream ss(pathAndResource);
                                   string path, resource;
                                   ss >> path >> resource;
                                   TriggerScreenshot(path, resource);
                               });
    }

    Renderer::~Renderer() {
        device->waitIdle();
    }

    void Renderer::LoadLightState(ecs::Lock<ecs::Read<ecs::Light, ecs::Transform>> lock) {
        int lightCount = 0;
        glm::ivec2 renderTargetSize(0, 0);
        for (auto entity : lock.EntitiesWith<ecs::Light>()) {
            if (!entity.Has<ecs::Light, ecs::Transform>(lock)) continue;

            auto &light = entity.Get<ecs::Light>(lock);
            if (!light.on) continue;

            int extent = (int)std::pow(2, light.shadowMapSize);

            auto &transform = entity.Get<ecs::Transform>(lock);
            auto &view = lights.views[lightCount];

            view.visibilityMask.set(ecs::Renderable::VISIBLE_LIGHTING_SHADOW);
            view.extents = {extent, extent};
            view.fov = light.spotAngle * 2.0f;
            view.offset = {renderTargetSize.x, 0};
            view.clearMode.reset();
            view.clip = light.shadowMapClip;
            view.UpdateProjectionMatrix();
            view.UpdateViewMatrix(lock, entity);

            auto &data = lights.gpuData[lightCount];
            data.position = transform.GetGlobalPosition(lock);
            data.tint = light.tint;
            data.direction = transform.GetGlobalForward(lock);
            data.spotAngleCos = cos(light.spotAngle);
            data.proj = view.projMat;
            data.invProj = view.invProjMat;
            data.view = view.viewMat;
            data.clip = view.clip;
            data.mapOffset = {renderTargetSize.x, 0, extent, extent};
            data.intensity = light.intensity;
            data.illuminance = light.illuminance;
            data.gelId = light.gelId;

            renderTargetSize.x += extent;
            if (extent > renderTargetSize.y) renderTargetSize.y = extent;
            if (++lightCount >= MAX_LIGHTS) break;
        }
        glm::vec4 mapOffsetScale(renderTargetSize, renderTargetSize);
        for (int i = 0; i < lightCount; i++) {
            lights.gpuData[i].mapOffset /= mapOffsetScale;
        }
        lights.renderTargetSize = renderTargetSize;
        lights.count = lightCount;
    }

    void Renderer::RenderFrame() {
        RenderGraph graph(device);

        auto lock = ecs::World.StartTransaction<
            ecs::Read<ecs::Name, ecs::Transform, ecs::Light, ecs::Renderable, ecs::View, ecs::XRView, ecs::Mirror>>();

        LoadLightState(lock);
        RenderShadowMaps(graph, lock);

        Tecs::Entity windowEntity = device.GetActiveView();

        if (windowEntity && windowEntity.Has<ecs::View>(lock)) {
            auto view = windowEntity.Get<ecs::View>(lock);
            view.UpdateViewMatrix(lock, windowEntity);

            graph.AddPass(
                "ForwardPass",
                [&](RenderGraphPassBuilder &builder) {
                    RenderTargetDesc desc;
                    desc.extent = vk::Extent3D(view.extents.x, view.extents.y, 1);

                    desc.format = vk::Format::eR8G8B8A8Srgb;
                    desc.usage = vk::ImageUsageFlagBits::eColorAttachment;
                    builder.OutputRenderTarget("GBuffer0", desc);

                    desc.format = vk::Format::eD24UnormS8Uint;
                    desc.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
                    builder.OutputRenderTarget("GBufferDepth", desc);
                },
                [this, view, lock](RenderGraphResources &resources, CommandContext &cmd) {
                    auto gBuffer0 = resources.GetRenderTarget("GBuffer0")->ImageView();
                    auto depth = resources.GetRenderTarget("GBufferDepth")->ImageView();

                    RenderPassInfo info;
                    glm::vec4 clear = {0.0f, 1.0f, 0.0f, 1.0f};
                    info.PushColorAttachment(gBuffer0, LoadOp::Clear, StoreOp::Store, clear);
                    info.SetDepthStencilAttachment(depth, LoadOp::Clear, StoreOp::DontCare);
                    cmd.BeginRenderPass(info);

                    ViewStateUniforms viewState;
                    viewState.view[0] = view.viewMat;
                    viewState.projection[0] = view.projMat;
                    cmd.UploadUniformData(0, 10, &viewState);

                    cmd.SetShaders("test.vert", "test.frag");
                    this->ForwardPass(cmd, view.visibilityMask, lock, [](auto lock, Tecs::Entity &ent) {});

                    cmd.EndRenderPass();
                });
        }

#ifdef SP_XR_SUPPORT
        auto xrViews = lock.EntitiesWith<ecs::XRView>();
        if (xrSystem && xrViews.size() > 0) {
            ecs::View firstView;
            for (auto &ent : xrViews) {
                if (ent.Has<ecs::View>(lock)) {
                    auto &view = ent.Get<ecs::View>(lock);

                    if (firstView.extents == glm::ivec2(0)) {
                        firstView = view;
                    } else if (firstView.extents != view.extents) {
                        Abort("All XR views must have the same extents");
                    }
                }
            }

            xrRenderPoses.resize(xrViews.size());
            auto forwardPassViewMask = firstView.visibilityMask;

            graph.AddPass(
                "XRForwardPass",
                [&](RenderGraphPassBuilder &builder) {
                    RenderTargetDesc targetDesc;
                    targetDesc.extent = vk::Extent3D(firstView.extents.x, firstView.extents.y, 1);
                    targetDesc.arrayLayers = xrViews.size();

                    targetDesc.format = vk::Format::eR8G8B8A8Srgb;
                    targetDesc.usage = vk::ImageUsageFlagBits::eColorAttachment;
                    builder.OutputRenderTarget("XRColor", targetDesc);

                    targetDesc.format = vk::Format::eD24UnormS8Uint;
                    targetDesc.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
                    builder.OutputRenderTarget("XRDepth", targetDesc);
                },
                [this, xrViews, forwardPassViewMask, lock](RenderGraphResources &resources, CommandContext &cmd) {
                    auto xrColor = resources.GetRenderTarget("XRColor")->ImageView();
                    auto xrDepth = resources.GetRenderTarget("XRDepth")->ImageView();

                    RenderPassInfo renderPassInfo;
                    renderPassInfo.PushColorAttachment(xrColor,
                                                       LoadOp::Clear,
                                                       StoreOp::Store,
                                                       {0.0f, 1.0f, 0.0f, 1.0f});

                    renderPassInfo.SetDepthStencilAttachment(xrDepth, LoadOp::Clear, StoreOp::DontCare);

                    cmd.BeginRenderPass(renderPassInfo);

                    auto viewState = cmd.AllocUniformData<ViewStateUniforms>(0, 10);

                    cmd.SetShaders("test.vert", "test.frag");
                    this->ForwardPass(cmd, forwardPassViewMask, lock, [](auto lock, Tecs::Entity &ent) {});

                    cmd.EndRenderPass();

                    for (size_t i = 0; i < xrViews.size(); i++) {
                        if (!xrViews[i].Has<ecs::View, ecs::XRView>(lock)) continue;
                        auto view = xrViews[i].Get<ecs::View>(lock);
                        auto &eye = xrViews[i].Get<ecs::XRView>(lock).eye;

                        view.UpdateViewMatrix(lock, xrViews[i]);

                        if (this->xrSystem->GetPredictedViewPose(eye, this->xrRenderPoses[i])) {
                            viewState->view[i] = glm::inverse(this->xrRenderPoses[i]) * view.viewMat;
                        } else {
                            viewState->view[i] = view.viewMat;
                        }

                        viewState->projection[i] = view.projMat;
                    }
                });

            graph.AddPass(
                "XRSubmit",
                [&](RenderGraphPassBuilder &builder) {
                    builder.TransferRead("XRColor");
                },
                [this, xrViews, lock](RenderGraphResources &resources, CommandContext &cmd) {
                    auto xrRenderTarget = resources.GetRenderTarget("XRColor");

                    cmd.AfterSubmit([this, xrRenderTarget, xrViews, lock]() {
                        for (size_t i = 0; i < xrViews.size(); i++) {
                            auto &eye = xrViews[i].Get<ecs::XRView>(lock).eye;
                            this->xrSystem->SubmitView(eye, this->xrRenderPoses[i], xrRenderTarget->ImageView().get());
                        }
                    });
                });
        }
#endif

        auto swapchainImage = device.SwapchainImageView();
        if (windowEntity && windowEntity.Has<ecs::View>(lock) && swapchainImage) {
            auto view = windowEntity.Get<ecs::View>(lock);
            auto windowViewTarget = CVarWindowViewTarget.Get();

            graph.AddPass(
                "WindowFinalOutput",
                [&](RenderGraphPassBuilder &builder) {
                    builder.ShaderRead(windowViewTarget);

                    RenderTargetDesc desc;
                    desc.extent = vk::Extent3D(view.extents.x, view.extents.y, 1);
                    desc.format = vk::Format::eR8G8B8A8Srgb;
                    desc.usage = vk::ImageUsageFlagBits::eColorAttachment;
                    builder.OutputRenderTarget("WindowFinalOutput", desc);
                },
                [this, windowViewTarget](RenderGraphResources &resources, CommandContext &cmd) {
                    auto source = resources.GetRenderTarget(windowViewTarget)->ImageView();
                    auto color = resources.GetRenderTarget("WindowFinalOutput")->ImageView();

                    RenderPassInfo info;
                    info.PushColorAttachment(color, LoadOp::Clear, StoreOp::Store, {0, 1, 0, 1});
                    cmd.BeginRenderPass(info);

                    cmd.SetTexture(0, 0, source);
                    cmd.DrawScreenCover(source);

                    if (this->debugGuiRenderer) {
                        this->debugGuiRenderer->Render(cmd, vk::Rect2D{{0, 0}, cmd.GetFramebufferExtent()});
                    }

                    cmd.EndRenderPass();
                });

            graph.SetTargetImageView("WindowFinalOutput", swapchainImage);
        }

        AddScreenshotPasses(graph);
        graph.Execute();
        EndFrame();
    }

    void Renderer::RenderShadowMaps(RenderGraph &graph, DrawLock lock) {
        if (lights.count == 0) return;

        graph.AddPass(
            "ShadowMaps",
            [&](RenderGraphPassBuilder &builder) {
                RenderTargetDesc desc;
                auto extent = glm::max(glm::ivec2(1), lights.renderTargetSize);
                desc.extent = vk::Extent3D(extent.x, extent.y, 1);
                desc.format = vk::Format::eR32Sfloat;
                desc.usage = vk::ImageUsageFlagBits::eColorAttachment;
                builder.OutputRenderTarget("ShadowMapLinear", desc);

                desc.format = vk::Format::eD16Unorm;
                desc.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
                builder.OutputRenderTarget("ShadowMapDepth", desc);
            },
            [this, lock](RenderGraphResources &resources, CommandContext &cmd) {
                auto depthLinear = resources.GetRenderTarget("ShadowMapLinear")->ImageView();
                auto depthNative = resources.GetRenderTarget("ShadowMapDepth")->ImageView();

                RenderPassInfo info;
                info.PushColorAttachment(depthLinear, LoadOp::Clear, StoreOp::Store);
                info.SetDepthStencilAttachment(depthNative, LoadOp::Clear, StoreOp::Store);
                cmd.BeginRenderPass(info);

                cmd.SetShaders("shadow_map.vert", "shadow_map.frag");

                auto &lights = this->lights;

                for (int i = 0; i < lights.count; i++) {
                    auto &view = lights.views[i];
                    ViewStateUniforms viewState;
                    viewState.view[0] = view.viewMat;
                    viewState.projection[0] = view.projMat;
                    viewState.clip[0] = view.clip;
                    cmd.UploadUniformData(0, 10, &viewState);

                    vk::Rect2D viewport;
                    viewport.extent = vk::Extent2D(view.extents.x, view.extents.y);
                    viewport.offset = vk::Offset2D(view.offset.x, view.offset.y);
                    cmd.SetViewport(viewport);

                    this->ForwardPass(cmd, view.visibilityMask, lock, [](auto lock, Tecs::Entity &ent) {});
                }

                cmd.EndRenderPass();
            });

        VisualizeBuffer(graph, "ShadowMapLinear");
    }

    void Renderer::AddScreenshotPasses(RenderGraph &graph) {
        for (auto &pending : pendingScreenshots) {
            auto screenshotPath = pending.first;
            auto screenshotResource = pending.second;
            if (screenshotResource.empty()) screenshotResource = CVarWindowViewTarget.Get();

            graph.AddPass(
                "Screenshot",
                [&](RenderGraphPassBuilder &builder) {
                    auto resource = builder.GetResourceByName(screenshotResource);
                    if (resource.type != RenderGraphResource::Type::RenderTarget) {
                        Errorf("Can't screenshot \"%s\": invalid resource", screenshotResource);
                    } else {
                        builder.TransferRead(resource.id);
                    }
                },
                [screenshotPath, screenshotResource](RenderGraphResources &resources, CommandContext &cmd) {
                    auto &res = resources.GetResourceByName(screenshotResource);
                    if (res.type == RenderGraphResource::Type::RenderTarget) {
                        auto target = resources.GetRenderTarget(res.id);
                        WriteScreenshot(cmd.Device(), screenshotPath, target->ImageView());
                    }
                });
        }
        pendingScreenshots.clear();
    }

    void Renderer::VisualizeBuffer(RenderGraph &graph, string_view name) {
        string outputName = string("Vis").append(name);
        graph.AddPass(
            "VisualizeBuffer",
            [&](RenderGraphPassBuilder &builder) {
                auto &res = builder.ShaderRead(name);
                auto desc = res.renderTargetDesc;
                desc.format = vk::Format::eR8G8B8A8Srgb;
                desc.usage = vk::ImageUsageFlagBits::eColorAttachment;
                builder.OutputRenderTarget(outputName, desc);
            },
            [name, outputName](RenderGraphResources &resources, CommandContext &cmd) {
                auto source = resources.GetRenderTarget(name)->ImageView();
                auto dest = resources.GetRenderTarget(outputName)->ImageView();

                RenderPassInfo info;
                info.PushColorAttachment(dest, LoadOp::Clear, StoreOp::Store, {0, 1, 0, 1});
                cmd.BeginRenderPass(info);

                auto format = source->Format();

                cmd.SetShader(ShaderStage::Vertex, "screen_cover.vert");
                switch (FormatComponentCount(format)) {
                case 1:
                    cmd.SetShader(ShaderStage::Fragment, "visualize_buffer_r32.frag");
                    break;
                default:
                    Abort("unimplemented type for VisualizeBuffer");
                }

                cmd.SetTexture(0, 0, source);
                cmd.Draw(3);

                cmd.EndRenderPass();
            });
    }

    void Renderer::ForwardPass(CommandContext &cmd,
                               ecs::Renderable::VisibilityMask viewMask,
                               DrawLock lock,
                               const PreDrawFunc &preDraw) {
        for (Tecs::Entity &ent : lock.EntitiesWith<ecs::Renderable>()) {
            if (ent.Has<ecs::Renderable, ecs::Transform>(lock)) {
                if (ent.Has<ecs::Mirror>(lock)) continue;
                DrawEntity(cmd, viewMask, lock, ent, preDraw);
            }
        }

        for (Tecs::Entity &ent : lock.EntitiesWith<ecs::Renderable>()) {
            if (ent.Has<ecs::Renderable, ecs::Transform, ecs::Mirror>(lock)) {
                DrawEntity(cmd, viewMask, lock, ent, preDraw);
            }
        }
    }

    void Renderer::DrawEntity(CommandContext &cmd,
                              ecs::Renderable::VisibilityMask viewMask,
                              DrawLock lock,
                              Tecs::Entity &ent,
                              const PreDrawFunc &preDraw) {
        auto &comp = ent.Get<ecs::Renderable>(lock);
        if (!comp.model || !comp.model->Valid()) return;

        // Filter entities that aren't members of all layers in the view's visibility mask.
        ecs::Renderable::VisibilityMask mask = comp.visibility;
        mask &= viewMask;
        if (mask != viewMask) return;

        glm::mat4 modelMat = ent.Get<ecs::Transform>(lock).GetGlobalTransform(lock);

        if (preDraw) preDraw(lock, ent);

        auto model = activeModels.Load(comp.model->name);
        if (!model) {
            model = make_shared<Model>(*comp.model, device);
            activeModels.Register(comp.model->name, model);
        }

        model->Draw(cmd, modelMat); // TODO pass and use comp.model->bones
    }

    void Renderer::EndFrame() {
        activeModels.Tick();
    }

    void Renderer::SetDebugGui(GuiManager &gui) {
        debugGuiRenderer = make_unique<GuiRenderer>(device, gui);
    }

    void Renderer::TriggerScreenshot(const string &path, const string &resource) {
        pendingScreenshots.push_back({path, resource});
    }
} // namespace sp::vulkan
