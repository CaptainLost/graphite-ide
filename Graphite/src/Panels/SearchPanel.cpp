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
		: Core::Panel("search", "Search"), m_Service(service), m_Settings(settings),
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
		m_Service.Drain(m_Hits, m_Progress);
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

		if (request.Options.ProjectsOnly && m_Workspace.LoadedSolution())
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
		m_Hits.clear();
		m_Progress = SearchProgress{};

		m_Service.Start(BuildRequest());
	}

	void SearchPanel::RenderSummary()
	{
		std::string summary = std::to_string(m_Hits.size()) + " hits, " +
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

		// Grouped afresh each frame rather than incrementally: hits arrive from a job queue in
		// no promised order, so a file's hits are not necessarily contiguous in m_Hits.
		std::map<std::filesystem::path, std::vector<const SearchHit*>> byFile;

		for (const SearchHit& hit : m_Hits)
			byFile[hit.File].push_back(&hit);

		for (const auto& [file, hits] : byFile)
		{
			// The id and the label are kept apart on purpose: the label carries the running hit
			// count, which changes on every new hit. Passing that combined string as the node's
			// id — its earlier mistake — made ImGui see a new node each time and close whatever
			// the user had just opened. The id below never changes while a search runs.
			const std::string id = Core::ToUtf8(file);
			const std::string label = id + "  (" + std::to_string(hits.size()) + ")";

			if (!ImGui::TreeNode(id.c_str(), "%s", label.c_str()))
				continue;

			int hitIndex = 0;

			for (const SearchHit* hit : hits)
			{
				ImGui::PushID(hitIndex++);

				const std::string entry = std::to_string(hit->Line + 1) + ": " + hit->Text;

				if (ImGui::Selectable(entry.c_str()))
					OpenAndSelect(*hit);

				ImGui::PopID();
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
