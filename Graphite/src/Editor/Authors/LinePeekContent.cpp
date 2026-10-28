#include "Editor/Authors/LinePeekContent.h"

#include "Editor/Authors/AuthorText.h"
#include "Git/History/CommitDetails.h"
#include "Git/History/CommitLabels.h"
#include "UI/TypographicMarks.h"

#include "Core/Base/Strings.h"

#include <algorithm>

namespace Graphite {

	namespace {

		// Lines of context kept either side of the change around the tracked line, as many as git gives a hunk.
		constexpr std::size_t kContextLines = 3;

		// A run of changed lines up to this long is shown whole; a longer one, such as a file's first commit where the
		// run is the whole file, only around its tracked lines.
		constexpr std::size_t kLongestWholeRun = 2 * kContextLines + 1;

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

		PeekEntry UncommittedEntry(const LinePeekInput& input, const LineAuthor& author, bool withLines)
		{
			PeekEntry entry;
			entry.Title = std::string{kUncommittedTitle};
			entry.Meta = std::string{kYou};
			entry.Byline = std::string{kYou} + std::string{kMiddleDotSeparator} + "not committed yet";

			if (!withLines)
			{
				return entry;
			}

			if (author.HeadLine.has_value())
			{
				entry.Lines.push_back({Core::DiffLineKind::Removed,
									   HeadLineText(input.Authors->Head->HeadText, *author.HeadLine), true});
			}

			entry.Lines.push_back({Core::DiffLineKind::Added, WithoutLineEnd(input.BufferLine), true});
			return entry;
		}

		bool IsTracked(const Core::DiffLine& diffLine, const Core::LineChange& change)
		{
			if (diffLine.Kind == Core::DiffLineKind::Added)
			{
				return diffLine.NewLine == static_cast<long long>(change.Line);
			}

			if (diffLine.Kind == Core::DiffLineKind::Removed)
			{
				return change.ParentLine.has_value() && diffLine.OldLine == static_cast<long long>(*change.ParentLine);
			}

			return false;
		}

		struct KeptLines
		{
			std::size_t First = 0;
			std::size_t Last = 0;   // inclusive
		};

		// The lines either side of one tracked line that stay: its whole run of changed lines and the context around it
		// when the run is short, or only the lines closest to it when the run is long.
		KeptLines KeptAround(const std::vector<Core::DiffLine>& lines, std::size_t tracked)
		{
			const auto changed = [&lines](std::size_t index) { return lines[index].Kind != Core::DiffLineKind::Context; };

			std::size_t runFirst = tracked;
			std::size_t runLast = tracked;

			while (runFirst > 0 && changed(runFirst - 1) && changed(tracked))
			{
				--runFirst;
			}

			while (runLast + 1 < lines.size() && changed(runLast + 1) && changed(tracked))
			{
				++runLast;
			}

			const bool wholeRun = runLast - runFirst + 1 <= kLongestWholeRun;
			const std::size_t from = wholeRun ? runFirst : tracked;
			const std::size_t to = wholeRun ? runLast : tracked;

			return KeptLines{.First = from - std::min(from, kContextLines),
							 .Last = std::min(lines.size() - 1, to + kContextLines)};
		}

		void AppendOmitted(std::vector<PeekDiffLine>& out)
		{
			if (out.empty() || !out.back().Omitted)
			{
				out.push_back(PeekDiffLine{.Text = std::string{kEllipsis}, .Omitted = true});
			}
		}

		// WARNING: Not the whole hunk. A line untouched since its file was created has the creating commit as its one
		// entry, whose hunk is the whole file: copied whole, the tracked line sat far below the top of the detail, and a
		// file of ten thousand lines was copied and measured for every frame drawn.
		void AppendTrimmed(std::vector<PeekDiffLine>& out, const Core::Hunk& hunk, const Core::LineChange& change)
		{
			const std::vector<Core::DiffLine>& lines = hunk.Lines;

			if (lines.empty())
			{
				return;
			}

			std::vector<KeptLines> kept;

			for (std::size_t index = 0; index < lines.size(); ++index)
			{
				if (IsTracked(lines[index], change))
				{
					kept.push_back(KeptAround(lines, index));
				}
			}

			// A hunk that holds no tracked line, which a diff read differently can give, shows its first change.
			if (kept.empty())
			{
				const auto firstChange = std::ranges::find_if(lines, [](const Core::DiffLine& diffLine) {
					return diffLine.Kind != Core::DiffLineKind::Context;
				});
				const std::size_t anchor =
					firstChange == lines.end() ? 0 : static_cast<std::size_t>(firstChange - lines.begin());

				kept.push_back(KeptAround(lines, anchor));
			}

			std::size_t next = 0;

			for (const KeptLines& span : kept)
			{
				if (span.Last < next)
				{
					continue;
				}

				const std::size_t first = std::max(span.First, next);

				if (first > next)
				{
					AppendOmitted(out);
				}

				for (std::size_t index = first; index <= span.Last; ++index)
				{
					const Core::DiffLine& diffLine = lines[index];
					out.push_back({diffLine.Kind, WithoutLineEnd(diffLine.Text), IsTracked(diffLine, change)});
				}

				next = span.Last + 1;
			}

			if (next < lines.size())
			{
				AppendOmitted(out);
			}
		}

