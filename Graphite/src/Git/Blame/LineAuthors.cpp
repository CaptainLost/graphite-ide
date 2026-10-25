#include "Git/Blame/LineAuthors.h"

#include <utility>

namespace Graphite {

	LineAuthors BuildLineAuthors(Core::Ref<const HeadBlame> head, std::span<const Core::LineHunk> headToBuffer,
								 std::size_t bufferLineCount)
	{
		LineAuthors authors;
		authors.Head = std::move(head);
		authors.Lines.resize(bufferLineCount);

		if (authors.Head == nullptr || authors.Head->Blame == nullptr)
		{
			return authors;
		}
		const Core::Blame& blame = *authors.Head->Blame;

		for (std::size_t line = 0; line < bufferLineCount; ++line)
		{
			const Core::TracedLine traced = Core::TraceLineToOld(headToBuffer, line);
			LineAuthor& author = authors.Lines[line];

			// TraceLineToOld is zero-based; the blame and HeadLine are one-based.
			const std::size_t headLine = traced.OldLine + 1;
			switch (traced.Fate)
			{
			case Core::LineFate::Unchanged:
			{
				// Past the blame: the empty line after a final newline.
				const Core::BlameHunk* hunk = Core::HunkOfLine(blame, headLine);
				if (hunk == nullptr)
				{
					break;
				}
				author.Kind = LineAuthorKind::Committed;
				author.Hunk = static_cast<std::size_t>(hunk - blame.Hunks.data());
				author.HeadLine = headLine;
				break;
			}
			case Core::LineFate::Changed:
				author.Kind = LineAuthorKind::Uncommitted;
				author.HeadLine = headLine;
				break;
			case Core::LineFate::Added:
				author.Kind = LineAuthorKind::Uncommitted;
				break;
			}
		}
		return authors;
	}

	std::optional<Core::LineHistoryRequest> LineHistoryStart(const LineAuthors& authors, std::size_t bufferLine)
	{
		if (authors.Head == nullptr || authors.Head->Blame == nullptr || bufferLine >= authors.Lines.size())
		{
			return std::nullopt;
		}
		const LineAuthor& author = authors.Lines[bufferLine];
		if (author.Kind == LineAuthorKind::Unknown || !author.HeadLine.has_value())
		{
			return std::nullopt;
		}

		const Core::BlameHunk* hunk = Core::HunkOfLine(*authors.Head->Blame, *author.HeadLine);
		if (hunk == nullptr)
		{
			return std::nullopt;
		}

		Core::LineHistoryRequest request;
		request.From = hunk->Commit;
		request.File = hunk->OriginalPath;
		request.Line = hunk->OriginalFirstLine + (*author.HeadLine - hunk->FirstLine);
		return request;
	}
}
