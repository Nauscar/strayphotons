#pragma once

#include "graphics/GPUTimer.hh"
#include "game/GuiManager.hh"

#include <sstream>
#include <imgui/imgui.h>

namespace sp
{
	class ProfilerGui : public GuiRenderable
	{
	public:
		ProfilerGui(GPUTimer *timer) : timer(timer) { }

		void Add()
		{
			if (CVarProfileGPU.Get() != 1 || timer->lastCompleteFrame.results.empty())
				return;

			ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize;

			ImGui::Begin("Profiler", nullptr, flags);
			AddResults(timer->lastCompleteFrame.results);
			ImGui::End();
		}

	private:
		size_t AddResults(const vector<GPUTimeResult> &results, size_t offset = 0, int depth = 1)
		{
			while (offset < results.size())
			{
				auto result = results[offset++];

				if (result.depth < depth)
					return offset - 1;

				if (result.depth > depth)
					continue;

				ImGui::PushID(offset);

				int depth = result.depth;
				double elapsed = (double)result.elapsed / 1000000.0;

				if (ImGui::TreeNodeEx("node", ImGuiTreeNodeFlags_DefaultOpen, "%s %.2fms", result.name.c_str(), elapsed))
				{
					offset = AddResults(results, offset, depth + 1);
					ImGui::TreePop();
				}

				ImGui::PopID();
			}
			return offset;
		}

		GPUTimer *timer;
	};
}