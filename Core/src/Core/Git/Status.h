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
		// Relative to the working directory, so it compares against the file tree and the open
		// documents without either of them knowing the root.
		std::filesystem::path Path;

		std::optional<std::filesystem::path> RenamedFrom;

		// A file can carry flags in both: staged one way and edited again since.
		StatusFlags Index = StatusFlags::None;
		StatusFlags WorkTree = StatusFlags::None;

		bool IsConflicted = false;
		bool IsUntracked = false;

		// libgit2 reports a wholly untracked directory as one entry. Path is stored without a
		// trailing separator, so ask this instead of looking for one.
		bool IsDirectory = false;
	};

	struct WorkingTreeStatus
	{
		std::vector<FileStatus> Entries;
	};
}