		PeekEntry CommittedEntry(const LinePeekInput& input, const Core::LineChange& change, bool withLines)
		{
			const std::string author = AuthorName(change.Commit.Author);

			PeekEntry entry;
			entry.Title = change.Commit.Summary;
			entry.Meta = author + std::string{kMiddleDotSeparator} + RelativeAge(change.Commit.Author.When, input.Now) +
						 std::string{kMiddleDotSeparator} + change.Commit.Id.ShortHex();
			entry.Byline = author + std::string{kMiddleDotSeparator} +
						   FormattedTimestamp(change.Commit.Author.When, input.Zone);
			entry.Commit = change.Commit.Id;
			entry.Path = change.Path;

			if (!withLines)
			{
				return entry;
			}

			for (std::size_t index = 0; index < change.Hunks.size(); ++index)
			{
				// The lines between two hunks are left out too.
				if (index > 0)
				{
					AppendOmitted(entry.Lines);
				}

				AppendTrimmed(entry.Lines, change.Hunks[index], change);
			}

			return entry;
		}

		std::string LineHeader(std::size_t line)
		{
			return "History of line " + std::to_string(line + 1);
		}

		void FillCounts(LinePeekContent& content)
		{
			content.Counts = Core::Counted(static_cast<long long>(content.Entries.size()), "change") +
							 std::string{kMiddleDotSeparator} + "follows renames";
		}

		// The entry the peek shows, as DrawLinePeek picks it: the selected one, or the first when that is past the end.
		std::size_t ShownEntry(std::size_t selected, std::size_t entryCount)
		{
			return selected < entryCount ? selected : 0;
		}
	}

	LinePeekContent BuildLinePeekContent(const LinePeekInput& input)
	{
		LinePeekContent content;

		// The counts and the renames note join it once the history is ready.
		content.Header = LineHeader(input.Line);

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
			content.State = PeekState::Failed;
			content.Error = "This line has no committed history.";
			return content;
		}

		const bool uncommitted = author.Kind == LineAuthorKind::Uncommitted;

		// A line added since HEAD has no history to wait for.
		if (uncommitted && !author.HeadLine.has_value())
		{
			content.Entries.push_back(UncommittedEntry(input, author, true));
			content.State = PeekState::Ready;
			FillCounts(content);
			return content;
		}

		if (input.History != nullptr && input.History->Error.has_value())
		{
			content.State = PeekState::Failed;
			content.Error = input.History->Error->Message;
			return content;
		}

		if (input.History == nullptr || input.History->History == nullptr)
		{
			return content;
		}

		const std::vector<Core::LineChange>& changes = input.History->History->Changes;
		const std::size_t firstCommitted = uncommitted ? 1 : 0;
		const std::size_t shown = ShownEntry(input.Selected, firstCommitted + changes.size());

		if (uncommitted)
		{
			content.Entries.push_back(UncommittedEntry(input, author, shown == 0));
		}

		for (std::size_t index = 0; index < changes.size(); ++index)
		{
			content.Entries.push_back(CommittedEntry(input, changes[index], shown == firstCommitted + index));
		}

		content.StopsEarly = !input.History->History->Complete;
		content.State = PeekState::Ready;
		FillCounts(content);
		return content;
	}

	const LinePeekContent& LinePeekCache::Get(const LinePeekSource& source, std::chrono::sys_seconds now,
											  const std::function<LinePeekContent()>& build)
	{
		const bool agesOld = now - m_BuiltAt >= kAgesRefresh || now < m_BuiltAt;

		if (m_Source != source || agesOld)
		{
			m_Content = build();
			m_Source = source;
			m_BuiltAt = now;
		}

		return m_Content;
	}
}
