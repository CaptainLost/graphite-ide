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

	FileSyncOutcome SyncWithFile(DocumentSet& documents, std::size_t index);

	Result<void> ReloadItem(DocumentSet& documents, std::size_t index);

	Result<void> SaveDocument(DocumentSet& documents, std::size_t index);

	struct SaveAllReport
	{
		std::size_t saved = 0;

		std::vector<std::filesystem::path> conflicted;

		struct Failure
		{
			std::filesystem::path path;
			Error error;
		};

		std::optional<Failure> stoppedAt;

		bool IsComplete() const
		{
			return !stoppedAt.has_value() && conflicted.empty();
		}
	};

	SaveAllReport SaveAllModified(DocumentSet& documents);
}