#include "corepch.h"

#include "Core/FileSystem/WslFileOperations.h"

#include <atomic>
#include <format>

namespace Core {

	namespace {

		std::atomic<WslFileOperations*> g_Operations{nullptr};
	}

	void SetWslFileOperations(WslFileOperations* operations)
	{
		g_Operations.store(operations);
	}

	WslFileOperations* GetWslFileOperations()
	{
		return g_Operations.load();
	}

	std::optional<Result<WslRoute>> RouteThroughWsl(const std::filesystem::path& path)
	{
		std::optional<WslSharePath> place = ParseWslSharePath(path);

		if (!place)
		{
			return std::nullopt;
		}

		WslFileOperations* const operations = GetWslFileOperations();

		if (operations == nullptr)
		{
			return Result<WslRoute>{
				Error{ErrorCode::Unsupported, std::format("Graphite is not connected to {}, so it cannot change files there", place->Distro)}};
		}

		return Result<WslRoute>{WslRoute{.Place = std::move(*place), .Operations = operations}};
	}
}
