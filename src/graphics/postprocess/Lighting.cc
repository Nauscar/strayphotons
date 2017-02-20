#include "Lighting.hh"
#include "core/Logging.hh"
#include "core/CVar.hh"
#include "graphics/Renderer.hh"
#include "graphics/ShaderManager.hh"
#include "graphics/GenericShaders.hh"
#include "graphics/Util.hh"
#include "graphics/Buffer.hh"
#include "ecs/components/Light.hh"
#include "ecs/components/Transform.hh"

namespace sp
{
	static CVar<int> CVarVoxelLightingMode("r.VoxelLighting", 1, "Voxel lighting mode (0: direct only, 1: full, 2: indirect only, 3: diffuse only, 4: specular only, 5: full voxel)");
	static CVar<int> CVarVoxelDiffuseDownsample("r.VoxelDiffuseDownsample", 2, "N times downsampled rendering of indirect diffuse lighting");
	static CVar<float> CVarExposure("r.Exposure", 1.0, "Fixed exposure value");
	static CVar<bool> CVarDrawHistogram("r.Histogram", false, "Draw HDR luminosity histogram");

	class TonemapFS : public Shader
	{
		SHADER_TYPE(TonemapFS)

		TonemapFS(shared_ptr<ShaderCompileOutput> compileOutput) : Shader(compileOutput)
		{
		}
	};

	IMPLEMENT_SHADER_TYPE(TonemapFS, "tonemap.frag", Fragment);

	void Tonemap::Process(const PostProcessingContext *context)
	{
		auto r = context->renderer;
		auto dest = outputs[0].AllocateTarget(context)->GetTexture();

		r->SetRenderTarget(&dest, nullptr);
		r->ShaderControl->BindPipeline<BasicPostVS, TonemapFS>(r->GlobalShaders);

		DrawScreenCover();
	}

	class LumiHistogramCS : public Shader
	{
		SHADER_TYPE(LumiHistogramCS)

		const int Bins = 64;

		LumiHistogramCS(shared_ptr<ShaderCompileOutput> compileOutput) : Shader(compileOutput)
		{
		}

		Texture &GetTarget(Renderer *r)
		{
			if (!target)
			{
				target = r->RTPool->Get(RenderTargetDesc(PF_R32UI, { Bins, 1 }));
			}
			return target->GetTexture();
		}

		float ComputeExposure()
		{
			if (!readBackBuf)
			{
				return 0.0f;
			}

			uint32 *buf = (uint32 *) readBackBuf.Map(GL_READ_ONLY);
			if (!buf)
			{
				Logf("Missed readback of luminosity histogram");
				return 0.0f;
			}

			uint64 sum = 0;

			for (int i = 0; i < Bins; i++)
				sum += buf[i];

			double discardLower = sum * 0.5, discardUpper = sum * 0.9;
			double accum = 0.0, totalWeight = 0.0;

			for (int i = 0; i < Bins; i++)
			{
				double weight = buf[i];
				double m = std::min(weight, discardLower);

				weight -= m; // discard samples
				discardLower -= m;
				discardUpper -= m;

				weight = std::min(weight, discardUpper);
				discardUpper -= weight;

				double luminance = LuminanceFromBin(i / (double) (Bins - 1));
				accum += luminance * weight;
				totalWeight += weight;
			}

			accum /= std::max(1e-5, totalWeight);
			readBackBuf.Unmap();

			float exposure = 0.0f;
			// TODO
			return exposure;
		}

		double LuminanceFromBin(double bin)
		{
			double lumMin = -8, lumMax = 4;
			return std::exp2(bin * (lumMax - lumMin) + lumMin);
		}

		void StartReadback()
		{
			if (!readBackBuf)
			{
				readBackBuf.Create().Data(sizeof(uint32) * Bins, nullptr, GL_STREAM_READ);
			}

			readBackBuf.Bind(GL_PIXEL_PACK_BUFFER);
			glGetTextureImage(target->GetTexture().handle, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, sizeof(uint32) * Bins, 0);
		}

	private:
		shared_ptr<RenderTarget> target;
		sp::Buffer readBackBuf;
	};

	IMPLEMENT_SHADER_TYPE(LumiHistogramCS, "lumi_histogram.comp", Compute);

	class RenderHistogramFS : public Shader
	{
		SHADER_TYPE(RenderHistogramFS)

		RenderHistogramFS(shared_ptr<ShaderCompileOutput> compileOutput) : Shader(compileOutput)
		{
		}
	};

	IMPLEMENT_SHADER_TYPE(RenderHistogramFS, "render_histogram.frag", Fragment);

