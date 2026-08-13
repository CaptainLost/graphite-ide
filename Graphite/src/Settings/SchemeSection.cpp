#include "Settings/SchemeSection.h"

#include "Settings/AppSettings.h"
#include "Theme/ActiveScheme.h"

#include <imgui.h>

namespace Graphite {

	namespace {

		const Core::Color& ColorIn(const ResolvedScheme& scheme, TokenKind kind)
		{
			return scheme.Syntax[static_cast<std::size_t>(kind)];
		}

		const Core::Color& ColorIn(const ResolvedScheme& scheme, EditorColor color)
		{
			return scheme.Editor[static_cast<std::size_t>(color)];
		}
	}

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

	template<typename Which>
	void SchemeSection::RenderColorRow(Which which)
	{
		const std::size_t index = static_cast<std::size_t>(which);

		ImGui::PushID(static_cast<int>(index));

		const bool readOnly = m_Draft.IsReadOnly();
		const bool overridden = m_Draft.IsOverridden(which);

		const Core::Color& shown = ColorIn(m_Draft.GetPreview(), which);

		// A local array rather than &shown.R: Core::Color's four floats are almost certainly
		// contiguous, but "almost certainly" is not a thing to hand a pointer to.
		float rgba[4] = {shown.R, shown.G, shown.B, shown.A};

		ImGuiColorEditFlags flags = ImGuiColorEditFlags_AlphaBar |
									ImGuiColorEditFlags_AlphaPreviewHalf;

		if (readOnly)
		{
			flags |= ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoOptions;
		}

		// .data() is safe only because every label is a string literal and therefore
		// null-terminated.
		if (ImGui::ColorEdit4(LabelOf(which).data(), rgba, flags) && !readOnly)
		{
			m_Draft.SetColor(which, Core::Color{rgba[0], rgba[1], rgba[2], rgba[3]});
			Apply();
		}

		// Written on release, not on every frame of the drag: SaveScheme rewrites the whole
		// scheme file, and a drag through the picker produces a new colour every frame.
		if (ImGui::IsItemDeactivatedAfterEdit() && !readOnly)
		{
			m_Draft.Save();
		}

		if (overridden && !readOnly)
		{
			ImGui::SameLine();

			if (ImGui::SmallButton("Reset"))
			{
				m_Draft.Revert(which);
				Apply();
				m_Draft.Save();
			}
		}
		else if (!readOnly)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("inherited");
		}

		ImGui::PopID();
	}

	template<typename Which>
	void SchemeSection::RenderGroup(const char* title, std::size_t count)
	{
		if (!ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		ImGui::PushID(title);

		for (std::size_t index = 0; index < count; ++index)
		{
			RenderColorRow(static_cast<Which>(index));
		}

		ImGui::PopID();
	}

	void SchemeSection::OnImGuiRender()
	{
		RenderSchemeList();

		if (m_Draft.IsReadOnly())
		{
			ImGui::TextDisabled("Built-in schemes cannot be edited. Duplicate one to change it.");
		}

		ImGui::Separator();

		RenderGroup<TokenKind>("Syntax", static_cast<std::size_t>(TokenKind::Count));
		RenderGroup<EditorColor>("Editor", static_cast<std::size_t>(EditorColor::Count));
	}
}
