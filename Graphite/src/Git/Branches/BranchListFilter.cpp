#include "Git/Branches/BranchListFilter.h"

#include "Core/Base/Ascii.h"

namespace Graphite {

	namespace {

		constexpr char kWordSeparator = ' ';

		bool MessageHolds(const Core::Branch& branch, std::string_view loweredWord)
		{
			if (!branch.TipCommit.has_value())
			{
				return false;
			}

			return Core::LoweredTextContains(branch.TipCommit->Summary, loweredWord) ||
				   Core::LoweredTextContains(branch.TipCommit->Body, loweredWord);
		}

		// WARNING: The unknown author's name is a stand-in, not anyone's name, so typing "unknown"
		// would otherwise list every branch whose tip cannot be read.
		bool AuthorHolds(const BranchAuthor& author, std::string_view loweredWord)
		{
			if (author.Key.empty())
			{
				return false;
			}

			return Core::LoweredTextContains(author.Name, loweredWord) || author.Key.find(loweredWord) != std::string::npos;
		}

		bool AnyAuthorHolds(const std::vector<BranchAuthor>& authors, std::string_view loweredWord)
		{
			for (const BranchAuthor& author : authors)
			{
				if (AuthorHolds(author, loweredWord))
				{
					return true;
				}
			}

			return false;
		}
	}

	std::vector<std::string> LoweredWordsOf(std::string_view text)
	{
		const std::string loweredText = Core::Lowered(text);

		std::vector<std::string> words;
		std::size_t wordStart = 0;

		while (wordStart < loweredText.size())
		{
			const std::size_t wordEnd = loweredText.find(kWordSeparator, wordStart);
			const std::size_t wordLength =
				wordEnd == std::string::npos ? loweredText.size() - wordStart : wordEnd - wordStart;

			if (wordLength > 0)
			{
				words.push_back(loweredText.substr(wordStart, wordLength));
			}

			if (wordEnd == std::string::npos)
			{
				break;
			}

			wordStart = wordEnd + 1;
		}

		return words;
	}

	bool AuthorHoldsAnyWord(const BranchAuthor& author, const std::vector<std::string>& loweredWords)
	{
		for (const std::string& word : loweredWords)
		{
			if (AuthorHolds(author, word))
			{
				return true;
			}
		}

		return false;
	}

	bool MatchesWords(const Core::Branch& branch, const std::vector<BranchAuthor>& authors,
					  const std::vector<std::string>& loweredWords, bool alsoMessage)
	{
		for (const std::string& word : loweredWords)
		{
			const bool nameHoldsWord = Core::LoweredTextContains(branch.Name, word);
			const bool messageHoldsWord = alsoMessage && MessageHolds(branch, word);

			if (!nameHoldsWord && !messageHoldsWord && !AnyAuthorHolds(authors, word))
			{
				return false;
			}
		}

		return true;
	}

	std::string_view FirstWordInName(std::string_view name, const std::vector<std::string>& loweredWords)
	{
		for (const std::string& word : loweredWords)
		{
			if (Core::LoweredTextContains(name, word))
			{
				return word;
			}
		}

		return std::string_view{};
	}
}
