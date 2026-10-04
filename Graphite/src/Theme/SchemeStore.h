#pragma once

#include "Core/Base/Result.h"

#include "Theme/ColorScheme.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// The file holding scheme `name` (UTF-8) in `directory`. Through Core::FromUtf8, so a name with
	// non-ASCII characters is not read as the ANSI code page on the way to a path.
	std::filesystem::path SchemeFile(const std::filesystem::path& directory, std::string_view name);

	// The names of the .json files in the directory, without their extension, minus any that
	// collide with a built-in — a file named dark.json is ignored so the built-in keeps its
	// name.
	std::vector<std::string> ListSchemes(const std::filesystem::path& directory);

	// Never fails. A file that is missing, unreadable or malformed yields a scheme with the dark
	// base and no overrides, and warns — because a scheme on a drive that is not plugged in
	// should come back when the drive does, not take the editor down while it is out.
	ColorScheme LoadScheme(const std::filesystem::path& file);

	// An error when the name is a built-in or the write fails. Writes only the colours the
	// scheme actually overrides, so it stays sparse and keeps surviving a new token kind.
	Core::Result<void> SaveScheme(const std::filesystem::path& directory, std::string_view name,
								  const ColorScheme& scheme);

	Core::Result<void> DeleteScheme(const std::filesystem::path& directory, std::string_view name);

	// Why `name` cannot be a scheme's name, or nothing when it can: empty, longer than a file name
	// should be, a character or ending Windows refuses in a file name, a device name, or a name
	// already in `existing` (compared without regard to case, as the file system does). A
	// built-in's name is among what the caller lists as existing.
	std::optional<std::string> SchemeNameProblem(std::string_view name, const std::vector<std::string>& existing);

	// Renames the stored scheme file. An error when either name is a built-in or not a valid name,
	// when `from` is not there, when `to` is taken, or when the rename itself fails.
	Core::Result<void> RenameScheme(const std::filesystem::path& directory, std::string_view from,
									std::string_view to);
}
