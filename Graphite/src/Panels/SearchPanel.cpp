#include "Panels/SearchPanel.h"

#include "Panels/SearchPreview.h"
#include "Search/FileEnumerator.h"
#include "Search/HitDisplay.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Text/Caret.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace Graphite {

	SearchPanel::SearchPanel(SearchService& service, const SearchSettings& settings,
							 Workspace& workspace, Core::DocumentSet& documents,
							 const ActiveScheme& scheme, const EditorSettings& editor)
		// Hidden until asked for, like the settings panel. Panel's third parameter defaults to
		// visible, which suits the explorer and the console — a window you want on screen from
		// the first frame. A search panel is not that: it opens with an empty query and nothing
		// to show, so it started every session taking up space nobody had asked for.
		// The id stays "search" while the title changes: the id is what imgui.ini keys the dock
		// layout on and what the panel's command is named after, so renaming it would move
		// everyone's panel back to where it started.
		: Core::Panel("search", "Find in Files", false), m_Service(service), m_Settings(settings),
		  m_Workspace(workspace), m_Documents(documents), m_Scheme(scheme), m_Editor(editor)
	{
	}

	void SearchPanel::OnUpdate(Core::Timestep)
	{
		// Once per frame, regardless of whether this panel is visible or drawn: a hidden panel,
		// or one behind another tab of the same dock node, never reaches OnImGuiRender, and
		// Drain is the only thing that ever clears Progress.Running. Draining there instead of
		// here left the status bar stuck reading a frozen "Searching..." count while another
		// tab was on screen.
		m_Drained.clear();
		m_Service.Drain(m_Drained, m_Progress);

		// Grouped here, as hits arrive, rather than in the render: this frame's cost is what this
		// frame drained, and a finished search costs nothing at all.
		for (SearchHit& hit : m_Drained)
		{
			m_Groups.Add(std::move(hit));
		}
	}

	void SearchPanel::OnImGuiRender()
	{
		RenderQuery();
		ImGui::Separator();
		RenderSummary();
		ImGui::Separator();

		// Nothing selected, no pane at all — not an empty one with a prompt in it. This panel
		// is used to read a list far more often than to look at one hit, and a pane reserving
		// half the height for a message is half a panel spent saying nothing.
		//
		// The results are then the whole panel, and the preview costs nothing until it is
		// asked for.
		if (!m_Selected)
		{
			if (ImGui::BeginChild("##searchResults", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
								  ImGuiWindowFlags_HorizontalScrollbar))
			{
				RenderResults();
			}

			ImGui::EndChild();
			return;
		}

		// Stacked, not side by side: the query field and its row of toggles run the full width
		// above both, so a preview beside the results made three columns of a panel that reads
		// top to bottom.
		const float available = ImGui::GetContentRegionAvail().y;

		// The thickness ImGui gives the resizing border between docked windows, read from the
		// same style field because this divider does the same job. At ItemSpacing.y, which is
		// what this first used, it was five pixels against the docking separators' two and read
		// as a heavier rule than anything else in the application.
		const float thickness = std::max(1.0f, ImGui::GetStyle().DockingSeparatorSize);

		// A quarter of the panel each at the tightest, and two rows each when there is room for
		// more than that — scaled rather than fixed, because this panel docks at the bottom
		// where it is wide and short, and a fixed minimum taller than half of it would leave
		// the splitter nothing to travel through.
		const float minimum = std::min(ImGui::GetFrameHeight() * 2.0f, available * 0.25f);

		if (m_ResultsHeight <= 0.0f)
		{
			m_ResultsHeight = (available - thickness) * 0.5f;
		}

		float previewHeight = available - thickness - m_ResultsHeight;

		// The dock node resizes under us, which moves `available` without touching the split
		// the user dragged. Clamped here rather than rescaled: a split is a position the user
		// chose, and the pane that gives way when there is less room is the one they are not
		// looking at.
		if (previewHeight < minimum)
		{
			previewHeight = minimum;
			m_ResultsHeight = available - thickness - previewHeight;
		}

		if (m_ResultsHeight < minimum)
		{
			m_ResultsHeight = minimum;
			previewHeight = available - thickness - m_ResultsHeight;
		}

		if (ImGui::BeginChild("##searchResults", ImVec2(0.0f, m_ResultsHeight),
							  ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
		{
			RenderResults();
		}

		ImGui::EndChild();

		// A short delay before the divider lights up and the cursor changes, so sweeping the
		// mouse across the panel does not flash it. The same 0.04s ImGui waits before
		// highlighting a window's resize border (WINDOWS_RESIZE_FROM_EDGES_FEEDBACK_TIMER in
		// imgui.cpp) — copied as a value because that one is file-static.
		constexpr float kSplitterHoverDelay = 0.04f;

		// EndChild leaves the cursor an item's spacing below the results. The divider replaces
		// that spacing rather than sitting under it, so the gap between the two panes is the
		// line and nothing else — which is how the docking separators beside it look.
		const ImVec2 below = ImGui::GetCursorScreenPos();
		const ImVec2 at(below.x, below.y - ImGui::GetStyle().ItemSpacing.y);

		const ImRect bar(at, ImVec2(at.x + ImGui::GetContentRegionAvail().x, at.y + thickness));

		// ImGui's own splitter rather than a line and a hand-rolled drag: it carries the
		// ResizeNS cursor, a grab zone wider than the line it draws, the minimum sizes above
		// and the Separator colours every other divider in the application uses. The grab zone
		// is the same padding ImGui gives a window's own border, so this divider is no harder
		// to catch than the ones around it.
		ImGui::SplitterBehavior(bar, ImGui::GetID("##searchSplit"), ImGuiAxis_Y, &m_ResultsHeight,
								&previewHeight, minimum, minimum,
								ImGui::GetCurrentContext()->WindowsBorderHoverPadding,
								kSplitterHoverDelay);

		// SplitterBehavior hit-tests and draws but never calls ItemSize, so the cursor is still
		// where it was. Set rather than advanced with a Dummy, which would add the ordinary
		// item spacing back on top of the bar's own height.
		ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + thickness));

		if (ImGui::BeginChild("##searchPreview", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
							  ImGuiWindowFlags_HorizontalScrollbar))
		{
			RenderPreview();
		}

		ImGui::EndChild();
	}

	SearchStart SearchPanel::BuildRequest() const
	{
		SearchStart request;

		request.Query.Needle = m_QueryBuffer.data();
		request.Query.Mask = m_MaskBuffer.data();
		request.Query.MatchCase = m_MatchCase;
		request.Query.WholeWord = m_WholeWord;

		request.Options = m_Settings.Get();
		request.Root = m_Workspace.GetRoot();

		// Copied here, on the UI thread, because DocumentSet's buffers change as the user types
		// and a worker reading them would be a data race. Open tabs number in the tens.
		for (std::size_t index = 0; index < m_Documents.GetCount(); ++index)
		{
			const Core::OpenItem& item = m_Documents.At(index);

			if (item.Document)
			{
				request.Unsaved.push_back(
					UnsavedFile{item.Path, item.Document->GetBuffer().Materialize()});
			}
		}

		// The setting outlives the solution it was set for: it persists to graphite.json, and a
		// later session on a plain folder has no projects to list. Left set, the worker would
		// take the projects branch over an empty file list and report nothing found, every time,
		// with the checkbox greyed out because no solution is open — a dead search with no way
		// out of it short of editing the settings file. Without a solution, walk the folder.
		request.Options.ProjectsOnly =
			request.Options.ProjectsOnly && m_Workspace.LoadedSolution().has_value();

		if (request.Options.ProjectsOnly)
		{
			EnumerateProjectFiles(*m_Workspace.LoadedSolution(),
								  [&request](const std::filesystem::path& file)
								  { request.ProjectFiles.push_back(file); });
		}

		return request;
	}

	std::optional<std::string> SearchPanel::GetStatusMessage() const
	{
		if (!m_Progress.Running)
			return std::nullopt;

		return "Searching... " + std::to_string(m_Progress.FilesScanned) + " files";
	}

	void SearchPanel::Focus()
	{
		SetVisible(true);

		// Visible is not the same as on screen: a panel sharing a dock node with another tab
		// stays behind it, never reaches OnImGuiRender, and so never consumes the queued
		// keyboard focus below — the user would have to click the tab first.
		// The same name PanelManager gives the window, because that is what ImGui knows it by.
		const std::string window = GetTitle() + "###" + GetId();

		ImGui::SetWindowFocus(window.c_str());

		m_FocusQueryNextFrame = true;
	}

	void SearchPanel::RenderQuery()
	{
		// Consumed on the first frame this panel actually draws after Focus() was called:
		// SetKeyboardFocusHere only takes effect on the very next widget submitted.
		if (m_FocusQueryNextFrame)
		{
			ImGui::SetKeyboardFocusHere();
			m_FocusQueryNextFrame = false;
		}

		ImGui::SetNextItemWidth(-1.0f);

		const bool entered = ImGui::InputTextWithHint(
			"##searchQuery", "Find in files", m_QueryBuffer.data(), m_QueryBuffer.size(),
			ImGuiInputTextFlags_EnterReturnsTrue);

		// The mask shares a row with the toggles rather than taking one of its own: four rows of
		// controls before the first result is most of a short panel.
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
		ImGui::InputTextWithHint("##searchMask", "*.cpp;*.h", m_MaskBuffer.data(),
								 m_MaskBuffer.size());

		ImGui::SameLine();
		ImGui::Checkbox("Match case", &m_MatchCase);
		ImGui::SameLine();
		ImGui::Checkbox("Whole word", &m_WholeWord);
		ImGui::SameLine();

		// Enter in the field or the button, never every keystroke: a search per character on a
		// large tree is a search per character on a large tree.
		if (ImGui::Button("Search") || entered)
			RunSearch();
	}

	void SearchPanel::RunSearch()
	{
		m_Groups.Clear();
		m_Groups.SetRoot(m_Workspace.GetRoot());

		// The old selection names a group that no longer exists, and a preview of it would
		// outlive the results it came from.
		m_Selected.reset();
		m_Preview.Clear();

		m_Drained.clear();
		m_Progress = SearchProgress{};

		m_Service.Start(BuildRequest());
	}

	void SearchPanel::RenderSummary()
	{
		std::string summary = std::to_string(m_Groups.GetCount()) + " hits, " +
							  std::to_string(m_Progress.FilesScanned) + " files scanned";

		// Reported only when non-zero: the spec calls for saying what could not be read, not for
		// padding every search with two zeroes.
		if (m_Progress.FilesSkipped != 0)
			summary += ", " + std::to_string(m_Progress.FilesSkipped) + " skipped";

		if (m_Progress.Unreadable != 0)
			summary += ", " + std::to_string(m_Progress.Unreadable) + " unreadable";

		if (m_Progress.Running)
			summary += "  (searching...)";

		ImGui::TextUnformatted(summary.c_str());
	}

	void SearchPanel::RenderResults()
	{
		// Already grouped, in OnUpdate, as the hits arrived. This loop walks groups; it never
		// touches a hit belonging to a file it is not drawing.
		// The accent the theme already uses for a tick, so the match reads as marked rather than
		// as a second colour nobody chose. When sub-project D makes the chrome data-driven this
		// follows it without a setting of its own.
		const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);

		for (const HitGroup& group : m_Groups.Get())
		{
			// The node's id is the full path and never changes while a search runs. Its label is
			// drawn separately, below, because the count in it changes on every new hit — passing
			// the two combined as the id, an earlier mistake here, made ImGui see a new node each
			// time and close whatever the user had just opened.
			const bool open = ImGui::TreeNodeEx(group.Id.c_str(),
												ImGuiTreeNodeFlags_SpanAvailWidth |
													ImGuiTreeNodeFlags_AllowOverlap,
												"");

			ImGui::SameLine(0.0f, 0.0f);

			// Dimmed, because the directory is the same for run after run of neighbouring rows
			// and the eye is looking for the file name.
			if (!group.Directory.empty())
			{
				ImGui::TextDisabled("%s", group.Directory.c_str());
				ImGui::SameLine(0.0f, 0.0f);
			}

			ImGui::TextUnformatted(group.Name.c_str());
			ImGui::SameLine(0.0f, 0.0f);
			ImGui::TextDisabled("  (%d)", static_cast<int>(group.Hits.size()));

			if (!open)
				continue;

			// One expanded file can hold thousands of hits, and every row costs a string built to
			// draw it. The clipper submits only the rows the scroll actually shows, so an expanded
			// file costs what is on screen rather than what is in it.
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(group.Hits.size()));

			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const SearchHit& hit = group.Hits[static_cast<std::size_t>(row)];

					ImGui::PushID(row);

					const ImVec2 rowStart = ImGui::GetCursorPos();

					const bool selected = m_Selected && m_Selected->Group == group.Id &&
										  m_Selected->Hit == static_cast<std::size_t>(row);

					// An empty label, drawn first, so the whole row is the click target and the
					// text can be laid over it in pieces. AllowOverlap keeps the click reaching
					// this rather than being swallowed by the text items on top;
					// AllowDoubleClick is what lets one row answer both gestures.
					if (ImGui::Selectable("##hit", selected,
										  ImGuiSelectableFlags_AllowOverlap |
											  ImGuiSelectableFlags_AllowDoubleClick))
					{
						m_Selected = Selection{group.Id, static_cast<std::size_t>(row)};

						// Single click previews, double click opens. That is the whole point:
						// arrow through fifty hits without leaving fifty tabs behind.
						if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
							OpenAndSelect(hit);
					}

					// The preview follows the selection when it moves by keyboard too, which
					// arrives as nav focus rather than as a click.
					if (!selected && ImGui::IsItemFocused())
						m_Selected = Selection{group.Id, static_cast<std::size_t>(row)};

					ImGui::SetCursorPos(rowStart);

					// Three pieces rather than one string with a coloured rectangle behind it:
					// positioning that rectangle means measuring the text myself, and
					// CalcTextSize rounds up — the same drift that made the editor count whole
					// cells instead. Letting ImGui advance between pieces is exact by
					// construction.
					const HitLine line = SplitForDisplay(hit);

					ImGui::TextDisabled("%d: ", static_cast<int>(hit.Line + 1));
					ImGui::SameLine(0.0f, 0.0f);

					ImGui::TextUnformatted(line.Before.data(),
										   line.Before.data() + line.Before.size());
					ImGui::SameLine(0.0f, 0.0f);

					ImGui::TextColored(accent, "%.*s", static_cast<int>(line.Match.size()),
									   line.Match.data());
					ImGui::SameLine(0.0f, 0.0f);

					ImGui::TextUnformatted(line.After.data(),
										   line.After.data() + line.After.size());

					ImGui::PopID();
				}
			}

			ImGui::TreePop();
		}
	}

	// Only reached with a selection: OnImGuiRender draws no pane at all without one.
	void SearchPanel::RenderPreview()
	{
		// Looked up by id rather than remembered as a pointer: groups are appended to as a
		// search runs, and the vector holding them reallocates.
		const HitGroup* group = nullptr;

		for (const HitGroup& candidate : m_Groups.Get())
		{
			if (candidate.Id == m_Selected->Group)
			{
				group = &candidate;
				break;
			}
		}

		// The results were cleared or regrouped under the selection. Dropping it here is what
		// takes the pane away again on the next frame.
		if (group == nullptr || m_Selected->Hit >= group->Hits.size())
		{
			m_Selected.reset();
			m_Preview.Clear();
			return;
		}

		const SearchHit& hit = group->Hits[m_Selected->Hit];

		// Every frame, not only when the selection changes: the file may have been opened,
		// closed or edited since the last one, and Show is cheap when nothing moved.
		m_Preview.Show(hit.File, m_Documents);

		// By name: Current and TabWidth are both std::size_t, and a positional list would take
		// them transposed without a word.
		DrawSearchPreview(m_Preview,
						  SearchPreviewInput{.Hits = group->Hits,
											 .Current = m_Selected->Hit,
											 .Scheme = m_Scheme.Get(),
											 .TabWidth = m_Editor.GetIndentStyle().Width,
											 .FontSize = m_Editor.GetFontSize()});
	}

	void SearchPanel::OpenAndSelect(const SearchHit& hit)
	{
		m_Workspace.RequestOpenFile(hit.File, true);

		// RequestOpenFile publishes synchronously, and DocumentWindows::RequestOpen opens or
		// activates the tab inline, so by the time control returns here the document is either
		// in the set already or never will be this frame (a large file still waiting on the
		// size confirmation, say) — either way Find is the right thing to check.
		const auto index = m_Documents.Find(hit.File);

		if (!index)
			return;

		Core::OpenItem& item = m_Documents.At(*index);

		if (!item.Document)
			return;

		Core::TextBuffer& buffer = item.Document->GetBuffer();
		const std::size_t lineStart = buffer.LineStart(hit.Line);

		// Line and byte offsets both come from the search that ran some time ago; the file may
		// have changed since. PlaceAt runs every offset through SnapToBoundary, which clamps to
		// the buffer's current length, so a stale range lands at the nearest valid position
		// instead of reading past the end of a document that has since shrunk.
		item.Caret = Core::PlaceAt(buffer, item.Caret, lineStart + hit.From, false, item.TabWidth);
		item.Caret = Core::PlaceAt(buffer, item.Caret, lineStart + hit.To, true, item.TabWidth);

		// The vertical aim was measured, if at all, against wherever the caret used to sit; the
		// next Up or Down should resolve it fresh from here rather than carry that forward.
		item.RowAimAt = Core::NoRowAim;

		// This runs during the panels' turn, before DocumentWindows renders the editor later in
		// the same frame — so by the editor's own reckoning the caret never moved this frame,
		// and it would not otherwise scroll to it. See PendingScroll's comment in DocumentSet.h.
		item.PendingScroll = true;
	}
}
