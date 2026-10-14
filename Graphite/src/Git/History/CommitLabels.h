#pragma once

#include "Theme/GitPalette.h"

#include "Core/Git/Diff.h"
#include "Core/Git/Signature.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	std::string RelativeAge(std::chrono::sys_seconds when, std::chrono::sys_seconds now);

	std::string AuthorInitials(const Core::Signature& signature);

	std::string AuthorName(const Core::Signature& signature);

	inline constexpr std::string_view Ellipsis = "\xE2\x80\xA6";

	std::optional<std::size_t> CutToFit(std::string_view text, float maxWidth, float ellipsisWidth,
										const std::function<float(std::string_view)>& widthOf);

	char ChangeLetter(Core::FileChange change);

	const char* ChangeName(Core::FileChange change);

	ImU32 ChangeColor(Core::FileChange change, const GitPalette& palette);

	// WARNING: Not named DisplayPath, because that is already a struct in Graphite::Search (HitDisplay.h) and a unit including both would have the function hide the type.
	const std::filesystem::path& ShownPath(const Core::FileDiff& file);

	std::optional<std::size_t> IndexOfFile(const Core::Diff& diff,
										   const std::filesystem::path& path);
}
