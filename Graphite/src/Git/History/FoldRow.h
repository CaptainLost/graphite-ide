#pragma once

#include "Git/Lines/BranchLines.h"
#include "Git/Lines/LineReader.h"

#include "Core/Git/Commit.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Requests.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace Graphite {

	// How the history of a reference shows the line the reference is compared with.
	enum class LineFold
	{
		// The reference alone: a base, an old copy, a branch compared with nothing or with no history in common.
		None,

		// The branch alone, its own commits first, the line's newer commits folded into one row.
		Folded,

		// The branch beside its line's newer commits, which are dimmed; the status line offers to fold them again.
		Shown,

		// As Shown, without that offer: a branch merged by history, or one with no commits of its own.
		Combined
	};

	struct HistoryLine
	{
		LineFold Fold = LineFold::None;

		std::string Branch;   // full name
		std::string Line;     // full name of what the branch is compared with
		std::string LineName; // as the header names it: "develop", "origin/main"

		// As the read has them.
		Core::Oid BranchTip;
		Core::Oid LineTip;

		friend bool operator==(const HistoryLine&, const HistoryLine&) = default;
	};

	// `newerShown`: the reader asked to see the line's newer commits beside this branch.
	HistoryLine HistoryLineOf(const LineRead& read, const BranchLines& lines, std::string_view shownRef, bool newerShown);

	// The log of a history whose line's newer commits are shown also walks from the line's tip.
	void WidenToLine(Core::LogRequest& log, const HistoryLine& line);

	// What is asked together with `log` so the history can tell its rows apart: the branch's own commits while the line
	// is folded, the line's newer commits while they are shown, whatever words narrow the log; nothing for a reference
	// alone, nor own commits for a log narrowed by words. It follows first parents as the log does, and looks at least
	// as far.
	std::optional<Core::LogRequest> MarkedQueryFor(const HistoryLine& line, const Core::LogRequest& log);

	struct FoldRow
	{
		// Index in the page of the first commit that is not the branch's own.
		std::size_t BeforeCommit = 0;

		std::string Text;
		std::string Action;
	};

	// WARNING: A folded line's fold row is drawn only with its count. Drawn with an ellipsis, it would go again when
	// the count lands as nothing newer and move every row under it, so the rows wait for the count with it. `behind`
	// is empty while it is measured; `log` is the history's log, whose words decide whether own commits are asked.
	bool RowsWaitForFoldCount(const HistoryLine& line, const Core::LogRequest& log, std::optional<std::size_t> behind);

	// WARNING: `own` must be the branch's log without what its line holds, asked with the page. An own list cut short
	// and a page that ends before the first commit that is not the branch's own give no fold row: it would stand in
	// the wrong place. `behind` is how many commits the line has that the branch lacks, empty while it is measured; a
	// line known to have none gives no fold row either.
	std::optional<FoldRow> FindFoldRow(const Core::LogPage& page, const Core::LogPage& own, const std::string& lineName,
									   std::optional<std::size_t> behind);

	// The commit a history opened on a branch starts on: its first own commit when `own` is given, its first commit
	// that is not one of the line's newer ones when `newer` is, and its first commit without either or when there is
	// no such commit.
	std::optional<Core::Oid> StartingCommit(const Core::LogPage& page, const std::unordered_set<Core::Oid>* own,
											const std::unordered_set<Core::Oid>* newer);
}