	void LumiHistogram::Process(const PostProcessingContext *context)
	{
		const int wgsize = 16;
		const int downsample = 2; // Calculate histograms with N times fewer workgroups.

		auto r = context->renderer;
		auto shader = r->GlobalShaders->Get<LumiHistogramCS>();
		auto histTex = shader->GetTarget(r);

		float lastExposure = shader->ComputeExposure();
		if (lastExposure != 0.0f) Logf("%f", lastExposure);

		r->SetRenderTarget(&histTex, nullptr);
		glClearColor(0.0, 0.0, 0.0, 0.0);
		glClear(GL_COLOR_BUFFER_BIT);

		r->ShaderControl->BindPipeline<LumiHistogramCS>(r->GlobalShaders);
		histTex.BindImage(0, GL_READ_WRITE);

		auto extents = GetInput(0)->GetOutput()->TargetDesc.extent / downsample;
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
		glDispatchCompute((extents.x + wgsize - 1) / wgsize, (extents.y + wgsize - 1) / wgsize, 1);
		glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

		shader->StartReadback();

		if (CVarDrawHistogram.Get())
		{
			auto dest = outputs[0].AllocateTarget(context)->GetTexture();
			r->SetRenderTarget(&dest, nullptr);
			r->ShaderControl->BindPipeline<BasicPostVS, RenderHistogramFS>(r->GlobalShaders);
			DrawScreenCover();
		}
		else
		{
			SetOutputTarget(0, GetInput(0)->GetOutput()->TargetRef);
		}
	}

	class VoxelLightingFS : public Shader
	{
		SHADER_TYPE(VoxelLightingFS)

		static const int maxLights = 16;

		VoxelLightingFS(shared_ptr<ShaderCompileOutput> compileOutput) : Shader(compileOutput)
		{
			Bind(lightCount, "lightCount");
			Bind(lightPosition, "lightPosition");
			Bind(lightTint, "lightTint");
			Bind(lightDirection, "lightDirection");
			Bind(lightSpotAngleCos, "lightSpotAngleCos");
			Bind(lightProj, "lightProj");
			Bind(lightInvProj, "lightInvProj");
			Bind(lightView, "lightView");
			Bind(lightClip, "lightClip");
			Bind(lightMapOffset, "lightMapOffset");
			Bind(lightIntensity, "lightIntensity");
			Bind(lightIlluminance, "lightIlluminance");

			Bind(exposure, "exposure");
			Bind(targetSize, "targetSize");

			Bind(invProjMat, "invProjMat");
			Bind(invViewMat, "invViewMat");
			Bind(mode, "mode");

			Bind(voxelSize, "voxelSize");
			Bind(voxelGridCenter, "voxelGridCenter");
			Bind(diffuseDownsample, "diffuseDownsample");
		}

		void SetLights(ecs::EntityManager &manager, ecs::EntityManager::EntityCollection &lightCollection)
		{
			glm::vec3 lightPositions[maxLights];
			glm::vec3 lightTints[maxLights];
			glm::vec3 lightDirections[maxLights];
			float lightSpotAnglesCos[maxLights];
			glm::mat4 lightProjs[maxLights];
			glm::mat4 lightInvProjs[maxLights];
			glm::mat4 lightViews[maxLights];
			glm::vec2 lightClips[maxLights];
			glm::vec4 lightMapOffsets[maxLights];
			float lightIntensities[maxLights];
			float lightIlluminances[maxLights];

			int lightNum = 0;
			for (auto entity : lightCollection)
			{
				auto light = entity.Get<ecs::Light>();
				auto view = entity.Get<ecs::View>();
				auto transform = entity.Get<ecs::Transform>();
				lightPositions[lightNum] = transform->GetModelTransform(manager) * glm::vec4(0, 0, 0, 1);
				lightTints[lightNum] = light->tint;
				lightDirections[lightNum] = glm::mat3(transform->GetModelTransform(manager)) * glm::vec3(0, 0, -1);
				lightSpotAnglesCos[lightNum] = cos(light->spotAngle);
				lightProjs[lightNum] = view->projMat;
				lightInvProjs[lightNum] = view->invProjMat;
				lightViews[lightNum] = view->viewMat;
				lightClips[lightNum] = view->clip;
				lightMapOffsets[lightNum] = light->mapOffset;
				lightIntensities[lightNum] = light->intensity;
				lightIlluminances[lightNum] = light->illuminance;
				lightNum++;
			}

			Set(lightCount, lightNum);
			Set(lightPosition, lightPositions, lightNum);
			Set(lightTint, lightTints, lightNum);
			Set(lightDirection, lightDirections, lightNum);
			Set(lightSpotAngleCos, lightSpotAnglesCos, lightNum);
			Set(lightProj, lightProjs, lightNum);
			Set(lightInvProj, lightInvProjs, lightNum);
			Set(lightView, lightViews, lightNum);
			Set(lightClip, lightClips, lightNum);
			Set(lightMapOffset, lightMapOffsets, lightNum);
			Set(lightIntensity, lightIntensities, lightNum);
			Set(lightIlluminance, lightIlluminances, lightNum);
		}

		void SetExposure(float newExposure)
		{
			Set(exposure, newExposure);
		}

