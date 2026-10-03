#include "Console/ConsolePanel.h"

#include "Core/ImGui/UiScale.h"

#include "Core/Base/Narrow.h"
#include "Core/Base/Base.h"

#include "Core/ImGui/TextMetrics.h"
#include "Core/Text/Columns.h"

#include <IconsMaterialDesignIcons.h>

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

	ConsolePanel::ConsolePanel(Core::Ref<Core::RingBufferSink> sink)
		: Core::Panel("console", "Console", "tools", ICON_MDI_MESSAGE_TEXT_OUTLINE), m_Sink(std::move(sink))
	{
	}

	void ConsolePanel::OnImGuiRender()
	{
		// Not every frame: taking the snapshot copies every message the buffer holds, and
		// filtering copies the shown ones again, on frames where nothing has changed.
		const std::uint64_t revision = m_Sink->GetRevision();

		if (revision != m_SeenRevision || m_SeenFilter != m_Filter.InputBuf)
		{
			m_Entries = m_Sink->TakeSnapshot();
			CollectShownEntries();

			m_SeenRevision = revision;
			m_SeenFilter = m_Filter.InputBuf;
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
			if (m_Filter.PassFilter(entry.Message.c_str()))
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

		if (ImGui::Button("Copy all"))
		{
			ImGui::SetClipboardText(JoinEntries(m_Entries).c_str());
		}

		ImGui::SameLine();
		ImGui::Checkbox("Auto-scroll", &m_AutoScroll);

		ImGui::SameLine();
		ImGui::SetNextItemWidth(Core::Scaled(240.0f));
		m_Filter.Draw("##consoleFilter");

		ImGui::SameLine();
		ImGui::TextDisabled("%d entries", Core::Narrow<int>(m_Entries.size()));
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

		HandleSweep(layout);

		ImDrawList* const draw = ImGui::GetWindowDrawList();
		const ImU32 selectionColor = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
		const float lineHeight = ImGui::GetTextLineHeight();

		for (std::size_t row = 0; row < m_Shown.size(); ++row)
		{
			const Core::LogEntry& entry = m_Shown[row];
			const std::size_t columns = Core::ColumnCount(entry.Message, Core::DefaultTabWidth);

			if (const std::optional<ConsoleColumnSpan> span =
					m_Selection.ColumnsOn(entry.Sequence, columns))
			{
				const float top = layout.OriginY + static_cast<float>(row) * layout.RowHeight;

				draw->AddRectFilled(
					ImVec2(layout.OriginX + static_cast<float>(span->From) * layout.CellWidth,
						   top),
					ImVec2(layout.OriginX + static_cast<float>(span->To) * layout.CellWidth,
						   top + lineHeight),
					selectionColor);
			}

			ImGui::PushStyleColor(ImGuiCol_Text, ColorFor(entry.Level));
			ImGui::TextUnformatted(entry.Message.c_str());
			ImGui::PopStyleColor();
		}

		ImGui::PopStyleVar();

		RenderContextMenu();
		HandleShortcuts();
		FollowTailOnlyWhenEntriesArrived();

		ImGui::EndChild();
	}

	void ConsolePanel::HandleSweep(const ConsoleLayout& layout)
	{
		const ImVec2 mouse = ImGui::GetMousePos();

		if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			// The output is a child window, and a child nothing focused never sees a key:
			// without this the copy shortcut would go wherever the focus happened to be.
			ImGui::SetWindowFocus();

			if (const std::optional<ConsoleAnchor> anchor =
					AnchorAtPoint(m_Shown, layout, mouse.x, mouse.y))
			{
				// Shift keeps the pressed end where it was, which is how a reader stretches a
				// selection they already made rather than starting another one.
				if (ImGui::GetIO().KeyShift && !m_Selection.IsEmpty())
				{
					m_Selection.ExtendTo(*anchor);
				}
				else
				{
					m_Selection.Begin(*anchor);
				}

				m_Sweeping = true;
			}
		}

		if (!m_Sweeping)
		{
			return;
		}

		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			m_Sweeping = false;
			return;
		}

		if (const std::optional<ConsoleAnchor> anchor =
				AnchorAtPoint(m_Shown, layout, mouse.x, mouse.y))
		{
			m_Selection.ExtendTo(*anchor);
		}

		// Dragging out of the top or the bottom scrolls the output after the pointer, so a
		// selection can reach further than the panel is tall.
		const float top = ImGui::GetWindowPos().y;
		const float bottom = top + ImGui::GetWindowSize().y;

		if (mouse.y < top)
		{
			ImGui::SetScrollY(ImGui::GetScrollY() - layout.RowHeight);
		}
		else if (mouse.y > bottom)
		{
			ImGui::SetScrollY(ImGui::GetScrollY() + layout.RowHeight);
		}
	}

	void ConsolePanel::HandleShortcuts()
	{
		if (!ImGui::IsWindowFocused())
		{
			return;
		}

		const ImGuiIO& io = ImGui::GetIO();

		if (!io.KeyCtrl)
		{
			return;
		}

		if (ImGui::IsKeyPressed(ImGuiKey_C))
		{
			CopySelection();
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_A))
		{
			m_Selection.SelectAll(m_Shown);
		}
	}

	void ConsolePanel::RenderContextMenu()
	{
		if (!ImGui::BeginPopupContextWindow("##consoleMenu"))
		{
			return;
		}

		if (ImGui::MenuItem("Copy", "Ctrl+C", false, !m_Selection.IsEmpty()))
		{
			CopySelection();
		}

		if (ImGui::MenuItem("Select all", "Ctrl+A", false, !m_Shown.empty()))
		{
			m_Selection.SelectAll(m_Shown);
		}

		ImGui::EndPopup();
	}

	void ConsolePanel::CopySelection() const
	{
		const std::string selected = m_Selection.ExtractText(m_Shown);

		if (!selected.empty())
		{
			ImGui::SetClipboardText(selected.c_str());
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
