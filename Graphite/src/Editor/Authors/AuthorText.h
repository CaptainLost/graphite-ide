#pragma once

#include "Git/Blame/LineAuthors.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	inline constexpr std::size_t kMinimumTitleCharacters = 8;
	inline constexpr float kAuthorQuietSeconds = 0.5f;
	inline constexpr std::string_view kYou = "You";
	inline constexpr std::string_view kUncommittedTitle = "Uncommitted changes";

	struct AuthorTextParts
	{
		std::string Lead;    // "Anna Kowalczyk, 3 months ago", or "You"
		std::string Name;    // "Anna Kowalczyk", or "You"
		std::string Title;   // the commit title, or "Uncommitted changes"
	};

	// Empty for an Unknown line, a line past the end, or when the blame or the line's commit is missing.
	std::optional<AuthorTextParts> AuthorTextPartsOf(const LineAuthors& authors, std::size_t bufferLine,
													 std::chrono::sys_seconds now);

	// The longest of the steps below that measures within available; nothing when not even the name does:
	// Lead + kBulletSeparator + Title, the same with the title cut and an ellipsis, Lead, Name. An empty title starts at
	// Lead.
	std::optional<std::string> FitAuthorText(const AuthorTextParts& parts, float available,
											 const std::function<float(std::string_view)>& measure);
}
