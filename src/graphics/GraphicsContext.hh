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
	class GPUTimer;

	class GraphicsContext
	{
	public:
		GraphicsContext(Game *game);
		virtual ~GraphicsContext();

		void CreateWindow(glm::ivec2 initialSize = { 640, 480 });
		bool ShouldClose();
		void SetTitle(string title);
		void BindInputCallbacks(InputManager &inputManager);
		void ResizeWindow(ecs::View &frameBufferView, int fullscreen);

		virtual void Prepare() = 0;
		virtual void BeginFrame() = 0;
		virtual void RenderPass(ecs::View &view) = 0;
		virtual void PrepareForView(ecs::View &view) = 0;
		virtual void RenderLoading(ecs::View &view) = 0;
		virtual void EndFrame() = 0;

		ShaderSet *GlobalShaders;
		GPUTimer *Timer = nullptr;

		GLFWwindow *GetWindow()
		{
			return window;
		}

	private:
		glm::ivec2 prevWindowSize, prevWindowPos;
		int prevFullscreen = 0;
		double windowScale = 1.0;

	protected:
		GLFWwindow *window = nullptr;
		Game *game;
	};
}
