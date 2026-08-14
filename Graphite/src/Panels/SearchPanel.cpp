#include "Panels/SearchPanel.h"

#include "Search/FileEnumerator.h"

#include "Core/Fs/FileSystem.h"

#include <imgui.h>

#include <map>
#include <string>

namespace Graphite {

	SearchPanel::SearchPanel(SearchService& service, const SearchSettings& settings,
							 Workspace& workspace, const Core::DocumentSet& documents)
		: Core::Panel("search", "Search"), m_Service(service), m_Settings(settings),
		  m_Workspace(workspace), m_Documents(documents)
	{
	}

	void SearchPanel::OnImGuiRender()
	{
		// Once per frame, before anything is drawn: Drain appends, so a second call this frame
		// would double what a frame already showed.
		m_Service.Drain(m_Hits, m_Progress);

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

	void SearchPanel::RenderQuery()
	{
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
			const std::string label =
				Core::ToUtf8(file) + "  (" + std::to_string(hits.size()) + ")";

			if (!ImGui::TreeNode(label.c_str()))
				continue;

			int hitIndex = 0;

			for (const SearchHit* hit : hits)
			{
				ImGui::PushID(hitIndex++);

				const std::string entry = std::to_string(hit->Line + 1) + ": " + hit->Text;

				if (ImGui::Selectable(entry.c_str()))
					m_Workspace.RequestOpenFile(hit->File, true);

				ImGui::PopID();
			}

			ImGui::TreePop();
		}

		ImGui::EndChild();
	}
}
