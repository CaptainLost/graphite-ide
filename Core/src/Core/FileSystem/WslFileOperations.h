#pragma once

#include "Core/Base/Result.h"
#include "Core/FileSystem/WslSharePath.h"

#include <filesystem>
#include <optional>
#include <string_view>

namespace Core {

	// What a change under a WSL share goes through instead of Win32, which cannot write there
	// (ReplaceFileW fails with error 50) and would drop Linux permission bits (ADR 0696).
	// Graphite registers an implementation over the distro's agent.
	class WslFileOperations
	{
	public:
		virtual ~WslFileOperations() = default;

		virtual Result<void> WriteFile(const WslSharePath& place, std::string_view bytes) = 0;
		virtual Result<void> Rename(const WslSharePath& from, const WslSharePath& to) = 0;
		virtual Result<void> MakeDirectory(const WslSharePath& place) = 0;
		virtual Result<void> CreateEmptyFile(const WslSharePath& place) = 0;
		virtual Result<void> MoveToTrash(const WslSharePath& place) = 0;
	};

	// nullptr, the default, means none: a change under a WSL share is then refused.
	void SetWslFileOperations(WslFileOperations* operations);
	WslFileOperations* GetWslFileOperations();

	// For the Core functions that write: when path is under a WSL share, the operations to use,
	// or the error to return when none are registered; empty for any other path.
	struct WslRoute
	{
		WslSharePath Place;
		WslFileOperations* Operations = nullptr;
	};
	std::optional<Result<WslRoute>> RouteThroughWsl(const std::filesystem::path& path);
}
