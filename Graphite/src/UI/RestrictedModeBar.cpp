#include "UI/RestrictedModeBar.h"

#include "Core/FileSystem/FileSystem.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <string>

namespace Graphite {

	RestrictedBarChoice DrawRestrictedModeBar(const std::optional<std::filesystem::path>& parent)
	{
		RestrictedBarChoice choice = RestrictedBarChoice::None;

		const ImGuiStyle& style = ImGui::GetStyle();
		const float height = ImGui::GetFrameHeight() + style.FramePadding.y * 2.0f;

		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.ItemSpacing.x, style.FramePadding.y));

		if (ImGui::BeginChild("##RestrictedMode", ImVec2(0.0f, height), ImGuiChildFlags_AlwaysUseWindowPadding,
							  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(ICON_MDI_SHIELD_ALERT_OUTLINE
								   "  Restricted mode: the language server and background analysis are off for this folder.");

			ImGui::SameLine();

			if (ImGui::Button("Trust Folder"))
			{
				choice = RestrictedBarChoice::TrustFolder;
			}

			if (parent)
			{
				ImGui::SameLine();

				// WARNING: "###" keeps the button's id fixed whatever the path holds, "##" included.
				const std::string label = "Trust All in " + Core::ToUtf8(*parent) + "###TrustParent";

				if (ImGui::Button(label.c_str()))
				{
					choice = RestrictedBarChoice::TrustParent;
				}
			}
		}

		ImGui::EndChild();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();

		return choice;
	}

	std::string TrustAllMessage(const std::filesystem::path& parent)
	{
		return "Trust every folder under\n" + Core::ToUtf8(parent) +
			   "\n\nEach folder in it, now and in the future, will run the language server and "
			   "background analysis when opened.";
	}
}
