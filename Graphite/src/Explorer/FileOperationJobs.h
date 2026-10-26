#pragma once

#include "Core/Base/Pointers.h"
#include "Core/Base/Result.h"
#include "Core/Base/Timestep.h"
#include "Core/Jobs/JobSubmitter.h"
#include "Core/Jobs/Mailbox.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	enum class FileOperationKind : std::uint8_t
	{
		Duplicate,
		MoveToRecycleBin
	};

	struct RunningFileOperation
	{
		std::uint64_t Id = 0;
		FileOperationKind Kind = FileOperationKind::Duplicate;
		std::filesystem::path Target;
		std::filesystem::path Folder;
		Core::SteadyClock::time_point StartedAt;
		Core::Ref<std::atomic<std::size_t>> CopiedFiles;
	};

	// The copy's path for a duplicate; the target itself for a move to the Recycle Bin.
	struct FinishedFileOperation
	{
		std::uint64_t Id = 0;
		FileOperationKind Kind = FileOperationKind::Duplicate;
		std::filesystem::path Target;
		std::filesystem::path Folder;
		Core::Result<std::filesystem::path> Outcome;
	};

	std::string RunningOperationText(FileOperationKind kind, const std::filesystem::path& target, std::size_t copiedFiles);

	// WARNING: An operation says nothing for its first fifth of a second, as every loader waits, so a copy over in a frame
	// or two does not flash its words in the status bar.
	std::optional<std::string> FileOperationsStatusText(const std::vector<RunningFileOperation>& running,
														Core::SteadyClock::time_point now);

	// Duplicate and Move to the Recycle Bin run here, off the UI thread. Neither can be cancelled once started.
	class FileOperationJobs final
	{
	public:
		using Clock = Core::SteadyClock;

		explicit FileOperationJobs(Core::JobSubmitter submit);

		FileOperationJobs(const FileOperationJobs&) = delete;
		FileOperationJobs& operator=(const FileOperationJobs&) = delete;

		// The folder is the one open when the operation starts, so its result is not applied to another one.
		void Start(FileOperationKind kind, const std::filesystem::path& target, const std::filesystem::path& folder,
				   Clock::time_point now);

		std::vector<FinishedFileOperation> TakeFinished();

		// A path is busy while an operation runs on it or on a folder holding it.
		bool IsBusy(const std::filesystem::path& path) const;

		bool ShowsBusy(const std::filesystem::path& path, Clock::time_point now) const;

		std::optional<std::string> StatusText(Clock::time_point now) const;

		bool IsRunning() const { return !m_Running.empty(); }

	private:
		Core::JobSubmitter m_Submit;
		std::vector<RunningFileOperation> m_Running;
		Core::Ref<Core::Mailbox<FinishedFileOperation>> m_Finished = Core::CreateRef<Core::Mailbox<FinishedFileOperation>>();
		std::uint64_t m_NextId = 1;
	};
}
