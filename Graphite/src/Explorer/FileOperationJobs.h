#pragma once

#include "Core/Base/Pointers.h"
#include "Core/Base/Result.h"
#include "Core/Base/Timestep.h"
#include "Core/FileSystem/FileOperations.h"
#include "Core/Jobs/JobSubmitter.h"
#include "Core/Jobs/Mailbox.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
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
		std::filesystem::path CopyDestination;
		std::filesystem::path FolderOpenAtStart;
		Core::SteadyClock::time_point StartedAt;
		Core::Ref<std::atomic<std::size_t>> CopiedFiles;
	};

	struct FinishedFileOperation
	{
		std::uint64_t Id = 0;
		FileOperationKind Kind = FileOperationKind::Duplicate;
		std::filesystem::path Target;
		std::filesystem::path FolderOpenAtStart;
		Core::Result<std::filesystem::path> CopyOrMovedPath;
	};

	using FileOperationRunner = std::function<Core::Result<std::filesystem::path>(
		const RunningFileOperation& operation, const Core::CopiedFileCount& noteCopied)>;

	Core::Result<std::filesystem::path> RunFileOperation(const RunningFileOperation& operation,
														 const Core::CopiedFileCount& noteCopied);

	std::string RunningOperationText(FileOperationKind kind, const std::filesystem::path& target, std::size_t copiedFiles);

	std::optional<std::string> FileOperationsStatusText(const std::vector<RunningFileOperation>& running);

	class FileOperationJobs final
	{
	public:
		using Clock = Core::SteadyClock;

		explicit FileOperationJobs(Core::JobSubmitter submit, FileOperationRunner run = RunFileOperation);

		FileOperationJobs(const FileOperationJobs&) = delete;
		FileOperationJobs& operator=(const FileOperationJobs&) = delete;

		void Start(FileOperationKind kind, const std::filesystem::path& target, const std::filesystem::path& folderOpen,
				   Clock::time_point now);

		std::vector<FinishedFileOperation> TakeFinished();

		bool IsBusy(const std::filesystem::path& path) const;

		bool IsBusyOrHoldsABusyPath(const std::filesystem::path& path) const;

		bool ShowsBusy(const std::filesystem::path& path, Clock::time_point now) const;

		bool IsReservedForACopy(const std::filesystem::path& path) const;

		std::filesystem::path PickUniquePath(const std::filesystem::path& wanted) const;

		std::optional<std::string> StatusText() const;

		bool IsRunning() const { return !m_Running.empty(); }

	private:
		Core::JobSubmitter m_Submit;
		FileOperationRunner m_Run;
		std::vector<RunningFileOperation> m_Running;
		Core::Ref<Core::Mailbox<FinishedFileOperation>> m_Finished = Core::CreateRef<Core::Mailbox<FinishedFileOperation>>();
		std::uint64_t m_NextId = 1;
	};
}
