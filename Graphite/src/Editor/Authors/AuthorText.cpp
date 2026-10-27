#include "Editor/Authors/AuthorText.h"

#include "Git/History/CommitLabels.h"
#include "UI/TypographicMarks.h"

#include <vector>

namespace Graphite {

	namespace {

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
		// A commit with no title would leave the separator hanging at the end.
		if (!parts.Title.empty())
		{
			const std::string head = parts.Lead + std::string{kBulletSeparator};

			std::string full = head + parts.Title;

			if (measure(full) <= available)
			{
				return full;
			}

			const std::vector<std::size_t> offsets = CodePointOffsets(parts.Title);
			const std::size_t titleCharacters = offsets.size() - 1;

			const auto cutAt = [&](std::size_t characters) {
				return head + parts.Title.substr(0, offsets[characters]) + std::string{kEllipsis};
			};

			// WARNING: The longest cut that fits is found by halving, not by trying each from the longest down: this
			// runs every frame the caret line's author is drawn, and a long title cost a measure a character. Every
			// candidate ends on a code point boundary.
			if (titleCharacters >= kMinimumTitleCharacters && measure(cutAt(kMinimumTitleCharacters)) <= available)
			{
				std::size_t fitting = kMinimumTitleCharacters;
				std::size_t tooLong = titleCharacters + 1;

				while (tooLong - fitting > 1)
				{
					const std::size_t middle = fitting + (tooLong - fitting) / 2;

					if (measure(cutAt(middle)) <= available)
					{
						fitting = middle;
					}
					else
					{
						tooLong = middle;
					}
				}

				return cutAt(fitting);
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
