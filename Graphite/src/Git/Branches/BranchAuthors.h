#pragma once

#include "Core/Git/OwnAuthors.h"
#include "Core/Git/Ref.h"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Graphite {

	inline constexpr std::string_view kUnknownAuthorName = "Unknown author";

	struct BranchAuthors
	{
		std::unordered_map<Core::Oid, Core::TipAuthors> ByTip;

		std::optional<std::string> UserEmail;
	};

	struct BranchAuthor
	{
		// Empty for the unknown author, since no real e-mail is empty.
		std::string Key;
		std::string Name;

		std::chrono::sys_seconds When;
	};

	// Only tips whose commit was read. Sorted and without repeats, so two listings with the same
	// tips ask the same question.
	Core::OwnAuthorsRequest OwnAuthorsRequestFor(const std::vector<Core::Branch>& branches);

	std::vector<BranchAuthor> AuthorsOfBranch(const Core::Branch& branch, const BranchAuthors& authors);

	bool IsUserKey(std::string_view key, const BranchAuthors& authors);
}
