#include "Settings/InterfaceSection.h"

#include "Settings/AppSettings.h"
#include "Settings/InterfaceScale.h"

#include "Core/ImGui/ImGuiRenderer.h"

#include <imgui.h>

#include <string>

namespace Graphite {

	InterfaceSection::InterfaceSection(AppSettings& settings) : m_Settings(settings)
	{
	}

	void InterfaceSection::OnImGuiRender()
	{
		ImGui::SeparatorText("Scale");

		const int scalePercent = m_Settings.GetInterfaceScalePercent();

		const std::string current = std::to_string(scalePercent) + "%";

		if (ImGui::BeginCombo("Interface scale", current.c_str()))
		{
			for (const int percent : kInterfaceScalePercents)
			{
				const std::string label = std::to_string(percent) + "%";
				const bool selected = percent == scalePercent;

				if (ImGui::Selectable(label.c_str(), selected) && !selected)
				{
					Core::ImGuiRenderer::SetInterfaceScale(static_cast<float>(percent) / 100.0f);
					m_Settings.SetInterfaceScalePercent(percent);
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
