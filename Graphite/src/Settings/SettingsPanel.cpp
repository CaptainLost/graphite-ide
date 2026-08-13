#include "Settings/SettingsPanel.h"

#include <imgui.h>

namespace Graphite {

	namespace {

		constexpr float kSectionListWidth = 180.0f;
	}

	SettingsPanel::SettingsPanel(std::vector<Core::Scope<SettingsSection>> sections)
		: Core::Panel("settings", "Settings", false), m_Sections(std::move(sections))
	{
	}

	void SettingsPanel::SelectSection(std::size_t index)
	{
		if (index >= m_Sections.size())
		{
			return;
		}

		m_Active = index;
	}

	void SettingsPanel::OnEvent(Core::Event& event)
	{
		if (m_Active >= m_Sections.size())
		{
			return;
		}

		m_Sections[m_Active]->OnEvent(event);
	}

	void SettingsPanel::OnImGuiRender()
	{
		if (m_Sections.empty())
		{
			return;
		}

		// No splitter: an adjustable divider over a handful of entries is a control nobody
		// moves and everybody maintains.
		if (ImGui::BeginChild("sections", ImVec2(kSectionListWidth, 0.0f),
							  ImGuiChildFlags_Borders))
		{
			for (std::size_t index = 0; index < m_Sections.size(); ++index)
			{
				const std::string_view title = m_Sections[index]->GetTitle();

				if (ImGui::Selectable(std::string(title).c_str(), index == m_Active))
				{
					m_Active = index;
				}
			}
		}

		ImGui::EndChild();

		ImGui::SameLine();

		if (ImGui::BeginChild("section"))
		{
			m_Sections[m_Active]->OnImGuiRender();
		}

		ImGui::EndChild();
	}
}
