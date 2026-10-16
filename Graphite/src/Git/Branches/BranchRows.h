#pragma once

#include "Git/Branches/BranchAuthors.h"
#include "Git/Branches/BranchListFilter.h"
#include "Persistence/PersistedState.h"

#include "Core/Git/Ref.h"

#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	inline constexpr std::size_t kBranchSectionCount = 2;

	struct AuthorGroupKey
	{
		Core::RefKind Section = Core::RefKind::Local;
		std::string Author;

		friend std::strong_ordering operator<=>(const AuthorGroupKey&, const AuthorGroupKey&) = default;
		friend bool operator==(const AuthorGroupKey&, const AuthorGroupKey&) = default;
	};

	struct BranchRowsInput
	{
		const std::vector<Core::Branch>* Branches = nullptr;

		// Null while no answer has landed.
		const BranchAuthors* Authors = nullptr;

		// Read only while Authors is null.
		std::optional<std::string> AuthorsFailure;

		BranchListFilter Filter;
		BranchListState Options;

		std::array<bool, kBranchSectionCount> CollapsedSections{};
		std::set<AuthorGroupKey> ExpandedGroups;
	};

	enum class BranchRowKind
	{
		Section,
		AuthorGroup,
		Branch,
		Loading,
		Failed,
		NoMatches
	};

	struct BranchRow
	{
		BranchRowKind Kind = BranchRowKind::Branch;
		Core::RefKind Section = Core::RefKind::Local;

		std::size_t Depth = 0;

		std::string Label;

		std::size_t BranchIndex = 0;
		std::string AuthorKey;

		std::size_t Listed = 0;
		std::size_t Total = 0;

		bool Collapsed = false;

		friend bool operator==(const BranchRow&, const BranchRow&) = default;
	};

	struct AuthorChoice
	{
		std::string Key;
		std::string Name;

		std::size_t Branches = 0;

		bool IsUser = false;
		bool Ticked = false;

		friend bool operator==(const AuthorChoice&, const AuthorChoice&) = default;
	};

	std::size_t SectionIndexOf(Core::RefKind section);

	bool ViewNeedsAuthors(const BranchRowsInput& input);

	std::vector<BranchRow> BuildBranchRows(const BranchRowsInput& input);

	std::vector<AuthorChoice> ListAuthorChoices(const BranchRowsInput& input);

	std::vector<AuthorChoice> FilterAuthorChoices(const std::vector<AuthorChoice>& choices, std::string_view query);

	std::string SectionHeadingCount(const BranchRow& row);

	std::string AuthorGroupCount(const BranchRow& row);

	std::string AuthorChoiceText(const AuthorChoice& choice);

	std::string AuthorsFailureText(std::string_view message);

	std::string BranchTooltipText(const Core::Branch& branch, std::string_view label,
								  const std::vector<BranchAuthor>& authors, std::chrono::minutes localOffset);
}
