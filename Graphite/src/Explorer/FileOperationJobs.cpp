#include "Explorer/FileOperationJobs.h"

#include "UI/TypographicMarks.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Count.h"
#include "Core/FileSystem/FileOperations.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/LoadingState.h"

#include <chrono>
#include <format>
#include <utility>

namespace Graphite {

	namespace {

		constexpr std::size_t kOneFile = 1;

		bool HasRunLongEnoughToShow(const RunningFileOperation& operation, Core::SteadyClock::time_point now)
		{
			const std::chrono::duration<double> runFor = now - operation.StartedAt;
			return runFor.count() >= Core::kSecondsBeforeLoaderShows;
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

		Core::Result<std::filesystem::path> DuplicateCountingFiles(const RunningFileOperation& operation)
		{
			std::atomic<std::size_t>& copiedFiles = *operation.CopiedFiles;
			const Core::CopiedFileCount noteCopied = [&copiedFiles](std::size_t copiedSoFar) {
				copiedFiles.store(copiedSoFar, std::memory_order_relaxed);
			};

			return Core::Duplicate(operation.Target, noteCopied);
		}

		Core::Result<std::filesystem::path> OutcomeOf(const RunningFileOperation& operation)
		{
			switch (operation.Kind)
			{
			case FileOperationKind::Duplicate:
				return DuplicateCountingFiles(operation);
			case FileOperationKind::MoveToRecycleBin:
				return MoveToRecycleBinAnsweringTarget(operation.Target);
			}

			return Core::Error{.Code = Core::ErrorCode::Unsupported, .Message = "not a file operation the Explorer runs"};
		}

		FinishedFileOperation RunOperation(const RunningFileOperation& operation)
		{
			return FinishedFileOperation{.Id = operation.Id,
										 .Kind = operation.Kind,
										 .Target = operation.Target,
										 .Folder = operation.Folder,
										 .Outcome = OutcomeOf(operation)};
		}

		bool HasId(const RunningFileOperation& operation, std::uint64_t id)
		{
			return operation.Id == id;
		}
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

	std::optional<std::string> FileOperationsStatusText(const std::vector<RunningFileOperation>& running,
														Core::SteadyClock::time_point now)
	{
		const RunningFileOperation* first = nullptr;
		std::size_t othersShown = 0;

		for (const RunningFileOperation& operation : running)
		{
			if (!HasRunLongEnoughToShow(operation, now))
			{
				continue;
			}

			if (first == nullptr)
			{
				first = &operation;
			}
			else
			{
				++othersShown;
			}
		}

		if (first == nullptr)
		{
			return std::nullopt;
		}

		const std::size_t copiedFiles = first->CopiedFiles != nullptr ? first->CopiedFiles->load(std::memory_order_relaxed) : 0;
		std::string text = RunningOperationText(first->Kind, first->Target, copiedFiles);

		if (othersShown > 0)
		{
			text += std::format(" and {} more", othersShown);
		}

		return text;
	}

	FileOperationJobs::FileOperationJobs(Core::JobSubmitter submit) : m_Submit(std::move(submit))
	{
	}

	void FileOperationJobs::Start(FileOperationKind kind, const std::filesystem::path& target,
								  const std::filesystem::path& folder, Clock::time_point now)
	{
		const RunningFileOperation operation{.Id = m_NextId++,
											 .Kind = kind,
											 .Target = target,
											 .Folder = folder,
											 .StartedAt = now,
											 .CopiedFiles = Core::CreateRef<std::atomic<std::size_t>>(0)};

		m_Running.push_back(operation);

		// WARNING: Nothing else may ask for a frame while the copy runs, and the status bar shows it only after the
		// loader's wait.
		Core::Redraw::RequestIn(std::chrono::duration<double>(Core::kSecondsBeforeLoaderShows));

		m_Submit([operation, finished = m_Finished] { finished->Post(RunOperation(operation)); });
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
			if (Core::IsSameOrWithin(path, operation.Target))
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
			if (HasRunLongEnoughToShow(operation, now) && Core::IsSameOrWithin(path, operation.Target))
			{
				return true;
			}
		}

		return false;
	}

	std::optional<std::string> FileOperationJobs::StatusText(Clock::time_point now) const
	{
		return FileOperationsStatusText(m_Running, now);
	}
}
