#include "corepch.h"

#include "Core/ImGui/ContentWindowColours.h"

#include <imgui.h>

namespace Core {

	namespace {
		constexpr int kContentColourCount = 5;
	}

	void PushContentWindowColours()
	{
		// WARNING: Theme::Apply keeps the content colour in the empty dock space's slot, which is the only place the style holds it.
		const ImVec4 content = ImGui::GetStyleColorVec4(ImGuiCol_DockingEmptyBg);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, content);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, content);
		ImGui::PushStyleColor(ImGuiCol_TabSelected, content);
		ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected, content);
		ImGui::PushStyleColor(ImGuiCol_TabHovered, content);
	}

	void PopContentWindowColours()
	{
		ImGui::PopStyleColor(kContentColourCount);
	}
}
