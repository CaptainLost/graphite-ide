#include "Settings/SchemeSection.h"

#include "Settings/AppSettings.h"
#include "Theme/ActiveScheme.h"

#include <imgui.h>

namespace Graphite {

	SchemeSection::SchemeSection(ActiveScheme& scheme)
		: m_Active(scheme), m_Draft(AppSettings::ThemesDirectory())
	{
		// Constructed before GraphiteLayer calls ActiveScheme::Load, so this reads the setting
		// rather than the resolved value — the two agree, because Load reads the same key. An
		// unset key is handled by Select, not guarded against here: one place should know what an
		// empty name means.
		m_Draft.Select(AppSettings::LoadColorScheme());
	}

	void SchemeSection::Apply()
	{
		m_Active.Set(m_Draft.GetPreview());
	}

	void SchemeSection::OnDeactivated()
	{
		m_Draft.Select(AppSettings::LoadColorScheme());
		Apply();
	}

	void SchemeSection::RenderSchemeList()
	{
		if (!ImGui::BeginCombo("Scheme", m_Draft.GetSelected().c_str()))
		{
			return;
		}

		for (const std::string& name : m_Draft.GetNames())
		{
			const bool selected = name == m_Draft.GetSelected();

			if (ImGui::Selectable(name.c_str(), selected) && !selected)
			{
				m_Draft.Select(name);
				AppSettings::SaveColorScheme(name);
				Apply();
			}
		}

		ImGui::EndCombo();
	}

	void SchemeSection::OnImGuiRender()
	{
		RenderSchemeList();

		if (m_Draft.IsReadOnly())
		{
			ImGui::TextDisabled("Built-in schemes cannot be edited. Duplicate one to change it.");
		}
	}
}
