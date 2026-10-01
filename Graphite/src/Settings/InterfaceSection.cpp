#include "Settings/InterfaceSection.h"

#include "Settings/AppSettings.h"
#include "Settings/InterfaceScale.h"

#include "Core/ImGui/ImGuiRenderer.h"

#include <imgui.h>

#include <string>

namespace Graphite {

	InterfaceSection::InterfaceSection()
		: m_ScalePercent(AppSettings::LoadInterfaceScalePercent())
	{
	}

	void InterfaceSection::OnImGuiRender()
	{
		ImGui::SeparatorText("Scale");

		const std::string current = std::to_string(m_ScalePercent) + "%";

		if (ImGui::BeginCombo("Interface scale", current.c_str()))
		{
			for (const int percent : kInterfaceScalePercents)
			{
				const std::string label = std::to_string(percent) + "%";
				const bool selected = percent == m_ScalePercent;

				if (ImGui::Selectable(label.c_str(), selected) && !selected)
				{
					m_ScalePercent = percent;
					Core::ImGuiRenderer::SetInterfaceScale(static_cast<float>(percent) / 100.0f);
					AppSettings::SaveInterfaceScalePercent(percent);
				}

				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}

			ImGui::EndCombo();
		}

		ImGui::TextDisabled("On top of the scale Windows sets for each monitor.");
	}
}
