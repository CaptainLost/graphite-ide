#include "Settings/Panel/AppearancePage.h"

#include "Settings/AppSettings.h"
#include "Settings/InterfaceScale.h"
#include "Settings/Panel/SettingsLayout.h"
#include "Theme/ActiveScheme.h"
#include "Theme/ColorScheme.h"
#include "Theme/Palettes.h"
#include "UI/DialogLayout.h"
#include "UI/IconButton.h"
#include "UI/ToastEvent.h"

#include "Core/Base/Ascii.h"
#include "Core/Events/EventBus.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/UiScale.h"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {

		constexpr const char* kBuiltInSchemeDescription = "Built-in schemes cannot be edited. Duplicate one to change it.";
		constexpr const char* kOwnSchemeDescription = "A copy of your own: rename it, delete it, or change its colours below.";
		constexpr const char* kColourNote = "Dimmed names are inherited from the built-in scheme. \xE2\x86\xBA puts an overridden colour back.";
		constexpr float kColourCellWidth = 230.0f;
		constexpr float kPercentPerScale = 100.0f;

		const std::vector<std::string> kColourKeywords{"colour", "color"};

		std::size_t IndexOfName(const std::vector<std::string>& names, const std::string& name)
		{
			for (std::size_t nameIndex = 0; nameIndex < names.size(); ++nameIndex)
			{
				if (names[nameIndex] == name)
				{
					return nameIndex;
				}
			}

			return names.size();
		}

		std::vector<std::string> InterfaceScaleLabels()
		{
			std::vector<std::string> labels;

			for (const int percent : kInterfaceScalePercents)
			{
				labels.push_back(std::to_string(percent) + "%");
			}

			return labels;
		}

		std::size_t IndexOfPercent(int percent)
		{
			for (std::size_t percentIndex = 0; percentIndex < kInterfaceScalePercents.size(); ++percentIndex)
			{
				if (kInterfaceScalePercents[percentIndex] == percent)
				{
					return percentIndex;
				}
			}

			return kInterfaceScalePercents.size();
		}

		template<typename Which>
			requires std::is_enum_v<Which>
		std::vector<std::string> ColourNames(std::size_t count)
		{
			std::vector<std::string> names;

			for (std::size_t colourIndex = 0; colourIndex < count; ++colourIndex)
			{
				names.push_back(std::string{LabelOf(static_cast<Which>(colourIndex))});
			}

			return names;
		}

		template<typename Which>
			requires std::is_enum_v<Which>
		std::string DescribeColourMatches(std::size_t count, std::string_view query)
		{
			const std::string loweredQuery = Core::Lowered(query);
			std::string match;
			std::size_t matchCount = 0;

			for (const std::string& name : ColourNames<Which>(count))
			{
				if (Core::LoweredTextContains(name, loweredQuery))
				{
					match = name;
					++matchCount;
				}
			}

			if (matchCount == 0)
			{
				return {};
			}

			return matchCount == 1 ? match : std::to_string(matchCount) + " colours match";
		}

		int ColumnsFor(float availableWidth)
		{
			return std::max(1, static_cast<int>(availableWidth / Core::Scaled(kColourCellWidth)));
		}
	}

	// Constructed before GraphiteLayer calls ActiveScheme::Load, so this reads the setting rather than
	// the resolved value; the two agree, because Load reads the same key.
	AppearancePage::AppearancePage(ActiveScheme& scheme, AppSettings& settings, ModalGate& modals, Core::EventBus& events,
								   std::filesystem::path themesDirectory)
		: m_Active(scheme), m_Settings(settings), m_Events(events), m_Draft(std::move(themesDirectory)),
		  m_DeleteQuestion(modals, "Delete colour scheme?"), m_RenameQuestion(modals, "Rename colour scheme")
	{
		m_Draft.Select(m_Settings.GetColorScheme());
	}

	ImVec4 AppearancePage::DangerColour() const
	{
		return ImGui::ColorConvertU32ToFloat4(ReadEditorPalette(m_Active.Get()).DiagnosticError);
	}

	void AppearancePage::Apply()
	{
		m_Active.Set(m_Draft.GetPreview());
	}

	void AppearancePage::OnDeactivated()
	{
		// The question's popup is torn down with the page that renders it, and only this page's
		// render can release the gate, so it has to be given back here.
		m_DeleteQuestion.Withdraw();
		m_RenameQuestion.Withdraw();
		m_RenameShown = false;

		FlushColourEdit();
		m_Draft.Select(m_Settings.GetColorScheme());
		Apply();

		PublishDraftFailure();
	}

	void AppearancePage::PublishDraftFailure()
	{
		while (std::optional<ToastEvent> failure = m_Draft.TakeFailure())
		{
			m_Events.Publish(std::move(*failure));
		}
	}

	void AppearancePage::FlushColourEdit()
	{
		if (m_ColourEdited)
		{
			m_Draft.Save();
			m_ColourEdited = false;
		}
	}

	const char* AppearancePage::GetIcon() const
	{
		return ICON_MDI_PALETTE_OUTLINE;
	}

	template<typename Which>
		requires std::is_enum_v<Which>
	void AppearancePage::DrawColour(Which which)
	{
		const bool readOnly = m_Draft.IsReadOnly();
		const bool overridden = m_Draft.IsOverridden(which);
		const Core::Color& shown = ColourFromScheme(m_Draft.GetPreview(), which);

		// A local array rather than &shown.R: the four floats are almost certainly contiguous, but
		// "almost certainly" is not a thing to hand a pointer to.
		float rgba[4] = {shown.R, shown.G, shown.B, shown.A};

		ImGuiColorEditFlags flags = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoInputs;

		// A built-in's swatch is for looking at: a picker would let a drag flicker it to the dragged
		// value before the draft's refusal snaps it back.
		if (readOnly)
		{
			flags |= ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoOptions;
		}

		if (ImGui::ColorEdit4("##swatch", rgba, flags) && !readOnly)
		{
			m_Draft.SetColor(which, Core::Color{rgba[0], rgba[1], rgba[2], rgba[3]});
			Apply();
			m_ColourEdited = true;
		}

		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();

		const std::string label{LabelOf(which)};

		if (!readOnly && !overridden)
		{
			ImGui::TextDisabled("%s", label.c_str());
			return;
		}

		ImGui::TextUnformatted(label.c_str());

		if (readOnly)
		{
			return;
		}

		ImGui::SameLine();

		const bool engaged = false;

		if (DrawIconButton("##reset", ICON_MDI_RESTORE, engaged, "Put back the inherited colour"))
		{
			m_Draft.Revert(which);
			Apply();
			m_Draft.Save();
		}
	}

	template<typename Which>
		requires std::is_enum_v<Which>
	void AppearancePage::DrawColourGrid(std::size_t count)
	{
		const int columns = ColumnsFor(ImGui::GetContentRegionAvail().x);

		if (!ImGui::BeginTable("##colours", columns, ImGuiTableFlags_SizingStretchSame))
		{
			return;
		}

		for (std::size_t colourIndex = 0; colourIndex < count; ++colourIndex)
		{
			ImGui::TableNextColumn();
			ImGui::PushID(static_cast<int>(colourIndex));
			DrawColour(static_cast<Which>(colourIndex));
			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	template<typename Which>
		requires std::is_enum_v<Which>
	SettingsCard AppearancePage::DescribeColours(const char* heading, std::size_t count)
	{
		SettingsCard card{.Heading = heading, .Note = m_Draft.IsReadOnly() ? "" : kColourNote};

		std::vector<std::string> keywords = kColourKeywords;

		for (std::string& name : ColourNames<Which>(count))
		{
			keywords.push_back(std::move(name));
		}

		card.Rows.push_back(SettingsRow{.Title = heading,
										.Keywords = std::move(keywords),
										.DrawBlock = [this, count] { DrawColourGrid<Which>(count); },
										.DescribeMatches = [count](std::string_view query) { return DescribeColourMatches<Which>(count, query); }});

		return card;
	}

	std::vector<SettingsCard> AppearancePage::Describe()
	{
		return {DescribeTheme(),
				DescribeInterface(),
				DescribeColours<TokenKind>("Syntax colours", static_cast<std::size_t>(TokenKind::Count)),
				DescribeColours<EditorColor>("Editor colours", static_cast<std::size_t>(EditorColor::Count)),
				DescribeColours<ChromeColor>("Application colours", static_cast<std::size_t>(ChromeColor::Count)),
				DescribeColours<GitColor>("Git colours", static_cast<std::size_t>(GitColor::Count)),
				DescribeColours<SessionColor>("Claude sessions colours", static_cast<std::size_t>(SessionColor::Count))};
	}

	SettingsCard AppearancePage::DescribeTheme()
	{
		SettingsCard card{.Heading = "Theme"};

		card.Rows.push_back(SettingsRow{.Title = "Colour scheme",
										.Description = "Colours for the editor, the panels and the diffs.",
										.Keywords = {"theme", "dark", "light", "color"},
										.ControlWidth = SettingsLayout::ComboWidth(m_Draft.GetNames()),
										.DrawControl = [this] { DrawSchemeChoice(); }});
		card.Rows.push_back(SettingsRow{.Title = "This scheme",
										.Description = m_Draft.IsReadOnly() ? kBuiltInSchemeDescription : kOwnSchemeDescription,
										.Keywords = {"duplicate", "rename", "delete"},
										.ControlWidth = SettingsLayout::ButtonsWidth(SchemeActionLabels()),
										.DrawControl = [this] { DrawSchemeActions(); }});

		return card;
	}

	SettingsCard AppearancePage::DescribeInterface()
	{
		SettingsCard card{.Heading = "Interface"};

		card.Rows.push_back(SettingsRow{.Title = "Interface scale",
										.Description = "On top of the scale Windows sets for each monitor. Code text has its own size, under Editor.",
										.Keywords = {"zoom", "dpi", "size"},
										.ControlWidth = SettingsLayout::ComboWidth(InterfaceScaleLabels()),
										.DrawControl = [this] { DrawInterfaceScale(); }});

		return card;
	}

	std::vector<std::string> AppearancePage::SchemeActionLabels() const
	{
		if (m_Draft.IsReadOnly())
		{
			return {"Duplicate"};
		}

		return {"Duplicate", "Rename", "Delete"};
	}

	void AppearancePage::DrawSchemeChoice()
	{
		const std::vector<std::string>& names = m_Draft.GetNames();
		std::size_t selected = IndexOfName(names, m_Draft.GetSelected());

		if (!SettingsLayout::Choice("##colourScheme", names, selected, m_Draft.GetSelected()))
		{
			return;
		}

		const std::string chosen = names[selected];
		FlushColourEdit();
		m_Draft.Select(chosen);
		m_Settings.SetColorScheme(chosen);
		Apply();
	}

	void AppearancePage::DrawSchemeActions()
	{
		if (ImGui::Button("Duplicate"))
		{
			FlushColourEdit();

			if (m_Draft.Duplicate())
			{
				m_Settings.SetColorScheme(m_Draft.GetSelected());
				Apply();
			}
		}

		if (m_Draft.IsReadOnly())
		{
			return;
		}

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

	void AppearancePage::DrawInterfaceScale()
	{
		const std::vector<std::string> labels = InterfaceScaleLabels();
		std::size_t selected = IndexOfPercent(m_Settings.GetInterfaceScalePercent());

		if (!SettingsLayout::Choice("##interfaceScale", labels, selected))
		{
			return;
		}

		const int percent = kInterfaceScalePercents[selected];
		Core::ImGuiRenderer::SetInterfaceScale(static_cast<float>(percent) / kPercentPerScale);
		m_Settings.SetInterfaceScalePercent(percent);
	}

	void AppearancePage::Update()
	{
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			FlushColourEdit();
		}
	}

	void AppearancePage::RenderDialogs()
	{
		RenderRenameDialog();
		RenderDeleteDialog();

		// Last, after every place that saves, duplicates or deletes.
		PublishDraftFailure();
	}

	// Asked before deleting because the file is the user's own work and there is no undo.
	// A scheme that was renamed or deleted between the ask and the answer is no longer worth
	// asking about, which is what the predicate is for.
	void AppearancePage::RenderDeleteDialog()
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

	void AppearancePage::RenderRenameDialog()
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
			m_RenameError.clear();
			m_RenameBuffer.fill('\0');

			// A name written outside Graphite can be longer than the field; it is cut at the
			// start of a character, never inside one.
			std::size_t length = std::min(from.size(), m_RenameBuffer.size() - 1);

			while (length > 0 && length < from.size() && (static_cast<unsigned char>(from[length]) & 0xC0) == 0x80)
			{
				--length;
			}

			from.copy(m_RenameBuffer.data(), length);

			// The field is what the dialog is for, so it takes the keyboard as it opens.
			ImGui::SetKeyboardFocusHere();
		}

		ImGui::SetNextItemWidth(-FLT_MIN);
		const bool entered = ImGui::InputTextWithHint("##schemeName", "Scheme name", m_RenameBuffer.data(),
													  m_RenameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);

		if (ImGui::IsItemEdited())
		{
			m_RenameError.clear();
		}

		const std::string name(m_RenameBuffer.data());
		const bool unchanged = name == from;
		const std::optional<std::string> problem = m_Draft.NameProblem(name);

		// An empty field says so through its hint and the disabled button, and so does the name it
		// started with; anything else that will not do is said under the field.
		if (problem.has_value() && !name.empty() && !unchanged)
		{
			DialogLayout::ErrorText(*problem, DangerColour());
		}

		if (!m_RenameError.empty())
		{
			DialogLayout::ErrorText(m_RenameError, DangerColour());
		}

		DialogLayout::Gap();

		const bool acceptable = !problem.has_value() && !unchanged;

		const DialogLayout::Choice choice =
			DialogLayout::ButtonRow({.Label = "Rename", .Enabled = acceptable}, {}, DangerColour());

		if (acceptable && (choice == DialogLayout::Choice::Safe || entered))
		{
			FlushColourEdit();

			if (m_Draft.RenameSelected(name))
			{
				m_Settings.SetColorScheme(name);
				Apply();
				m_RenameQuestion.Answer();
				m_RenameShown = false;
			}
			else
			{
				// Kept open: the file may be in use or read-only, and the name can be tried again.
				m_RenameError = "Could not rename the scheme. Its file may be in use or read-only.";
			}
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			m_RenameQuestion.Answer();
			m_RenameShown = false;
		}

		m_RenameQuestion.End();
	}
}