		void SetViewParams(const ecs::View &view)
		{
			Set(invProjMat, view.invProjMat);
			Set(invViewMat, view.invViewMat);
			Set(targetSize, glm::vec2(view.extents));
		}

		void SetMode(int newMode)
		{
			Set(mode, newMode);
		}

		void SetVoxelInfo(ecs::VoxelInfo &voxelInfo, int diffDownsample)
		{
			Set(voxelSize, voxelInfo.voxelSize);
			Set(voxelGridCenter, voxelInfo.voxelGridCenter);
			Set(diffuseDownsample, (float) diffDownsample);
		}

	private:
		Uniform lightCount, lightPosition, lightTint, lightDirection, lightSpotAngleCos;
		Uniform lightProj, lightInvProj, lightView, lightClip, lightMapOffset, lightIntensity, lightIlluminance;
		Uniform exposure, targetSize, invViewMat, invProjMat, mode;
		Uniform voxelSize, voxelGridCenter, diffuseDownsample;
	};

	IMPLEMENT_SHADER_TYPE(VoxelLightingFS, "voxel_lighting.frag", Fragment);

	class VoxelLightingDiffuseFS : public Shader
	{
		SHADER_TYPE(VoxelLightingDiffuseFS)

		VoxelLightingDiffuseFS(shared_ptr<ShaderCompileOutput> compileOutput) : Shader(compileOutput)
		{
			Bind(exposure, "exposure");
			Bind(targetSize, "targetSize");

			Bind(invProjMat, "invProjMat");
			Bind(invViewMat, "invViewMat");

			Bind(voxelSize, "voxelSize");
			Bind(voxelGridCenter, "voxelGridCenter");
		}

		void SetExposure(float newExposure)
		{
			Set(exposure, newExposure);
		}

		void SetViewParams(const ecs::View &view)
		{
			Set(invProjMat, view.invProjMat);
			Set(invViewMat, view.invViewMat);
			Set(targetSize, glm::vec2(view.extents));
		}

		void SetVoxelInfo(ecs::VoxelInfo &voxelInfo)
		{
			Set(voxelSize, voxelInfo.voxelSize);
			Set(voxelGridCenter, voxelInfo.voxelGridCenter);
		}

	private:
		Uniform exposure, targetSize, invViewMat, invProjMat;
		Uniform voxelSize, voxelGridCenter;
	};

	IMPLEMENT_SHADER_TYPE(VoxelLightingDiffuseFS, "voxel_lighting_diffuse.frag", Fragment);

	void VoxelLighting::Process(const PostProcessingContext *context)
	{
		auto r = context->renderer;
		auto dest = outputs[0].AllocateTarget(context)->GetTexture();

		int diffuseDownsample = CVarVoxelDiffuseDownsample.Get();
		if (diffuseDownsample < 1) diffuseDownsample = 1;

		auto lights = context->game->entityManager.EntitiesWith<ecs::Light>();
		r->GlobalShaders->Get<VoxelLightingFS>()->SetLights(context->game->entityManager, lights);
		r->GlobalShaders->Get<VoxelLightingFS>()->SetExposure(CVarExposure.Get());
		r->GlobalShaders->Get<VoxelLightingFS>()->SetViewParams(context->view);
		r->GlobalShaders->Get<VoxelLightingFS>()->SetMode(CVarVoxelLightingMode.Get());
		r->GlobalShaders->Get<VoxelLightingFS>()->SetVoxelInfo(context->renderer->voxelInfo, diffuseDownsample);

		r->SetRenderTarget(&dest, nullptr);
		r->ShaderControl->BindPipeline<BasicPostVS, VoxelLightingFS>(r->GlobalShaders);

		DrawScreenCover();
	}

	VoxelLightingDiffuse::VoxelLightingDiffuse()
	{
		downsample = CVarVoxelDiffuseDownsample.Get();
		if (downsample < 1) downsample = 1;
	}

	void VoxelLightingDiffuse::Process(const PostProcessingContext *context)
	{
		auto r = context->renderer;
		auto dest = outputs[0].AllocateTarget(context)->GetTexture();

		r->GlobalShaders->Get<VoxelLightingDiffuseFS>()->SetExposure(CVarExposure.Get());
		r->GlobalShaders->Get<VoxelLightingDiffuseFS>()->SetViewParams(context->view);
		r->GlobalShaders->Get<VoxelLightingDiffuseFS>()->SetVoxelInfo(context->renderer->voxelInfo);

		glViewport(0, 0, outputs[0].TargetDesc.extent[0], outputs[0].TargetDesc.extent[1]);
		r->SetRenderTarget(&dest, nullptr);
		r->ShaderControl->BindPipeline<BasicPostVS, VoxelLightingDiffuseFS>(r->GlobalShaders);

		DrawScreenCover();

		auto view = context->view;
		glViewport(view.offset.x, view.offset.y, view.extents.x, view.extents.y);
	}
}
