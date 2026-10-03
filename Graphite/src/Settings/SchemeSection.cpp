#include "Settings/SchemeSection.h"

#include "Settings/AppSettings.h"
#include "Theme/ActiveScheme.h"
#include "Theme/Palettes.h"
#include "UI/DialogLayout.h"

#include <imgui.h>

#include <optional>

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

		const Core::Color& ColorIn(const ResolvedScheme& scheme, ChromeColor color)
		{
			return scheme.Chrome[static_cast<std::size_t>(color)];
		}

		const Core::Color& ColorIn(const ResolvedScheme& scheme, GitColor color)
		{
			return scheme.Git[static_cast<std::size_t>(color)];
		}

		const Core::Color& ColorIn(const ResolvedScheme& scheme, SessionColor color)
		{
			return scheme.Session[static_cast<std::size_t>(color)];
		}
	}

	SchemeSection::SchemeSection(ActiveScheme& scheme, AppSettings& settings, ModalGate& modals)
		: m_Active(scheme), m_Settings(settings), m_Draft(AppSettings::ThemesDirectory()),
		  m_DeleteQuestion(modals, "Delete colour scheme?"), m_RenameQuestion(modals, "Rename colour scheme")
	{
		// Constructed before GraphiteLayer calls ActiveScheme::Load, so this reads the setting
		// rather than the resolved value — the two agree, because Load reads the same key. An
		// unset key is handled by Select, not guarded against here: one place should know what an
		// empty name means.
		m_Draft.Select(m_Settings.GetColorScheme());
	}

	ImVec4 SchemeSection::DangerColour() const
	{
		return ImGui::ColorConvertU32ToFloat4(ReadEditorPalette(m_Active.Get()).DiagnosticError);
	}

	void SchemeSection::Apply()
	{
		m_Active.Set(m_Draft.GetPreview());
	}

	void SchemeSection::OnDeactivated()
	{
		// The question's popup is torn down with the section that renders it, and only this
		// section's render can release the gate — so it has to be given back here.
		m_DeleteQuestion.Withdraw();
		m_RenameQuestion.Withdraw();
		m_RenameShown = false;

		FlushColourEdit();
		m_Draft.Select(m_Settings.GetColorScheme());
		Apply();
	}

	void SchemeSection::FlushColourEdit()
	{
		if (m_ColourEdited)
		{
			m_Draft.Save();
			m_ColourEdited = false;
		}
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
				FlushColourEdit();
				m_Draft.Select(name);
				m_Settings.SetColorScheme(name);
				Apply();
			}
		}

		ImGui::EndCombo();
	}

	template<typename Which>
		requires std::is_enum_v<Which>
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

		// NoInputs: the row is a swatch and a name, and the picker opens on the swatch instead of
		// four RGBA fields crowding the row. DisplayHex makes the picker show the colour as the hex
		// the scheme file stores; the picker's right-click menu still switches it to RGB or HSV.
		ImGuiColorEditFlags flags = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
									ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_DisplayHex;

		if (readOnly)
		{
			// NoPicker as well: a built-in's swatch is for looking at, and a picker would let a drag
			// flicker the swatch to the dragged value before the draft's own refusal snaps it back.
			flags |= ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoOptions;
		}

		// .data() is safe only because every label is a string literal and therefore
		// null-terminated.
		if (ImGui::ColorEdit4(LabelOf(which).data(), rgba, flags) && !readOnly)
		{
			m_Draft.SetColor(which, Core::Color{rgba[0], rgba[1], rgba[2], rgba[3]});
			Apply();
			m_ColourEdited = true;
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
		requires std::is_enum_v<Which>
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

		const bool readOnly = m_Draft.IsReadOnly();

		// Released: the picker is open while the mouse is held on it, and the colour is then still
		// moving.
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			FlushColourEdit();
		}

		if (ImGui::Button("Duplicate"))
		{
			FlushColourEdit();

			if (m_Draft.Duplicate())
			{
				m_Settings.SetColorScheme(m_Draft.GetSelected());
				Apply();
			}
		}

		if (!readOnly)
		{
			ImGui::SameLine();

			if (ImGui::Button("Rename"))
			{
				FlushColourEdit();
				m_RenameQuestion.Ask(m_Draft.GetSelected());
			}

			ImGui::SameLine();

			if (ImGui::Button("Delete"))
			{
				m_DeleteQuestion.Ask(m_Draft.GetSelected());
			}
		}

		if (readOnly)
		{
			ImGui::TextDisabled("Built-in schemes cannot be edited. Duplicate one to change it.");
		}

		ImGui::Separator();

		RenderGroup<TokenKind>("Syntax", static_cast<std::size_t>(TokenKind::Count));
		RenderGroup<EditorColor>("Editor", static_cast<std::size_t>(EditorColor::Count));
		RenderGroup<ChromeColor>("Application", static_cast<std::size_t>(ChromeColor::Count));
		RenderGroup<GitColor>("Git", static_cast<std::size_t>(GitColor::Count));
		RenderGroup<SessionColor>("Claude sessions", static_cast<std::size_t>(SessionColor::Count));

		RenderRenameDialog();
		RenderDeleteDialog();
	}

	// Asked before deleting because the file is the user's own work and there is no undo.
	// A scheme that was renamed or deleted between the ask and the answer is no longer worth
	// asking about, which is what the predicate is for.
	void SchemeSection::RenderDeleteDialog()
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);

		const bool open =
			m_DeleteQuestion.Begin([this](const std::string& name) { return name == m_Draft.GetSelected(); });

		DialogLayout::AfterBegin();

		if (!open)
		{
			return;
		}

		const std::string* current = m_DeleteQuestion.GetCurrent();

		if (current == nullptr)
		{
			ImGui::CloseCurrentPopup();
			m_DeleteQuestion.End();
			return;
		}

		DialogLayout::Text("Delete \"" + *current + "\"? This cannot be undone.");
		DialogLayout::Gap();

		// No safe action, so Enter does nothing; Escape cancels.
		const DialogLayout::Choice choice = DialogLayout::ButtonRow({}, {.Label = "Delete"}, DangerColour());

		if (choice == DialogLayout::Choice::Destructive)
		{
			if (m_Draft.DeleteSelected())
			{
				m_Settings.SetColorScheme(m_Draft.GetSelected());
				Apply();
			}

			m_DeleteQuestion.Answer();
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			m_DeleteQuestion.Answer();
		}

		m_DeleteQuestion.End();
	}

	void SchemeSection::RenderRenameDialog()
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);

		const bool open =
			m_RenameQuestion.Begin([this](const std::string& name) { return name == m_Draft.GetSelected(); });

		DialogLayout::AfterBegin();

		if (!open)
		{
			m_RenameShown = false;
			return;
		}

		const std::string* current = m_RenameQuestion.GetCurrent();

		if (current == nullptr)
		{
			ImGui::CloseCurrentPopup();
			m_RenameQuestion.End();
			m_RenameShown = false;
			return;
		}

		// Copied out before Answer(), which drops the payload `current` points at.
		const std::string from = *current;

		const bool first = !m_RenameShown;

		if (first)
		{
			m_RenameShown = true;
			m_RenameBuffer.fill('\0');
			from.copy(m_RenameBuffer.data(), m_RenameBuffer.size() - 1);

			// The field is what the dialog is for, so it takes the keyboard as it opens.
			ImGui::SetKeyboardFocusHere();
		}

		ImGui::SetNextItemWidth(-FLT_MIN);
		const bool entered = ImGui::InputTextWithHint("##schemeName", "Scheme name", m_RenameBuffer.data(),
													  m_RenameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);

		const std::string name(m_RenameBuffer.data());
		const bool unchanged = name == from;
		const std::optional<std::string> problem = m_Draft.NameProblem(name);

		// An empty field says so through its hint and the disabled button, and so does the name it
		// started with; anything else that will not do is said under the field.
		if (problem.has_value() && !name.empty() && !unchanged)
		{
			DialogLayout::ErrorText(*problem, DangerColour());
		}

		DialogLayout::Gap();

		const bool acceptable = !problem.has_value();

		const DialogLayout::Choice choice =
			DialogLayout::ButtonRow({.Label = "Rename", .Enabled = acceptable}, {}, DangerColour());

		if (acceptable && (choice == DialogLayout::Choice::Safe || entered))
		{
			FlushColourEdit();

			if (m_Draft.RenameSelected(name))
			{
				m_Settings.SetColorScheme(name);
				Apply();
			}

			m_RenameQuestion.Answer();
			m_RenameShown = false;
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			m_RenameQuestion.Answer();
			m_RenameShown = false;
		}

		m_RenameQuestion.End();
	}
}
