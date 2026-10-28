#pragma once

#include "Theme/GitPalette.h"

#include "Core/Git/Diff.h"
#include "Core/Git/Signature.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace Graphite {

	std::string RelativeAge(std::chrono::sys_seconds when, std::chrono::sys_seconds now);

	// The reader's local zone, looked up once: a lookup is an ICU call and a search of every zone. Its offset is taken at
	// each instant shown, through Core::OffsetAt, since it changes at a daylight-saving boundary.
	const std::chrono::time_zone* ReaderTimeZone();

	std::string AuthorInitials(const Core::Signature& signature);

	std::string AuthorName(const Core::Signature& signature);

	char ChangeLetter(Core::FileChange change);

	const char* ChangeName(Core::FileChange change);

	ImU32 ChangeColor(Core::FileChange change, const GitPalette& palette);

	// WARNING: Not named DisplayPath, because that is already a struct in Graphite::Search (HitDisplay.h) and a unit including both would have the function hide the type.
	const std::filesystem::path& ShownPath(const Core::FileDiff& file);

	std::optional<std::size_t> IndexOfFile(const Core::Diff& diff,
										   const std::filesystem::path& path);
}
