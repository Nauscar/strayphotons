#pragma once

#include <string>

#include "graphics/Graphics.hh"
#include "Common.hh"

namespace sp
{
	class Device;
	class ShaderSet;
	class Game;

	class GraphicsContext
	{
	public:
		GraphicsContext(Game *game);
		virtual ~GraphicsContext();

		void CreateWindow();
		bool ShouldClose();
		void SetTitle(string title);
		void ResetSwapchain(uint32 &width, uint32 &height);

		virtual void Prepare() = 0;
		virtual void RenderFrame() = 0;

		ShaderSet *GlobalShaders;

	private:

	protected:
		GLFWwindow *window = nullptr;
		Game *game;
	};
}
