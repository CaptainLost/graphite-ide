#include "Git/WriteConsequences.h"

#include "Git/Changes/ChangesShared.h"
#include "Git/CheckoutQuestion.h"
#include "Git/EditorBuffers.h"

#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		// WARNING: Status paths are relative to the working directory, not to the open folder: Repository::Open searches
		// upward for a .git, so the two can differ.
		std::vector<std::filesystem::path> InWorkingDirectory(const std::vector<std::filesystem::path>& relativePaths,
															  const std::filesystem::path& workingDirectory)
		{
			std::vector<std::filesystem::path> absolutePaths;
			absolutePaths.reserve(relativePaths.size());

			for (const std::filesystem::path& relativePath : relativePaths)
			{
				absolutePaths.push_back(workingDirectory / relativePath);
			}

			return absolutePaths;
		}
	}

	WriteConsequences::WriteConsequences(GitModel& model, EditorBuffers& editor, ChangesShared& draft,
										 FailureReport reportFailure, FilesWritten filesWritten)
		: m_Model(model), m_Editor(editor), m_Draft(draft), m_ReportFailure(std::move(reportFailure)),
		  m_FilesWritten(std::move(filesWritten))
	{
	}

	void WriteConsequences::Apply()
	{
		if (m_Model.TakeCommitted().has_value())
		{
			m_Draft.Message.clear();
			m_Draft.Amend = false;
		}

		const std::filesystem::path& workingDirectory = m_Model.GetWorkingDirectory();
		const std::vector<std::filesystem::path> discarded = m_Model.TakeDiscarded();
		const std::vector<std::filesystem::path> checkedOut = m_Model.TakeCheckedOut();

		if (!workingDirectory.empty())
		{
			// WARNING: A discard was confirmed with the unsaved edits it throws away named in its question, so every
			// file it rewrote reloads without asking.
			if (!discarded.empty())
			{
				m_Editor.ExpectExternalWrite(InWorkingDirectory(discarded, workingDirectory));
			}

			// WARNING: A checkout leaves a document with unsaved edits to ask, since its question offered to keep them.
			if (!checkedOut.empty())
			{
				m_Editor.ExpectExternalWrite(SilentReloads(InWorkingDirectory(checkedOut, workingDirectory),
														   m_Editor.ModifiedUnder(workingDirectory)));
			}
		}

		if (const std::uint64_t writes = m_Model.GetAppliedWriteCount(); writes != m_SeenWrites)
		{
			m_SeenWrites = writes;
			m_FilesWritten();
		}

		while (std::optional<GitModel::WriteFailure> failure = m_Model.TakeWriteError())
		{
			m_ReportFailure(*failure);
		}
	}
}
