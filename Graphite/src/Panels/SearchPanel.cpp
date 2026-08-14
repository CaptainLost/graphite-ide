#include "Panels/SearchPanel.h"

#include "Search/FileEnumerator.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Text/Caret.h"

#include <imgui.h>

#include <map>
#include <string>

namespace Graphite {

	SearchPanel::SearchPanel(SearchService& service, const SearchSettings& settings,
							 Workspace& workspace, Core::DocumentSet& documents)
		// Hidden until asked for, like the settings panel. Panel's third parameter defaults to
		// visible, which suits the explorer and the console — a window you want on screen from
		// the first frame. A search panel is not that: it opens with an empty query and nothing
		// to show, so it started every session taking up space nobody had asked for.
		: Core::Panel("search", "Search", false), m_Service(service), m_Settings(settings),
		  m_Workspace(workspace), m_Documents(documents)
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
		RenderResults();
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

		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint("##searchMask", "*.cpp;*.h (empty matches everything)",
								 m_MaskBuffer.data(), m_MaskBuffer.size());

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
		if (!ImGui::BeginChild("##searchResults", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
							   ImGuiWindowFlags_HorizontalScrollbar))
		{
			ImGui::EndChild();
			return;
		}

		// Already grouped, in OnUpdate, as the hits arrived. This loop walks groups; it never
		// touches a hit belonging to a file it is not drawing.
		for (const HitGroup& group : m_Groups.Get())
		{
			// The id and the label are kept apart on purpose: the label carries the running hit
			// count, which changes on every new hit. Passing that combined string as the node's
			// id — an earlier mistake here — made ImGui see a new node each time and close
			// whatever the user had just opened. The id below never changes while a search runs.
			const std::string label =
				group.Name + "  (" + std::to_string(group.Hits.size()) + ")";

			if (!ImGui::TreeNode(group.Name.c_str(), "%s", label.c_str()))
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

					const std::string entry = std::to_string(hit.Line + 1) + ": " + hit.Text;

					if (ImGui::Selectable(entry.c_str()))
						OpenAndSelect(hit);

					ImGui::PopID();
				}
			}

			ImGui::TreePop();
		}

		ImGui::EndChild();
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
