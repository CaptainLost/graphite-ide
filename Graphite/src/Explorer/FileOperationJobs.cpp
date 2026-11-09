#include "Explorer/FileOperationJobs.h"

#include "UI/TypographicMarks.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Count.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/LoadingState.h"

#include <chrono>
#include <exception>
#include <format>
#include <string_view>
#include <utility>

namespace Graphite {

	namespace {

		constexpr std::size_t kOneFile = 1;

		bool HasRunLongEnoughToShow(const RunningFileOperation& operation, Core::SteadyClock::time_point now)
		{
			const std::chrono::duration<double> runFor = now - operation.StartedAt;
			return runFor.count() >= Core::kSecondsBeforeLoaderShows;
		}

		bool IsOnPathOf(const std::filesystem::path& path, const RunningFileOperation& operation)
		{
			const bool onTarget = Core::IsSameOrWithin(path, operation.Target);
			const bool onCopy = !operation.CopyDestination.empty() && Core::IsSameOrWithin(path, operation.CopyDestination);

			return onTarget || onCopy;
		}

		bool HoldsPathOf(const std::filesystem::path& folder, const RunningFileOperation& operation)
		{
			const bool holdsTarget = Core::IsSameOrWithin(operation.Target, folder);
			const bool holdsCopy = !operation.CopyDestination.empty() && Core::IsSameOrWithin(operation.CopyDestination, folder);

			return holdsTarget || holdsCopy;
		}

		std::string CopiedFilesText(std::size_t copiedFiles)
		{
			return Core::FormatCount(copiedFiles) + (copiedFiles == kOneFile ? " file" : " files");
		}

		Core::Result<std::filesystem::path> MoveToRecycleBinAnsweringTarget(const std::filesystem::path& target)
		{
			const Core::Result<void> moved = Core::MoveToRecycleBin(target);

			if (!moved)
			{
				return moved.GetError();
			}

			return target;
		}

		std::string StoppedText(const RunningFileOperation& operation, std::string_view reason)
		{
			const std::string name = Core::ToUtf8(operation.Target.filename());

			switch (operation.Kind)
			{
			case FileOperationKind::Duplicate:
				return std::format("the copy of '{}' stopped: {}", name, reason);
			case FileOperationKind::MoveToRecycleBin:
				return std::format("moving '{}' to the Recycle Bin stopped: {}", name, reason);
			}

			return std::format("the operation on '{}' stopped: {}", name, reason);
		}

		// WARNING: A job that throws posts nothing, which would leave its path busy for good.
		Core::Result<std::filesystem::path> OutcomeCatchingThrows(const RunningFileOperation& operation,
																  const FileOperationRunner& run)
		{
			std::atomic<std::size_t>& copiedFiles = *operation.CopiedFiles;
			const Core::CopiedFileCount noteCopied = [&copiedFiles](std::size_t copiedSoFar) {
				copiedFiles.store(copiedSoFar, std::memory_order_relaxed);
			};

			try
			{
				return run(operation, noteCopied);
			}
			catch (const std::exception& thrown)
			{
				return Core::Error{.Code = Core::ErrorCode::IoFailure, .Message = StoppedText(operation, thrown.what())};
			}
			catch (...)
			{
				return Core::Error{.Code = Core::ErrorCode::IoFailure, .Message = StoppedText(operation, "unknown failure")};
			}
		}

		FinishedFileOperation RunOperation(const RunningFileOperation& operation, const FileOperationRunner& run)
		{
			return FinishedFileOperation{.Id = operation.Id,
										 .Kind = operation.Kind,
										 .Target = operation.Target,
										 .FolderOpenAtStart = operation.FolderOpenAtStart,
										 .CopyOrMovedPath = OutcomeCatchingThrows(operation, run)};
		}

		bool HasId(const RunningFileOperation& operation, std::uint64_t id)
		{
			return operation.Id == id;
		}
	}

	Core::Result<std::filesystem::path> RunFileOperation(const RunningFileOperation& operation,
														 const Core::CopiedFileCount& noteCopied)
	{
		switch (operation.Kind)
		{
		case FileOperationKind::Duplicate:
			return Core::DuplicateTo(operation.Target, operation.CopyDestination, noteCopied);
		case FileOperationKind::MoveToRecycleBin:
			return MoveToRecycleBinAnsweringTarget(operation.Target);
		}

		return Core::Error{.Code = Core::ErrorCode::Unsupported, .Message = "not a file operation the Explorer runs"};
	}

	std::string RunningOperationText(FileOperationKind kind, const std::filesystem::path& target, std::size_t copiedFiles)
	{
		const std::string name = Core::ToUtf8(target.filename());

		switch (kind)
		{
		case FileOperationKind::Duplicate:
			if (copiedFiles == 0)
			{
				return std::format("Duplicating {}{}", name, kEllipsis);
			}

			return std::format("Duplicating {}{} {}", name, kEllipsis, CopiedFilesText(copiedFiles));
		case FileOperationKind::MoveToRecycleBin:
			return std::format("Moving {} to the Recycle Bin{}", name, kEllipsis);
		}

		return {};
	}

