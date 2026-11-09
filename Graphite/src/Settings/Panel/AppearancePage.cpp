#include "Settings/Panel/AppearancePage.h"

#include "Settings/AppSettings.h"
#include "Settings/InterfaceScale.h"
#include "Settings/Panel/SettingsLayout.h"
#include "Settings/Panel/SettingsSearch.h"
#include "Theme/ActiveScheme.h"
#include "Theme/BuiltInSchemes.h"
#include "Theme/ColorScheme.h"
#include "Theme/Palettes.h"
#include "UI/Dialogs/DialogLayout.h"
#include "Core/ImGui/IconButton.h"
#include "UI/ToastEvent.h"

#include "Core/App/Redraw.h"
#include "Core/Events/EventBus.h"
#include "Core/ImGui/Animation.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/Slider.h"
#include "Core/ImGui/TextButton.h"
#include "Core/ImGui/UiScale.h"
#include "Core/Platform/Shell.h"

#include <IconsLucide.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {

		constexpr const char* kBuiltInSchemeDescription = "Built-in schemes cannot be edited. Duplicate one to change it.";
		constexpr const char* kOwnSchemeDescription = "A copy of your own: rename it, delete it, or change its colours below.";
		constexpr const char* kColourNote = "Dimmed names are inherited from the built-in scheme. \xE2\x86\xBA puts an overridden colour back.";
		constexpr const char* kBuiltInColourDescription = "Built-in scheme: duplicate it to change this colour.";
		constexpr const char* kLayoutNote = "Dimmed names are inherited from the built-in scheme. \xE2\x86\xBA puts an overridden measurement back.";
		constexpr const char* kBuiltInLayoutDescription = "Built-in scheme: duplicate it to change this measurement.";
		constexpr std::string_view kLayoutUnit = "px";
		constexpr const char* kResetLayoutTip = "Put back the inherited measurement";
		constexpr float kColourCellWidth = 230.0f;
		constexpr float kPercentPerScale = 100.0f;
		constexpr float kRevealSeconds = 1.5f;
		constexpr float kRevealStartOpacity = 0.35f;
		constexpr int kRevealScrollsUntilLayoutSettles = 2;
		constexpr const char* kNoText = "";
		constexpr unsigned char kUtf8ContinuationMask = 0xC0;
		constexpr unsigned char kUtf8ContinuationBits = 0x80;

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

		int ColumnsFor(float availableWidth)
		{
			return std::max(1, static_cast<int>(availableWidth / Core::Scaled(kColourCellWidth)));
		}

		std::string ColourGroupPrefix(std::string_view heading)
		{
			return std::string{heading} + "/";
		}

		std::string ColourOpenWith(std::string_view heading, std::string_view name)
		{
			return ColourGroupPrefix(heading) + std::string{name};
		}

		bool GrowsWithItsContent(const ImGuiWindow& window)
		{
			return (window.ChildFlags & ImGuiChildFlags_AutoResizeY) != 0;
		}

		ImGuiWindow* ScrollingWindowAround(ImGuiWindow* window)
		{
			while (GrowsWithItsContent(*window) && window->ParentWindow != nullptr)
			{
				window = window->ParentWindow;
			}

			return window;
		}
	}

	// WARNING: Constructed before GraphiteLayer calls ActiveScheme::Load, so this reads the setting rather than
	// the resolved value; the two agree, because Load reads the same key.
	AppearancePage::AppearancePage(ActiveScheme& scheme, AppSettings& settings, ModalGate& modals, Core::EventBus& events,
								   std::filesystem::path themesDirectory)
		: m_Active(scheme), m_Settings(settings), m_Events(events), m_ThemesDirectory(themesDirectory), m_Draft(std::move(themesDirectory)),
		  m_DeleteQuestion(modals, "Delete colour scheme?"), m_RenameQuestion(modals, "Rename colour scheme")
	{
		m_Draft.Select(m_Settings.GetColorScheme());
	}


	void AppearancePage::Apply()
	{
		m_Active.Set(m_Draft.GetPreview());
	}

	void AppearancePage::OnDeactivated()
	{
		// WARNING: The questions' popups are torn down with the page that renders them, and only this page
		// can release the modal gate, so it has to be given back here.
		m_DeleteQuestion.Withdraw();
		m_RenameQuestion.Withdraw();
		m_RenameShown = false;
		m_Revealed.reset();

		FlushColourEdit();
		m_Draft.Select(m_Settings.GetColorScheme());
		Apply();

		PublishDraftFailure();
	}

	void AppearancePage::OnOpenedFromSearch(std::string_view openWith)
	{
		m_Revealed = RevealedColour{.OpenWith = std::string{openWith}, .ScrollsLeft = kRevealScrollsUntilLayoutSettles};
	}

	bool AppearancePage::IsRevealed(std::string_view heading, std::string_view name) const
	{
		return m_Revealed.has_value() && m_Revealed->OpenWith == ColourOpenWith(heading, name);
	}

	bool AppearancePage::IsScrollingToGroup(std::string_view heading) const
	{
		return m_Revealed.has_value() && m_Revealed->ScrollsLeft > 0 && m_Revealed->OpenWith.starts_with(ColourGroupPrefix(heading));
	}

	ImU32 AppearancePage::RevealHighlight() const
	{
		const float progress = std::min(m_Revealed->SecondsShown / kRevealSeconds, 1.0f);
		ImVec4 colour = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
		colour.w = kRevealStartOpacity * (1.0f - progress * progress);

		return ImGui::GetColorU32(colour);
	}

	// WARNING: Reads the item drawn last, so it is called straight after the colour's group ends.
	void AppearancePage::ScrollToRevealedColour()
	{
		if (m_Revealed->ScrollsLeft == 0)
		{
			return;
		}

		--m_Revealed->ScrollsLeft;

		const ImRect colourRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		ImGui::ScrollToRect(ScrollingWindowAround(ImGui::GetCurrentWindow()), colourRect,
							ImGuiScrollFlags_AlwaysCenterY | ImGuiScrollFlags_NoScrollParent);
	}

	void AppearancePage::AdvanceReveal()
	{
		if (!m_Revealed.has_value())
		{
			return;
		}

		m_Revealed->SecondsShown += std::min(ImGui::GetIO().DeltaTime, Core::Animation::kLongestStepSeconds);

		if (m_Revealed->SecondsShown >= kRevealSeconds)
		{
			m_Revealed.reset();
		}
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
		return ICON_LC_PALETTE;
	}

	template<typename Which>
		requires std::is_enum_v<Which>
	void AppearancePage::DrawColourSwatch(Which which)
	{
		const bool readOnly = m_Draft.IsReadOnly();
		const Core::Color& shown = ColourFromScheme(m_Draft.GetPreview(), which);

		std::array<float, 4> channels = {shown.R, shown.G, shown.B, shown.A};

		ImGuiColorEditFlags flags = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoInputs;

		// WARNING: A built-in's swatch is for looking at: a picker would let a drag flicker it to the dragged
		// value before the draft's refusal snaps it back.
		if (readOnly)
		{
			flags |= ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoOptions;
		}

		if (ImGui::ColorEdit4("##swatch", channels.data(), flags) && !readOnly)
		{
			m_Draft.SetColor(which, Core::Color{channels[0], channels[1], channels[2], channels[3]});
			Apply();
			m_ColourEdited = true;
		}
	}

	template<typename Which>
		requires std::is_enum_v<Which>
	void AppearancePage::DrawColourReset(Which which)
	{
		const bool engaged = false;

		if (Core::DrawIconButton("##reset", ICON_LC_ROTATE_CCW, engaged, "Put back the inherited colour"))
		{
			m_Draft.Revert(which);
			Apply();
			m_Draft.Save();
		}
	}

	template<typename Which>
		requires std::is_enum_v<Which>
	void AppearancePage::DrawColour(Which which)
	{
		const bool readOnly = m_Draft.IsReadOnly();
		const bool overridden = m_Draft.IsOverridden(which);

		DrawColourSwatch(which);

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
		DrawColourReset(which);
	}

	template<typename Which>
		requires std::is_enum_v<Which>
	bool AppearancePage::ShowsReset(Which which) const
	{
		return !m_Draft.IsReadOnly() && m_Draft.IsOverridden(which);
	}

	template<typename Which>
		requires std::is_enum_v<Which>
	void AppearancePage::DrawColourResult(Which which)
	{
		if (ShowsReset(which))
		{
			DrawColourReset(which);
			ImGui::SameLine();
		}

		DrawColourSwatch(which);
	}

	template<typename Which>
		requires std::is_enum_v<Which>
	std::vector<SettingsRow> AppearancePage::FindColours(const char* heading, std::size_t count, std::span<const std::string> words)
	{
		const bool ownScheme = !m_Draft.IsReadOnly();
		const float swatchWidth = ImGui::GetFrameHeight();
		const float swatchAndResetWidth = swatchWidth + ImGui::GetStyle().ItemSpacing.x + Core::IconButtonSize();

		std::vector<SettingsRow> rows;

		for (std::size_t colourIndex = 0; colourIndex < count; ++colourIndex)
		{
			const Which which = static_cast<Which>(colourIndex);
			const std::string name{LabelOf(which)};
			const std::array<std::string_view, 1> texts{name};

			if (!EveryWordFound(words, texts))
			{
				continue;
			}

			const bool inherited = ownScheme && !m_Draft.IsOverridden(which);

			rows.push_back(SettingsRow{.Key = name,
									   .Title = name,
									   .Description = ownScheme ? kNoText : kBuiltInColourDescription,
									   .DimmedTitle = inherited,
									   .ControlWidth = ShowsReset(which) ? swatchAndResetWidth : swatchWidth,
									   .DrawControl = [this, which] { DrawColourResult(which); },
									   .OpenWith = ColourOpenWith(heading, name)});
		}

		return rows;
	}

	template<typename Which>
		requires std::is_enum_v<Which>
	void AppearancePage::DrawColourGrid(const char* heading, std::size_t count)
	{
		const int columnCount = ColumnsFor(ImGui::GetContentRegionAvail().x);

		if (!ImGui::BeginTable("##colours", columnCount, ImGuiTableFlags_SizingStretchSame))
		{
			return;
		}

		for (std::size_t colourIndex = 0; colourIndex < count; ++colourIndex)
		{
			const Which which = static_cast<Which>(colourIndex);
			const bool revealed = IsRevealed(heading, LabelOf(which));

			ImGui::TableNextColumn();

			if (revealed)
			{
				ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, RevealHighlight());
			}

			ImGui::PushID(static_cast<int>(colourIndex));
			ImGui::BeginGroup();
			DrawColour(which);
			ImGui::EndGroup();
			ImGui::PopID();

			if (revealed)
			{
				ScrollToRevealedColour();
				Core::Redraw::RequestIn(Core::Redraw::Clock::duration::zero());
			}
		}

		ImGui::EndTable();
	}

	template<typename Which>
		requires std::is_enum_v<Which>
	SettingsCard AppearancePage::DescribeColours(const char* heading, std::size_t count)
	{
		SettingsCard card{.Heading = heading, .Note = m_Draft.IsReadOnly() ? kNoText : kColourNote};

		std::vector<std::string> keywords = kColourKeywords;

		for (std::string& name : ColourNames<Which>(count))
		{
			keywords.push_back(std::move(name));
		}

		card.Rows.push_back(SettingsRow{.Title = heading,
										.Keywords = std::move(keywords),
										.DrawBlock = [this, heading, count] { DrawColourGrid<Which>(heading, count); },
										.DrawnEvenOutOfSight = IsScrollingToGroup(heading),
										.FindInside = [this, heading, count](std::span<const std::string> words) { return FindColours<Which>(heading, count, words); }});

		return card;
	}

	std::vector<SettingsCard> AppearancePage::Describe()
	{
		return {DescribeTheme(),
				DescribeInterface(),
				DescribeLayout(),
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
										.ControlWidth = SettingsLayout::ComboWidth(SchemeLabels(m_Draft.GetNames())),
										.DrawControl = [this] { DrawSchemeChoice(); }});
		card.Rows.push_back(SettingsRow{.Title = "This scheme",
										.Description = m_Draft.IsReadOnly() ? kBuiltInSchemeDescription : kOwnSchemeDescription,
										.Keywords = {"duplicate", "rename", "delete"},
										.ControlWidth = SettingsLayout::ButtonsWidth(SchemeActionLabels()),
										.DrawControl = [this] { DrawSchemeActions(); }});
		card.Rows.push_back(SettingsRow{.Title = "Themes folder",
										.Description = "Where your own schemes are kept, one file each.",
										.Keywords = {"open", "folder", "directory", "explorer", "files"},
										.ControlWidth = SettingsLayout::ButtonsWidth(std::vector<std::string>{"Open folder"}),
										.DrawControl = [this] { DrawThemesFolder(); }});

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

	bool AppearancePage::ShowsLayoutReset(LayoutMetric metric) const
	{
		return !m_Draft.IsReadOnly() && m_Draft.IsOverridden(metric);
	}

	void AppearancePage::DrawLayoutMetric(LayoutMetric metric)
	{
		const bool readOnly = m_Draft.IsReadOnly();

		if (ShowsLayoutReset(metric))
		{
			if (Core::DrawIconButton("##reset", ICON_LC_ROTATE_CCW, false, kResetLayoutTip))
			{
				m_Draft.Revert(metric);
				Apply();
				m_Draft.Save();
			}

			ImGui::SameLine();
		}

		const LayoutRange range = RangeOf(metric);
		int value = static_cast<int>(m_Draft.GetPreview().Layout[static_cast<std::size_t>(metric)]);

		ImGui::BeginDisabled(readOnly);

		if (Core::SliderInt("##layout", &value, static_cast<int>(range.Smallest), static_cast<int>(range.Largest), kLayoutUnit,
							SettingsLayout::SliderWidth()) &&
			!readOnly)
		{
			m_Draft.SetLayout(metric, static_cast<float>(value));
			Apply();
			m_ColourEdited = true;
		}

		ImGui::EndDisabled();
	}

	SettingsCard AppearancePage::DescribeLayout()
	{
		SettingsCard card{.Heading = "Layout", .Note = m_Draft.IsReadOnly() ? kNoText : kLayoutNote};

		const float resetWidth = ImGui::GetStyle().ItemSpacing.x + Core::IconButtonSize();
		const bool ownScheme = !m_Draft.IsReadOnly();

		for (std::size_t metricIndex = 0; metricIndex < kLayoutMetricCount; ++metricIndex)
		{
			const LayoutMetric metric = static_cast<LayoutMetric>(metricIndex);

			card.Rows.push_back(SettingsRow{.Title = std::string{LabelOf(metric)},
											.Description = ownScheme ? kNoText : kBuiltInLayoutDescription,
											.DimmedTitle = ownScheme && !m_Draft.IsOverridden(metric),
											.Keywords = {"layout", "spacing", "gap", "radius", "rounded", "round", "border", "scrollbar", "size"},
											.ControlWidth = SettingsLayout::SliderWidth() + (ShowsLayoutReset(metric) ? resetWidth : 0.0f),
											.DrawControl = [this, metric] { DrawLayoutMetric(metric); }});
		}

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

	std::vector<std::string> AppearancePage::SchemeLabels(const std::vector<std::string>& names)
	{
		std::vector<std::string> labels;
		labels.reserve(names.size());

		for (const std::string& name : names)
		{
			labels.push_back(SchemeDisplayName(name));
		}

		return labels;
	}

	void AppearancePage::DrawSchemeChoice()
	{
		const std::vector<std::string>& names = m_Draft.GetNames();
		std::size_t selected = IndexOfName(names, m_Draft.GetSelected());

		if (!SettingsLayout::Choice("##colourScheme", SchemeLabels(names), selected, SchemeDisplayName(m_Draft.GetSelected())))
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
		if (Core::TextButton("Duplicate"))
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

		if (Core::TextButton("Rename"))
		{
			FlushColourEdit();
			m_RenameQuestion.Ask(m_Draft.GetSelected());
		}

		ImGui::SameLine();

		if (Core::TextButton("Delete"))
		{
			m_DeleteQuestion.Ask(m_Draft.GetSelected());
		}
	}

	void AppearancePage::DrawThemesFolder()
	{
		if (!Core::TextButton("Open folder"))
		{
			return;
		}

		// The folder is created with the first saved scheme, so before that there is nothing to open.
		std::error_code ignored;
		std::filesystem::create_directories(m_ThemesDirectory, ignored);

		if (const Core::Result<void> opened = Core::Shell::OpenFolder(m_ThemesDirectory); !opened)
		{
			m_Events.Publish(ToastEvent{.Kind = Core::ToastKind::Error,
										.Title = "Cannot open themes folder",
										.Message = opened.GetError().Message});
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

		AdvanceReveal();
	}

	void AppearancePage::RenderDialogs()
	{
		RenderRenameDialog();
		RenderDeleteDialog();

		PublishDraftFailure();
	}

	void AppearancePage::RenderDeleteDialog()
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);

		const std::string* askedName =
			m_DeleteQuestion.Begin([this](const std::string& name) { return name == m_Draft.GetSelected(); });

		DialogLayout::AfterBegin();

		if (askedName == nullptr)
		{
			return;
		}

		DialogLayout::Text("Delete \"" + *askedName + "\"? This cannot be undone.");
		DialogLayout::Gap();

		const DialogLayout::ButtonSpec noSafeAction{};
		const DialogLayout::Choice choice = DialogLayout::ButtonRow(noSafeAction, {.Label = "Delete"}, DangerColourOf(m_Active.Get()));

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

		const std::string* askedName =
			m_RenameQuestion.Begin([this](const std::string& name) { return name == m_Draft.GetSelected(); });

		DialogLayout::AfterBegin();

		if (askedName == nullptr)
		{
			m_RenameShown = false;
			return;
		}

		// WARNING: Copied out before Answer(), which drops the payload askedName points at.
		const std::string originalName = *askedName;

		const bool firstFrame = !m_RenameShown;

		if (firstFrame)
		{
			m_RenameShown = true;
			m_RenameError.clear();
			m_RenameBuffer.fill('\0');

			std::size_t copiedLength = std::min(originalName.size(), m_RenameBuffer.size() - 1);

			while (copiedLength > 0 && copiedLength < originalName.size() &&
				   (static_cast<unsigned char>(originalName[copiedLength]) & kUtf8ContinuationMask) == kUtf8ContinuationBits)
			{
				--copiedLength;
			}

			originalName.copy(m_RenameBuffer.data(), copiedLength);

			ImGui::SetKeyboardFocusHere();
		}

		ImGui::SetNextItemWidth(-FLT_MIN);
		const bool enterPressed = ImGui::InputTextWithHint("##schemeName", "Scheme name", m_RenameBuffer.data(),
														   m_RenameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);

		if (ImGui::IsItemEdited())
		{
			m_RenameError.clear();
		}

		const std::string name(m_RenameBuffer.data());
		const bool unchanged = name == originalName;
		const std::optional<std::string> problem = m_Draft.NameProblem(name);
		const bool problemNeedsSaying = problem.has_value() && !name.empty() && !unchanged;

		if (problemNeedsSaying)
		{
			DialogLayout::ErrorText(*problem, DangerColourOf(m_Active.Get()));
		}

		if (!m_RenameError.empty())
		{
			DialogLayout::ErrorText(m_RenameError, DangerColourOf(m_Active.Get()));
		}

		DialogLayout::Gap();

		const bool acceptable = !problem.has_value() && !unchanged;

		const DialogLayout::Choice choice =
			DialogLayout::ButtonRow({.Label = "Rename", .Enabled = acceptable});

		if (acceptable && (choice == DialogLayout::Choice::Safe || enterPressed))
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
