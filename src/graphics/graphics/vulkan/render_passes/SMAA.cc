#include "SMAA.hh"

#include "assets/AssetManager.hh"
#include "graphics/vulkan/core/CommandContext.hh"
#include "graphics/vulkan/core/DeviceContext.hh"

namespace sp::vulkan::renderer {
    void SMAA::AddPass(RenderGraph &graph) {
        if (!areaTexAsset) areaTexAsset = GAssets.LoadImage("textures/smaa/AreaTex.tga");
        if (!searchTexAsset) searchTexAsset = GAssets.LoadImage("textures/smaa/SearchTex.tga");
        if (!areaTexAsset->Ready() || !searchTexAsset->Ready()) return;

        if (!areaTex) areaTex = graph.Device().LoadAssetImage(areaTexAsset->Get(), false, false);
        if (!searchTex) searchTex = graph.Device().LoadAssetImage(searchTexAsset->Get(), false, false);
        if (!areaTex->Ready() || !searchTex->Ready()) return;

        auto sourceID = graph.LastOutputID();
        auto scope = graph.Scope("SMAA");

        graph.AddPass("GammaCorrect")
            .Build([&](PassBuilder &builder) {
                auto source = builder.TextureRead("LinearLuminance");

                auto desc = source.DeriveRenderTarget();
                desc.format = vk::Format::eR8G8B8A8Unorm;
                builder.OutputColorAttachment(0, "luminance", desc, {LoadOp::DontCare, StoreOp::Store});
            })
            .Execute([](Resources &res, CommandContext &cmd) {
                cmd.SetShaders("screen_cover.vert", "gamma_correct.frag");
                cmd.SetImageView(0, 0, res.GetRenderTarget("LinearLuminance")->ImageView());
                cmd.Draw(3);
            });

        graph.AddPass("EdgeDetection")
            .Build([&](PassBuilder &builder) {
                auto luminance = builder.TextureRead("luminance");

                auto desc = luminance.DeriveRenderTarget();
                desc.format = vk::Format::eR8G8B8A8Unorm;
                builder.OutputColorAttachment(0, "edges", desc, {LoadOp::Clear, StoreOp::Store});

                desc.format = vk::Format::eD24UnormS8Uint;
                builder.OutputDepthAttachment("stencil", desc, {LoadOp::Clear, StoreOp::Store});

                builder.UniformRead("ViewState");
            })
            .Execute([](Resources &res, CommandContext &cmd) {
                cmd.SetShaders("screen_cover.vert", "smaa/edge_detection.frag");
                cmd.SetImageView(0, 0, res.GetRenderTarget("luminance")->ImageView());
                cmd.SetDepthTest(false, false);
                cmd.SetStencilTest(true);
                cmd.SetStencilCompareOp(vk::CompareOp::eAlways);
                cmd.SetStencilReference(vk::StencilFaceFlagBits::eFront, 1);
                cmd.SetStencilWriteMask(vk::StencilFaceFlagBits::eFront, 0xff);
                cmd.SetStencilFailOp(vk::StencilOp::eKeep);
                cmd.SetStencilDepthFailOp(vk::StencilOp::eKeep);
                cmd.SetStencilPassOp(vk::StencilOp::eReplace);
                cmd.SetUniformBuffer(0, 10, res.GetBuffer("ViewState"));
                cmd.Draw(3);
            });

        graph.AddPass("BlendingWeights")
            .Build([&](PassBuilder &builder) {
                auto edges = builder.TextureRead("edges");

                auto desc = edges.DeriveRenderTarget();
                builder.OutputColorAttachment(0, "weights", desc, {LoadOp::Clear, StoreOp::Store});

                builder.SetDepthAttachment("stencil", {LoadOp::Load, StoreOp::Store});
                builder.UniformRead("ViewState");
            })
            .Execute([this](Resources &res, CommandContext &cmd) {
                cmd.SetShaders("screen_cover.vert", "smaa/blending_weights.frag");
                cmd.SetImageView(0, 0, res.GetRenderTarget("edges")->ImageView());
                cmd.SetImageView(0, 1, areaTex->Get());
                cmd.SetImageView(0, 2, searchTex->Get());
                cmd.SetDepthTest(false, false);
                cmd.SetStencilTest(true);
                cmd.SetStencilCompareOp(vk::CompareOp::eEqual);
                cmd.SetStencilReference(vk::StencilFaceFlagBits::eFront, 1);
                cmd.SetStencilCompareMask(vk::StencilFaceFlagBits::eFront, 0xff);
                cmd.SetStencilFailOp(vk::StencilOp::eZero);
                cmd.SetStencilDepthFailOp(vk::StencilOp::eKeep);
                cmd.SetStencilPassOp(vk::StencilOp::eReplace);
                cmd.SetStencilWriteMask(vk::StencilFaceFlagBits::eFront, 0);
                cmd.SetUniformBuffer(0, 10, res.GetBuffer("ViewState"));
                cmd.Draw(3);
            });

        graph.AddPass("Blend")
            .Build([&](PassBuilder &builder) {
                auto source = builder.TextureRead(sourceID);
                builder.TextureRead("weights");

                auto desc = source.DeriveRenderTarget();
                builder.OutputColorAttachment(0, "Output", desc, {LoadOp::DontCare, StoreOp::Store});
                builder.UniformRead("ViewState");
            })
            .Execute([sourceID](Resources &res, CommandContext &cmd) {
                cmd.SetShaders("screen_cover.vert", "smaa/blending.frag");
                cmd.SetImageView(0, 0, res.GetRenderTarget(sourceID)->ImageView());
                cmd.SetImageView(0, 1, res.GetRenderTarget("weights")->ImageView());
                cmd.SetUniformBuffer(0, 10, res.GetBuffer("ViewState"));
                cmd.Draw(3);
            });
    }
} // namespace sp::vulkan::renderer
