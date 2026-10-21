#include "Console/ConsolePanel.h"

#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/TextButton.h"
#include "Core/ImGui/UiScale.h"

#include "Core/Base/Base.h"

#include "Core/ImGui/SelectionGesture.h"
#include "Core/ImGui/TextMetrics.h"
#include "Core/Text/Columns.h"

#include "Theme/Palettes.h"
#include "UI/ClipboardCopy.h"
#include "UI/Lists/FilterField.h"
#include "UI/TickBox.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		constexpr float kRowSpacing = 1.0f;
		constexpr float kRowItemSpacingX = 4.0f;
		constexpr float kFilterBoxWidth = 240.0f;
		constexpr float kLevelComboWidth = 80.0f;
		constexpr float kScrollToBottom = 1.0f;
		constexpr ImVec2 kFillAvailableSpace{0.0f, 0.0f};

		ConsoleLevelColours ReadConsoleLevelColours(const ResolvedScheme& scheme)
		{
			const EditorPalette palette = ReadEditorPalette(scheme);

			return ConsoleLevelColours{.Text = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_Text)),
									   .Quiet = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)),
									   .Warning = palette.DiagnosticWarning,
									   .Error = palette.DiagnosticError};
		}

		std::string JoinEntries(const std::vector<Core::LogEntry>& entries)
		{
			std::string joined;

			for (const Core::LogEntry& entry : entries)
			{
				joined.append(Core::ToString(entry.Level));
				joined.append(" ");
				joined.append(entry.Message);
				joined.append("\n");
			}

			return joined;
		}
	}

	ConsolePanel::ConsolePanel(Core::Ref<Core::RingBufferSink> sink, Core::EventBus& events, const ActiveScheme& scheme)
		: Core::Panel("console", "Console", "tools", ICON_LC_MESSAGE_SQUARE_TEXT), m_Sink(std::move(sink)),
		  m_Events(events), m_Scheme(scheme)
	{
	}

	void ConsolePanel::OnImGuiRender()
	{
		const std::uint64_t revision = m_Sink->GetRevision();

		if (revision != m_SeenRevision || m_SeenFilter != m_Filter.InputBuf ||
			m_SeenLevel != m_MinimumLevel)
		{
			m_Entries = m_Sink->TakeSnapshot();
			CollectShownEntries();

			m_SeenRevision = revision;
			m_SeenFilter = m_Filter.InputBuf;
			m_SeenLevel = m_MinimumLevel;
		}

		RenderToolbar();
		ImGui::Separator();
		RenderEntries();
	}

	void ConsolePanel::CollectShownEntries()
	{
		m_Shown.clear();

		for (const Core::LogEntry& entry : m_Entries)
		{
			if (IsShownAtLevel(entry.Level, m_MinimumLevel) && m_Filter.PassFilter(entry.Message.c_str()))
			{
				m_Shown.push_back(entry);
			}
		}
	}

	void ConsolePanel::RenderToolbar()
	{
		if (Core::TextButton("Clear"))
		{
			m_Sink->Clear();
			m_Entries.clear();
			m_Shown.clear();
			m_Selection.Clear();
			m_LastCount = 0;
		}

		ImGui::SameLine();

		if (Core::TextButton("Copy all"))
		{
			CopyToClipboard(m_Events, "console output", JoinEntries(m_Shown));
		}

		ImGui::SameLine();
		DrawTickBox("Auto-scroll", &m_AutoScroll);

		ImGui::SameLine();
		if (DrawFilterField("##consoleFilter", m_Filter.InputBuf, IM_ARRAYSIZE(m_Filter.InputBuf),
							{.Hint = "Filter (inc,-exc)", .Width = Core::Scaled(kFilterBoxWidth)}))
		{
			m_Filter.Build();
		}

		ImGui::SameLine();
		RenderLevelCombo();

		ImGui::SameLine();
		ImGui::TextDisabled("%s", ConsoleEntryCount(m_Shown.size(), m_Entries.size()).c_str());
	}

	void ConsolePanel::RenderLevelCombo()
	{
		const std::string preview{ConsoleLevelName(m_MinimumLevel)};

		ImGui::SetNextItemWidth(Core::Scaled(kLevelComboWidth));

		if (!ImGui::BeginCombo("##consoleLevel", preview.c_str()))
		{
			return;
		}

		for (const Core::LogLevel level : kConsoleLevels)
		{
			const std::string name{ConsoleLevelName(level)};

			if (ImGui::Selectable(name.c_str(), level == m_MinimumLevel))
			{
				m_MinimumLevel = level;
			}
		}

		ImGui::EndCombo();
	}

	void ConsolePanel::RenderEntries()
	{
		if (!ImGui::BeginChild("##consoleOutput", kFillAvailableSpace,
							   ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
		{
			ImGui::EndChild();
			return;
		}

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
							ImVec2(Core::Scaled(kRowItemSpacingX), Core::Scaled(kRowSpacing)));

		const ConsoleLayout layout = MeasureConsoleLayout(Core::Scaled(kRowSpacing));

		// WARNING: Nothing in this child can become active and its scrollbar takes the ActiveId
		// inside BeginChild, so a drag on the bar is already seen as claimed when this runs.
		UpdateSelection(layout);

		Core::PushCodeFont();

		ImDrawList* const drawList = ImGui::GetWindowDrawList();
		const float lineHeight = ImGui::GetTextLineHeight();
		const ConsoleLevelColours colours = ReadConsoleLevelColours(m_Scheme.Get());

		for (std::size_t row = 0; row < m_Shown.size(); ++row)
		{
			const Core::LogEntry& entry = m_Shown[row];
			const std::size_t columns = Core::ColumnCount(entry.Message, Core::DefaultTabWidth);

			if (const std::optional<Core::ColumnSpan> span =
					m_Selection.SpanOn(static_cast<std::int64_t>(entry.Sequence), columns))
			{
				const float top = layout.OriginY + static_cast<float>(row) * layout.RowHeight;

				Core::DrawSelectionSpan(*drawList,
										layout.OriginX + static_cast<float>(span->From) * layout.CellWidth,
										layout.OriginX + static_cast<float>(span->To) * layout.CellWidth,
										top, lineHeight);
			}

			ImGui::PushStyleColor(ImGuiCol_Text, ColourOfConsoleLevel(entry.Level, colours));
			ImGui::TextUnformatted(entry.Message.c_str());
			ImGui::PopStyleColor();
		}

		ImGui::PopStyleVar();

		ImGui::PopFont();

		FollowTailOnlyWhenEntriesArrived();

		ImGui::EndChild();
	}

	void ConsolePanel::UpdateSelection(const ConsoleLayout& layout)
	{
		Core::SelectionSource source;

		source.PointAt = [this, &layout](ImVec2 screen) {
			return PointAt(m_Shown, layout, screen.x, screen.y);
		};
		source.CharacterAt = [this, &layout](ImVec2 screen) {
			return CharacterAt(m_Shown, layout, screen.x, screen.y);
		};
		source.WordAt = [this](Core::TextPoint at) {
			return ConsoleWordAt(m_Shown, at);
		};
		source.LineLength = [this](std::int64_t line) {
			return ConsoleLineLength(m_Shown, line);
		};
		source.Everything = [this]() {
			return EverythingShown(m_Shown);
		};
		source.SelectedText = [this]() {
			return SelectedConsoleText(m_Selection, m_Shown);
		};

		Core::SelectionGestureOptions options;
		options.ScrollsVertically = true;
		options.ScrollsHorizontally = true;
		options.CopyKey = true;
		options.SelectAllKey = true;
		options.MenuId = "##consoleMenu";

		const Core::SelectionGestureResult result = m_Gesture.Update(m_Selection, source, options);

		if (result.Copied && result.FromMenu)
		{
			ReportCopied(m_Events, "selection", result.CopiedText);
		}
	}

	void ConsolePanel::FollowTailOnlyWhenEntriesArrived()
	{
		if (m_AutoScroll && m_Entries.size() != m_LastCount)
		{
			ImGui::SetScrollHereY(kScrollToBottom);
		}

		m_LastCount = m_Entries.size();
	}
}
