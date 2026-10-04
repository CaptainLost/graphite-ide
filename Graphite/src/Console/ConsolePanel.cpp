#include "Console/ConsolePanel.h"

#include "Core/ImGui/UiScale.h"

#include "Core/Base/Base.h"

#include "Core/ImGui/SelectionGesture.h"
#include "Core/ImGui/TextInput.h"
#include "Core/ImGui/TextMetrics.h"
#include "Core/Text/Columns.h"

#include "Editor/StatusBar/ClipboardCopy.h"

#include <IconsMaterialDesignIcons.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		ImVec4 ColorFor(Core::LogLevel level)
		{
			switch (level)
			{
			case Core::LogLevel::Trace:
				return ImVec4(0.55f, 0.58f, 0.62f, 1.0f);
			case Core::LogLevel::Info:
				return ImVec4(0.84f, 0.86f, 0.89f, 1.0f);
			case Core::LogLevel::Warn:
				return ImVec4(0.95f, 0.75f, 0.30f, 1.0f);
			case Core::LogLevel::Error:
				return ImVec4(0.94f, 0.42f, 0.42f, 1.0f);
			case Core::LogLevel::Critical:
				// Louder than Error, never calmer: it fell through to white until this case existed.
				return ImVec4(1.0f, 0.25f, 0.55f, 1.0f);
			}

			return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
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

		// The rows are laid out here rather than read back off ImGui, because the sweep has to
		// know where a row is before that row is drawn: the highlight goes under the text of
		// the same frame the mouse moved in, and a frame of lag behind the pointer shows. The
		// face is monospaced and the spacing is pushed by this panel, so this arithmetic is the
		// one ImGui walks through as the rows are emitted.
		ConsoleLayout MeasureRows(float rowSpacing)
		{
			const ImVec2 origin = ImGui::GetCursorScreenPos();

			ConsoleLayout layout;
			layout.OriginX = origin.x;
			layout.OriginY = origin.y;
			layout.RowHeight = ImGui::GetTextLineHeight() + rowSpacing;
			layout.CellWidth = Core::MonospaceCellWidth();

			return layout;
		}
	}

	ConsolePanel::ConsolePanel(Core::Ref<Core::RingBufferSink> sink, Core::EventBus& events)
		: Core::Panel("console", "Console", "tools", ICON_MDI_MESSAGE_TEXT_OUTLINE), m_Sink(std::move(sink)),
		  m_Events(events)
	{
	}

	void ConsolePanel::OnImGuiRender()
	{
		// Not every frame: taking the snapshot copies every message the buffer holds, and
		// filtering copies the shown ones again, on frames where nothing has changed.
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
		if (ImGui::Button("Clear"))
		{
			m_Sink->Clear();
			m_Entries.clear();
			m_Shown.clear();
			m_Selection.Clear();
			m_LastCount = 0;
		}

		ImGui::SameLine();

		// What the reader sees, as a selection is: lines the level or the filter hides are
		// not copied with the rest.
		if (ImGui::Button("Copy all"))
		{
			CopyToClipboard(m_Events, "console output", JoinEntries(m_Shown));
		}

		ImGui::SameLine();
		ImGui::Checkbox("Auto-scroll", &m_AutoScroll);

		ImGui::SameLine();
		ImGui::SetNextItemWidth(Core::Scaled(240.0f));
		if (Core::InputTextWithHint("##consoleFilter", "Filter (inc,-exc)", m_FilterText))
		{
			// The filter's own buffer is fixed; a longer pattern is cut, not overrun.
			const std::size_t length = std::min(m_FilterText.size(), sizeof(m_Filter.InputBuf) - 1);
			std::copy_n(m_FilterText.data(), length, m_Filter.InputBuf);
			m_Filter.InputBuf[length] = '\0';
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

		ImGui::SetNextItemWidth(Core::Scaled(80.0f));

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
		if (!ImGui::BeginChild("##consoleOutput", ImVec2(0.0f, 0.0f),
							   ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
		{
			ImGui::EndChild();
			return;
		}

		constexpr float kRowSpacing = 1.0f;

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Core::Scaled(4.0f), Core::Scaled(kRowSpacing)));

		const ConsoleLayout layout = MeasureRows(Core::Scaled(kRowSpacing));

		// Before the rows are drawn, so the highlight under them is this frame's. Nothing in the
		// child can become active (the rows are TextUnformatted) and its scrollbar takes the
		// ActiveId inside BeginChild, so a drag on the bar is already seen as claimed here.
		UpdateSelection(layout);

		ImDrawList* const draw = ImGui::GetWindowDrawList();
		const float lineHeight = ImGui::GetTextLineHeight();

		for (std::size_t row = 0; row < m_Shown.size(); ++row)
		{
			const Core::LogEntry& entry = m_Shown[row];
			const std::size_t columns = Core::ColumnCount(entry.Message, Core::DefaultTabWidth);

			if (const std::optional<Core::ColumnSpan> span =
					m_Selection.SpanOn(static_cast<std::int64_t>(entry.Sequence), columns))
			{
				const float top = layout.OriginY + static_cast<float>(row) * layout.RowHeight;

				Core::DrawSelectionSpan(*draw,
										layout.OriginX + static_cast<float>(span->From) * layout.CellWidth,
										layout.OriginX + static_cast<float>(span->To) * layout.CellWidth,
										top, lineHeight);
			}

			ImGui::PushStyleColor(ImGuiCol_Text, ColorFor(entry.Level));
			ImGui::TextUnformatted(entry.Message.c_str());
			ImGui::PopStyleColor();
		}

		ImGui::PopStyleVar();

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
		source.LineLength = [this](std::int64_t line) -> std::size_t {
			for (const Core::LogEntry& entry : m_Shown)
			{
				if (static_cast<std::int64_t>(entry.Sequence) == line)
				{
					return Core::ColumnCount(entry.Message, Core::DefaultTabWidth);
				}
			}

			return 0;
		};
		source.Everything = [this]() -> std::pair<Core::TextPoint, Core::TextPoint> {
			if (m_Shown.empty())
			{
				return {};
			}

			return {Core::TextPoint{static_cast<std::int64_t>(m_Shown.front().Sequence), 0},
					Core::TextPoint{static_cast<std::int64_t>(m_Shown.back().Sequence),
									Core::ColumnCount(m_Shown.back().Message, Core::DefaultTabWidth)}};
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

		// The gesture has set the clipboard already; only the menu's Copy is confirmed, Ctrl+C
		// goes unremarked as it does in the editor.
		if (result.Copied && result.FromMenu)
		{
			ReportCopied(m_Events, "selection", result.CopiedText);
		}
	}

	void ConsolePanel::FollowTailOnlyWhenEntriesArrived()
	{
		if (m_AutoScroll && m_Entries.size() != m_LastCount)
		{
			ImGui::SetScrollHereY(1.0f);
		}

		m_LastCount = m_Entries.size();
	}
}
