#include "Settings/SettingsPanel.h"

#include <imgui.h>

namespace Graphite {

	namespace {

		constexpr float kSectionListWidth = 180.0f;
	}

	SettingsPanel::SettingsPanel(std::vector<Core::Scope<SettingsSection>> sections)
		: Core::Panel("settings", "Settings", "window", false), m_Sections(std::move(sections))
	{
	}

	void SettingsPanel::SelectSection(std::size_t index)
	{
		if (index >= m_Sections.size())
		{
			return;
		}

		if (index != m_Active)
		{
			m_Sections[m_Active]->OnDeactivated();
		}

		m_Active = index;
	}

	void SettingsPanel::OnUpdate(Core::Timestep ts)
	{
		(void)ts;

		const bool drawn = WasDrawn();

		// Latched on the edge, not on the state: hiding the panel deactivates the section once,
		// rather than every frame it stays hidden. A section left holding transient state — an
		// open confirmation, say — is never rendered again and so can never let go of it itself.
		if (m_WasDrawnLastFrame && !drawn && m_Active < m_Sections.size())
		{
			m_Sections[m_Active]->OnDeactivated();
		}

		m_WasDrawnLastFrame = drawn;
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
				ImGui::PushID(static_cast<int>(index));

				const std::string_view title = m_Sections[index]->GetTitle();

				if (ImGui::Selectable(std::string(title).c_str(), index == m_Active))
				{
					SelectSection(index);
				}

				ImGui::PopID();
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