	std::optional<std::string> FileOperationsStatusText(const std::vector<RunningFileOperation>& running)
	{
		if (running.empty())
		{
			return std::nullopt;
		}

		const RunningFileOperation& oldest = running.front();
		const std::size_t copiedFiles = oldest.CopiedFiles != nullptr ? oldest.CopiedFiles->load(std::memory_order_relaxed) : 0;
		std::string text = RunningOperationText(oldest.Kind, oldest.Target, copiedFiles);

		const std::size_t othersRunning = running.size() - 1;

		if (othersRunning > 0)
		{
			text += std::format(" and {} more", othersRunning);
		}

		return text;
	}

	FileOperationJobs::FileOperationJobs(Core::JobSubmitter submit, FileOperationRunner run)
		: m_Submit(std::move(submit)), m_Run(std::move(run))
	{
	}

	void FileOperationJobs::Start(FileOperationKind kind, const std::filesystem::path& target,
								  const std::filesystem::path& folderOpen, Clock::time_point now)
	{
		const std::filesystem::path copyDestination =
			kind == FileOperationKind::Duplicate ? PickUniquePath(target) : std::filesystem::path{};

		Launch(kind, target, copyDestination, folderOpen, now);
	}

	void FileOperationJobs::StartCopyInto(const std::filesystem::path& target, const std::filesystem::path& intoFolder,
										  const std::filesystem::path& folderOpen, Clock::time_point now)
	{
		Launch(FileOperationKind::Duplicate, target, PickUniquePath(intoFolder / target.filename()), folderOpen, now);
	}

	void FileOperationJobs::Launch(FileOperationKind kind, const std::filesystem::path& target,
								   const std::filesystem::path& copyDestination, const std::filesystem::path& folderOpen,
								   Clock::time_point now)
	{
		const RunningFileOperation operation{.Id = m_NextId++,
											 .Kind = kind,
											 .Target = target,
											 .CopyDestination = copyDestination,
											 .FolderOpenAtStart = folderOpen,
											 .StartedAt = now,
											 .CopiedFiles = Core::CreateRef<std::atomic<std::size_t>>(0)};

		m_Running.push_back(operation);

		// WARNING: Nothing else may ask for the frame in which the busy rows' loader is due.
		Core::Redraw::RequestIn(std::chrono::duration<double>(Core::kSecondsBeforeLoaderShows));

		m_Submit([operation, run = m_Run, finished = m_Finished] { finished->Post(RunOperation(operation, run)); });
	}

	std::vector<FinishedFileOperation> FileOperationJobs::TakeFinished()
	{
		std::vector<FinishedFileOperation> finished = m_Finished->TakeAll();

		for (const FinishedFileOperation& operation : finished)
		{
			std::erase_if(m_Running, [&operation](const RunningFileOperation& running) { return HasId(running, operation.Id); });
		}

		return finished;
	}

	bool FileOperationJobs::IsBusy(const std::filesystem::path& path) const
	{
		for (const RunningFileOperation& operation : m_Running)
		{
			if (IsOnPathOf(path, operation))
			{
				return true;
			}
		}

		return false;
	}

	bool FileOperationJobs::IsBusyOrHoldsABusyPath(const std::filesystem::path& path) const
	{
		for (const RunningFileOperation& operation : m_Running)
		{
			if (IsOnPathOf(path, operation) || HoldsPathOf(path, operation))
			{
				return true;
			}
		}

		return false;
	}

	bool FileOperationJobs::ShowsBusy(const std::filesystem::path& path, Clock::time_point now) const
	{
		for (const RunningFileOperation& operation : m_Running)
		{
			if (HasRunLongEnoughToShow(operation, now) && IsOnPathOf(path, operation))
			{
				return true;
			}
		}

		return false;
	}

	bool FileOperationJobs::IsReservedForACopy(const std::filesystem::path& path) const
	{
		for (const RunningFileOperation& operation : m_Running)
		{
			if (!operation.CopyDestination.empty() && Core::SamePath(path, operation.CopyDestination))
			{
				return true;
			}
		}

		return false;
	}

	std::filesystem::path FileOperationJobs::PickUniquePath(const std::filesystem::path& wanted) const
	{
		const Core::PathTaken reservedForACopy = [this](const std::filesystem::path& path) { return IsReservedForACopy(path); };

		return Core::MakeUniquePath(wanted, reservedForACopy);
	}

	std::optional<std::string> FileOperationJobs::StatusText() const
	{
		return FileOperationsStatusText(m_Running);
	}
}
