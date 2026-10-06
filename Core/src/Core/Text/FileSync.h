#pragma once

#include "Core/Base/Result.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace Core {

	class DocumentSet;

	enum class FileSyncOutcome
	{
		Unchanged,

		WentMissing,

		NeedsLoad,

		SafeToReload,

		NeedsDecision
	};

	FileSyncOutcome SyncWithFile(DocumentSet& documents, std::size_t itemIndex);

	Result<void> ReloadItem(DocumentSet& documents, std::size_t itemIndex);

	Result<void> SaveDocument(DocumentSet& documents, std::size_t itemIndex);

	struct SaveAllReport
	{
		std::size_t Saved = 0;

		std::vector<std::filesystem::path> Conflicted;

		struct Failure
		{
			std::filesystem::path Path;
			Error Error;
		};

		std::optional<Failure> StoppedAt;

		bool IsComplete() const
		{
			return !StoppedAt.has_value() && Conflicted.empty();
		}
	};

	SaveAllReport SaveAllModified(DocumentSet& documents);
}