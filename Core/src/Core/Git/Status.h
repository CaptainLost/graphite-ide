#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace Core {

	enum class StatusFlags : std::uint8_t
	{
		None = 0,
		New = 1 << 0,
		Modified = 1 << 1,
		Deleted = 1 << 2,
		Renamed = 1 << 3,
		TypeChanged = 1 << 4
	};

	constexpr StatusFlags operator|(StatusFlags left, StatusFlags right)
	{
		return static_cast<StatusFlags>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
	}

	constexpr StatusFlags operator&(StatusFlags left, StatusFlags right)
	{
		return static_cast<StatusFlags>(static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
	}

	constexpr StatusFlags& operator|=(StatusFlags& left, StatusFlags right)
	{
		left = left | right;
		return left;
	}

	struct FileStatus
	{
		// Relative to the working directory, so it can be compared against what the file
		// tree and the open documents hold without either side knowing the root.
		std::filesystem::path Path;

		std::optional<std::filesystem::path> RenamedFrom;

		// The two halves git tracks separately: what is staged, and what is not. A file
		// can be both — staged one way and edited again since.
		StatusFlags Index = StatusFlags::None;
		StatusFlags WorkTree = StatusFlags::None;

		bool IsConflicted = false;
		bool IsUntracked = false;

		// libgit2 collapses a wholly untracked directory into one status entry. A reader
		// asks this rather than looking for a separator on the end of Path, because Path
		// is stored without one - see RepositoryStatus.cpp.
		bool IsDirectory = false;
	};

	struct WorkingTreeStatus
	{
		std::vector<FileStatus> Entries;
	};
}
