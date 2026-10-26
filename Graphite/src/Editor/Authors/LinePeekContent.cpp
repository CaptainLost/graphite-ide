#include "Editor/Authors/LinePeekContent.h"

#include "Editor/Authors/AuthorText.h"
#include "Git/History/CommitDetails.h"
#include "Git/History/CommitLabels.h"

#include "Core/Base/Strings.h"

namespace Graphite {

	namespace {

		constexpr std::string_view kDot = " \xC2\xB7 ";

		std::string WithoutLineEnd(std::string_view text)
		{
			if (text.ends_with('\n'))
			{
				text.remove_suffix(1);
			}

			if (text.ends_with('\r'))
			{
				text.remove_suffix(1);
			}

			return std::string{text};
		}

		// Empty when HEAD has no such line.
		std::string HeadLineText(const std::string& headText, std::size_t oneBasedLine)
		{
			std::size_t start = 0;

			for (std::size_t line = 1; line < oneBasedLine; ++line)
			{
				const std::size_t end = headText.find('\n', start);

				if (end == std::string::npos)
				{
					return {};
				}

				start = end + 1;
			}

			if (start >= headText.size())
			{
				return {};
			}

			const std::size_t end = headText.find('\n', start);
			return WithoutLineEnd(std::string_view{headText}.substr(start, end == std::string::npos ? end : end - start));
		}

		PeekEntry UncommittedEntry(const LinePeekInput& input, const LineAuthor& author)
		{
			PeekEntry entry;
			entry.Title = std::string{kUncommittedTitle};
			entry.Meta = std::string{kYou};
			entry.Byline = std::string{kYou} + std::string{kDot} + "not committed yet";

			if (author.HeadLine.has_value())
			{
				entry.Lines.push_back({Core::DiffLineKind::Removed,
									   HeadLineText(input.Authors->Head->HeadText, *author.HeadLine), true});
			}

			entry.Lines.push_back({Core::DiffLineKind::Added, WithoutLineEnd(input.BufferLine), true});
			return entry;
		}

		PeekEntry CommittedEntry(const LinePeekInput& input, const Core::LineChange& change)
		{
			const std::string author = AuthorName(change.Commit.Author);

			PeekEntry entry;
			entry.Title = change.Commit.Summary;
			entry.Meta = author + std::string{kDot} + RelativeAge(change.Commit.Author.When, input.Now) +
						 std::string{kDot} + change.Commit.Id.ShortHex();
			entry.Byline = author + std::string{kDot} +
						   FormattedTimestamp(change.Commit.Author.When, input.Offset);
			entry.Commit = change.Commit.Id;
			entry.Path = change.Path;

			const auto line = static_cast<long long>(change.Line);

			for (const Core::Hunk& hunk : change.Hunks)
			{
				for (const Core::DiffLine& diffLine : hunk.Lines)
				{
					bool tracked = false;

					if (diffLine.Kind == Core::DiffLineKind::Added)
					{
						tracked = diffLine.NewLine == line;
					}
					else if (diffLine.Kind == Core::DiffLineKind::Removed)
					{
						tracked = change.ParentLine.has_value() &&
								  diffLine.OldLine == static_cast<long long>(*change.ParentLine);
					}

					entry.Lines.push_back({diffLine.Kind, WithoutLineEnd(diffLine.Text), tracked});
				}
			}

			return entry;
		}

		void FillHeader(LinePeekContent& content, std::size_t line)
		{
			content.Header = "History of line " + std::to_string(line + 1) + std::string{kDot} +
							 Core::Counted(static_cast<long long>(content.Entries.size()), "change") +
							 std::string{kDot} + "follows renames";
		}
	}

	LinePeekContent BuildLinePeekContent(const LinePeekInput& input)
	{
		LinePeekContent content;

		if (input.Authors == nullptr || input.Authors->Head == nullptr)
		{
			return content;
		}

		if (!input.Authors->Head->Tracked || input.Authors->Head->Unusable)
		{
			content.State = PeekState::Failed;
			content.Error = "This file has no committed history.";
			return content;
		}

		if (input.Line >= input.Authors->Lines.size())
		{
			return content;
		}

		const LineAuthor& author = input.Authors->Lines[input.Line];

		if (author.Kind == LineAuthorKind::Unknown)
		{
			return content;
		}

		const bool uncommitted = author.Kind == LineAuthorKind::Uncommitted;

		if (uncommitted)
		{
			content.Entries.push_back(UncommittedEntry(input, author));

			if (!author.HeadLine.has_value())
			{
				content.State = PeekState::Ready;
				FillHeader(content, input.Line);
				return content;
			}
		}

		if (input.History != nullptr && input.History->Error.has_value())
		{
			content.Entries.clear();
			content.State = PeekState::Failed;
			content.Error = input.History->Error->Message;
			return content;
		}

		if (input.History == nullptr || input.History->History == nullptr)
		{
			content.Entries.clear();
			return content;
		}

		for (const Core::LineChange& change : input.History->History->Changes)
		{
			content.Entries.push_back(CommittedEntry(input, change));
		}

		content.StopsEarly = !input.History->History->Complete;
		content.State = PeekState::Ready;
		FillHeader(content, input.Line);
		return content;
	}
}
