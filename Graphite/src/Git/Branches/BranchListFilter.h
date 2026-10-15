#pragma once

#include "Git/Branches/BranchAuthors.h"

#include "Core/Git/Ref.h"

#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	struct BranchListFilter
	{
		std::string Text;
		bool AlsoMessage = false;

		// Author keys: e-mails lower-cased, the empty key for the unknown author.
		std::set<std::string> Authors;

		friend bool operator==(const BranchListFilter&, const BranchListFilter&) = default;
	};

	std::vector<std::string> LoweredWordsOf(std::string_view text);

	bool MatchesWords(const Core::Branch& branch, const std::vector<BranchAuthor>& authors,
					  const std::vector<std::string>& loweredWords, bool alsoMessage);

	bool AuthorHoldsAnyWord(const BranchAuthor& author, const std::vector<std::string>& loweredWords);

	// Empty when no word is in the name, as when only the tip's message or an author matched.
	std::string_view FirstWordInName(std::string_view name, const std::vector<std::string>& loweredWords);
}
