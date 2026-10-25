#include "Editor/Authors/AuthorText.h"

#include "Git/History/CommitLabels.h"
#include "UI/TypographicMarks.h"

#include <vector>

namespace Graphite {

	namespace {

		constexpr std::string_view kYou = "You";
		constexpr std::string_view kUncommittedTitle = "Uncommitted changes";

		bool IsContinuationByte(char c)
		{
			return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
		}

		// Byte offsets of each code point's start, plus the size of the text as the last entry.
		std::vector<std::size_t> CodePointOffsets(std::string_view text)
		{
			std::vector<std::size_t> offsets;

			for (std::size_t i = 0; i < text.size(); ++i)
			{
				if (!IsContinuationByte(text[i]))
				{
					offsets.push_back(i);
				}
			}

			offsets.push_back(text.size());
			return offsets;
		}
	}

	std::optional<AuthorTextParts> AuthorTextPartsOf(const LineAuthors& authors, std::size_t bufferLine,
													 std::chrono::sys_seconds now)
	{
		if (bufferLine >= authors.Lines.size())
		{
			return std::nullopt;
		}

		const LineAuthor& line = authors.Lines[bufferLine];

		if (line.Kind == LineAuthorKind::Uncommitted)
		{
			return AuthorTextParts{std::string{kYou}, std::string{kYou}, std::string{kUncommittedTitle}};
		}

		if (line.Kind != LineAuthorKind::Committed || !authors.Head || !authors.Head->Blame)
		{
			return std::nullopt;
		}

		const Core::Blame& blame = *authors.Head->Blame;

		if (line.Hunk >= blame.Hunks.size())
		{
			return std::nullopt;
		}

		const auto commit = blame.Commits.find(blame.Hunks[line.Hunk].Commit);

		if (commit == blame.Commits.end())
		{
			return std::nullopt;
		}

		AuthorTextParts parts;
		parts.Name = AuthorName(commit->second.Author);
		parts.Lead = parts.Name + ", " + RelativeAge(commit->second.Author.When, now);
		parts.Title = commit->second.Summary;
		return parts;
	}

	std::optional<std::string> FitAuthorText(const AuthorTextParts& parts, float available,
											 const std::function<float(std::string_view)>& measure)
	{
		const std::string head = parts.Lead + std::string{kAuthorSeparator};

		std::string full = head + parts.Title;

		if (measure(full) <= available)
		{
			return full;
		}

		const std::vector<std::size_t> offsets = CodePointOffsets(parts.Title);
		const std::size_t titleCharacters = offsets.size() - 1;

		// Largest cut first; every candidate ends on a code point boundary.
		for (std::size_t n = titleCharacters; n >= kMinimumTitleCharacters && n > 0; --n)
		{
			std::string cut = head + parts.Title.substr(0, offsets[n]) + std::string{kEllipsis};

			if (measure(cut) <= available)
			{
				return cut;
			}
		}

		if (measure(parts.Lead) <= available)
		{
			return parts.Lead;
		}

		if (parts.Name != parts.Lead && measure(parts.Name) <= available)
		{
			return parts.Name;
		}

		return std::nullopt;
	}
}
