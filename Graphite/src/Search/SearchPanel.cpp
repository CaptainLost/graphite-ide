#include "Search/SearchPanel.h"

#include "Editor/Navigation/SelectInDocument.h"

#include "Search/SearchPreview.h"
#include "Search/FileEnumerator.h"
#include "Search/HitList.h"
#include "UI/SplitResults.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Text/Caret.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <functional>
#include <string>

namespace Graphite {

	SearchPanel::SearchPanel(SearchService& service, const SearchSettings& settings,
							 Workspace& workspace, Core::DocumentSet& documents,
							 const ActiveScheme& scheme, const EditorSettings& editor,
							 PreviewSemantics& semantics)
		// Hidden until asked for, like the settings panel. Panel's third parameter defaults to
		// visible, which suits the explorer and the console — a window you want on screen from
		// the first frame. A search panel is not that: it opens with an empty query and nothing
		// to show, so it started every session taking up space nobody had asked for.
		// The id stays "search" while the title changes: the id is what imgui.ini keys the dock
		// layout on and what the panel's command is named after, so renaming it would move
		// everyone's panel back to where it started.
		: Core::Panel("search", "Find in Files", "navigate", ICON_MDI_TEXT_SEARCH, false), m_Service(service), m_Settings(settings),
		  m_Workspace(workspace), m_Documents(documents), m_Scheme(scheme), m_Editor(editor),
		  m_Preview(&semantics)
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

		const std::function<void()> drawResults = [this] { RenderResults(); };
		const std::function<void()> drawPreview = [this] { RenderPreview(); };

		DrawSplitResults(SplitResultsInput{.HasSelection = m_Selected.has_value(),
										   .ResultsHeight = m_ResultsHeight,
										   .DrawResults = drawResults,
										   .DrawPreview = drawPreview,
										   .ResultsChildId = "##searchResults",
										   .PreviewChildId = "##searchPreview",
										   .SplitterId = "##searchSplit"});
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
			request.Options.ProjectsOnly && m_Workspace.GetLoadedSolution().has_value();

		if (request.Options.ProjectsOnly)
		{
			EnumerateProjectFiles(*m_Workspace.GetLoadedSolution(),
								  [&request](const std::filesystem::path& file) { request.ProjectFiles.push_back(file); });
		}

		return request;
	}

	std::optional<std::string> SearchPanel::GetStatusMessage() const
	{
		if (!m_Progress.Running)
		{
			return std::nullopt;
		}

		return "Searching... " + std::to_string(m_Progress.FilesScanned) + " files";
	}

	void SearchPanel::Focus()
	{
		SetVisible(true);

		// Visible is not the same as on screen: a panel sharing a dock node with another tab
		// stays behind it, never reaches OnImGuiRender, and so never consumes the queued
		// keyboard focus below — the user would have to click the tab first.
		// The same name PanelManager gives the window, because that is what ImGui knows it by.
		const std::string window = GetWindowName();

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
		{
			RunSearch();
		}
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
		{
			summary += ", " + std::to_string(m_Progress.FilesSkipped) + " skipped";
		}

		if (m_Progress.Unreadable != 0)
		{
			summary += ", " + std::to_string(m_Progress.Unreadable) + " unreadable";
		}

		if (m_Progress.Running)
		{
			summary += "  (searching...)";
		}

		ImGui::TextUnformatted(summary.c_str());
	}

	void SearchPanel::RenderResults()
	{
		// Already grouped, in OnUpdate, as the hits arrived. DrawHitList walks groups; it never
		// touches a hit belonging to a file it is not drawing.
		const std::function<void(const SearchHit&)> onOpen = [this](const SearchHit& hit) { OpenAndSelect(hit); };

		DrawHitList(HitListInput{
			.Groups = m_Groups.Get(), .Selected = m_Selected, .OnOpen = onOpen});
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

		// Before Show, not after: Show is what declares the file to the server, and the first
		// declaration of a newly previewed file has to carry the user's tab width rather than
		// Core::DefaultTabWidth — DrawSearchPreview's own SetTabWidth runs too late for that
		// first frame, so it would go out at the wrong width and be discarded next frame anyway.
		m_Preview.SetTabWidth(m_Editor.GetIndentStyle().Width);

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
		// RequestOpenFile publishes synchronously, and DocumentWindows::RequestOpen opens or
		// activates the tab inline, so by the time it returns the document is either in the set
		// already or never will be this frame (a large file still waiting on the size
		// confirmation, say).
		SelectInDocument(m_Documents, hit.File, hit.Line, hit.From, hit.To,
						 [this](const std::filesystem::path& file) { m_Workspace.RequestOpenFile(file, true); });
	}
}
