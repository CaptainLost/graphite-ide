#include "Git/GitView.h"

#include "Core/Base/Base.h"
#include "Core/Base/Log.h"

#include "Git/History/CommitLabels.h"
#include "Git/History/CommitReveal.h"
#include "Git/History/HistoryFilter.h"
#include "Git/History/HistoryReach.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace Graphite {

	void GitView::ShowFileHistory(const std::filesystem::path& relativePath)
	{
		const std::string pathText = HistoryPathText(relativePath);
		const bool fitsPathBox = pathText.size() < m_FilterPathBuffer.size();

		if (!fitsPathBox)
		{
			APP_WARN("File History: '{}' is too long for the history's path box", pathText);
		}

		ShowHeadHistory(fitsPathBox ? pathText : std::string{});
	}

	void GitView::ShowCommit(const CommitReveal& reveal)
	{
		// The commit may be on a merged branch, which first parents leave out.
		m_FirstParentsOnly = false;
		ShowHeadHistory(std::string{});
		m_PendingReveal = reveal;
	}

	// WARNING: The log is asked here rather than left to the settling filter: a path already in the box
	// changes no filter, and the history would stay on the branch or stash it showed before.
	void GitView::ShowHeadHistory(const std::string& pathText)
	{
		m_PendingReveal.reset();
		m_RevealedFile.reset();

		m_SelectedRef.reset();
		DeselectStash();
		m_PendingRestore.reset();

		m_FilterBuffer = {};
		m_FilterPathBuffer = {};

		const std::size_t copiedLength = std::min(pathText.size(), m_FilterPathBuffer.size() - 1);
		std::copy_n(pathText.begin(), copiedLength, m_FilterPathBuffer.begin());

		m_Commits.ClearSelection();
		m_Commits.ClearRowSelection();
		m_OpeningRef = false;

		m_Filter.Set(HistoryFilter{.Text = {}, .Path = std::string(m_FilterPathBuffer.data())});
		RequestLog(HistoryReach{});
	}

	void GitView::AdvanceReveal(const Core::LogPage& page)
	{
		if (!m_PendingReveal.has_value())
		{
			return;
		}

		const std::optional<Core::LogRequest>& shown = m_Model.GetShownLogRequest();

		if (!shown.has_value() || !ShowsWholeHeadHistory(*shown))
		{
			m_PendingReveal.reset();
			return;
		}

		const HistoryReach asked{.MaxCount = shown->MaxCount, .MaxWalk = shown->MaxWalk};

		switch (NextRevealStep(*m_PendingReveal, page, asked))
		{
		case RevealStep::Select:
		{
			const FileSelection file{.Commit = m_PendingReveal->Commit, .Path = m_PendingReveal->Path};
			m_PendingReveal.reset();

			m_Commits.SelectCommit(file.Commit);
			m_Commits.SetExpanded(file.Commit, true);
			m_Commits.ScrollToCommit(file.Commit);

			// WARNING: No path selects no file: IndexOfFile would take an empty path for the empty new
			// path of the first file the commit deletes.
			if (!file.Path.empty())
			{
				m_Commits.SelectFile(file);
				m_RevealedFile = file;
			}

			break;
		}
		case RevealStep::LoadMore:
			RequestLog(LoadMore(m_Reach));
			break;
		case RevealStep::FilterByHash:
		{
			const std::string hash = m_PendingReveal->Commit.ToHex();
			m_PendingReveal.reset();

			m_FilterBuffer = {};
			std::ranges::copy(hash, m_FilterBuffer.begin());
			break;
		}
		}
	}

	void GitView::ConfirmRevealedFile()
	{
		if (!m_RevealedFile.has_value())
		{
			return;
		}

		const std::optional<FileSelection> selected = m_Commits.GetSelectedFile();
		const bool stillSelected = selected.has_value() && selected->Commit == m_RevealedFile->Commit &&
								   selected->Path == m_RevealedFile->Path;

		if (!stillSelected)
		{
			m_RevealedFile.reset();
			return;
		}

		const Core::Ref<const Core::Diff> diff = m_Model.GetDiffFor(m_RevealedFile->Commit);

		if (!diff)
		{
			if (m_Model.IsDiffFailed(m_RevealedFile->Commit))
			{
				m_RevealedFile.reset();
			}

			return;
		}

		const std::optional<std::size_t> fileIndex = IndexOfFile(*diff, m_RevealedFile->Path);

		// The file row is named by its shown path, which for a rename is the new one, so the selection
		// takes that name for the row to read as selected.
		if (fileIndex.has_value())
		{
			m_Commits.SelectFile(FileSelection{.Commit = m_RevealedFile->Commit, .Path = ShownPath(diff->Files[*fileIndex])});
		}
		else
		{
			m_Commits.ClearSelection();
		}

		m_RevealedFile.reset();
	}
}
