#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <string>
#include <string_view>

// The freedesktop.org trash, the one Linux file managers restore from: the item moves into
// <trash>/files/<name>, and <trash>/info/<name>.trashinfo records where it came from and when.
namespace Agent {

	// $XDG_DATA_HOME/Trash, or ~/.local/share/Trash.
	std::filesystem::path HomeTrash();

	// Moves item into trashRoot under a name free in both files/ and info/ ("a.txt", "a.txt 2",
	// ...). NotFound when the item is missing; Unsupported when it is on another file system than
	// the trash, where a move would be a copy (the host then asks before deleting for good).
	Core::Result<std::filesystem::path> MoveToTrash(const std::filesystem::path& item,
													const std::filesystem::path& trashRoot);

	// The Path= value of a .trashinfo: unreserved characters and '/' as they are, every other
	// byte as %XX.
	std::string PercentEncodePath(std::string_view path);
}
