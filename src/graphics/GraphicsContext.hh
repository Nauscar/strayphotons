#pragma once

#include <string>

#include "graphics/Graphics.hh"
#include "ecs/components/View.hh"
#include "Common.hh"

namespace sp
{
	class Device;
	class ShaderSet;
	class Game;
	class InputManager;
	class RenderTarget;

	struct VoxelColors {
		shared_ptr<RenderTarget> Color;
	};

	class GraphicsContext
	{
	public:
		GraphicsContext(Game *game);
		virtual ~GraphicsContext();

		void CreateWindow(glm::ivec2 initialSize = { 640, 480 });
		bool ShouldClose();
		void SetTitle(string title);
		void BindInputCallbacks(InputManager &inputManager);

		virtual void Prepare() = 0;
		virtual void BeginFrame(ecs::View &fbView, int fullscreen) = 0;
		virtual void RenderPass(ecs::View &view, shared_ptr<RenderTarget> shadowMap, VoxelColors voxelGrid) = 0;
		virtual void EndFrame() = 0;

		ShaderSet *GlobalShaders;

		GLFWwindow *GetWindow()
		{
			return window;
		}

	private:

	protected:
		GLFWwindow *window = nullptr;
		Game *game;
	};
}
