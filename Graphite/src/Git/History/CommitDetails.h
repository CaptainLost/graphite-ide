#pragma once

#include "Git/Content/ContentMark.h"
#include "Git/Lines/LineReader.h"

#include "Core/Git/Commit.h"
#include "Core/Git/Diff.h"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	struct DiffStats
	{
		std::size_t Files = 0;
		std::size_t Added = 0;
		std::size_t Removed = 0;
	};

	DiffStats CountChanges(const Core::Diff& diff);

	// The stats line while the commit's diff is not in hand; a failure is said with its message, here rather than in the
	// window bar, so it stays with the commit it is about.
	std::string UncountedChangesText(bool diffFailed, std::string_view failure);

	// WARNING: The offset is the reader's, not the author's, because Core::Signature drops the author's. Seconds are dropped, not rounded.
	std::string FormattedTimestamp(std::chrono::sys_seconds instant, std::chrono::minutes offset);

	// In the zone's offset at the instant itself, not today's: a summer time read in winter keeps its summer offset. A
	// null zone reads as UTC.
	std::string FormattedTimestamp(std::chrono::sys_seconds instant, const std::chrono::time_zone* zone);

	std::string CommitMessageText(const Core::CommitSummary& commit);

	bool ShowsCommitter(const Core::CommitSummary& commit);

	// A base that holds a copy of the commit, or a squash of it, without holding the commit: "release" and "≈ copy".
	struct InBaseEquivalent
	{
		std::string Base; // as the model names it
		std::string Text; // "≈ copy" or "≈ in squash <short hash>"
	};

	// The bases that hold a commit, as the "In bases" line names them.
	struct InBases
	{
		std::vector<std::string> Holding; // short names in model order; a remote base keeps its remote

		// The bases that do not hold it by history but hold its copy or its squash, in model order.
		std::vector<InBaseEquivalent> Equivalents;

		// Some base may yet show an equivalent: a copy is still searched for there, or the shown branch's check that may
		// find this commit's squash has not answered. With nothing held or found the line reads "…", not "in no base yet";
		// otherwise a "…" ends the line after the chips.
		bool EquivalentsPending = false;
		bool Pending = false;             // the answer has not landed
		std::optional<std::string> Unknown; // the line read failed: its message, and no answer will come
	};

	// The line for a moment with no read: waiting while one is coming, unknown once it has failed or once the branches
	// it would be made of could not be listed. A failure stays said while a read is asked again after it.
	InBases InBasesWithoutRead(bool reading, const std::optional<Core::Error>& linesError,
							   const std::optional<Core::Error>& branchesError);

	// The line for a commit the read's graph does not hold, as a detached HEAD's own commit or a stash: in no base, since
	// a base's history is in the graph; waiting while a newer read may hold it, but not for one asked again after a failure.
	InBases InBasesOutsideRead(bool reading, const std::optional<Core::Error>& linesError);

	// How many of the "In bases" chips go on the line, which never wraps: as many, in order, as fit in `room` with `gap`
	// between them and, when some are left over, the words for the rest after them, at most `moreWidth` wide.
	std::size_t InBasesChipsThatFit(const std::vector<float>& chipWidths, float gap, float room, float moreWidth);

	// What follows the chips shown: "and 29 more", "30 bases" when none is shown, empty when all are.
	std::string InBasesMoreText(std::size_t shown, std::size_t total);

	// What is known of a base that does not hold the commit by history.
	struct BaseEquivalence
	{
		std::size_t Base = 0;                   // index into read.Model.Bases
		TwinMark Twin;                          // the search for a copy of the commit on the base
		std::optional<Core::Oid> SquashLanding; // the base's commit holding the squash of the shown branch this commit is an own commit of
		bool SquashPending = false;             // that branch's check has not answered yet
	};

	// What the shown branch's verdict says of a copy of one of the branch's own commits on its line, so that none is
	// searched for there: once the verdict names a squash, the squash names the change, and the copies its check found
	// come with it. Empty while the verdict names none, so a copy is searched for.
	std::optional<TwinMark> TwinFromShownSquash(const ContentMark& shown, const Core::Oid& commit);

	// `holding` is LinesModel::BasesHolding: one flag per base of the read, absent while it is asked. `equivalence` holds
	// the bases that do not hold the commit: a found copy reads "≈ copy" (before a squash, which would name only
	// the same change), a squash "≈ in squash <short hash>"; a search still running makes the line pending, one that
	// could not answer adds nothing.
	InBases DescribeInBases(const LineRead& read, const std::optional<std::vector<bool>>& holding,
							const std::vector<BaseEquivalence>& equivalence = {});

	// What the line says when nothing is held and nothing found: "…" while an equivalent is pending, else "in no base yet".
	std::string InBasesEmptyText(const InBases& bases);

	// The source sentence of the commit the details show, decided when the commit is selected and kept until another
	// is: a verdict landing later shows it at the next selection, not as a line added to the open pane.
	class KeptSourceSentence
	{
	public:
		const std::optional<std::string>& Keep(const Core::Oid& commit, const std::optional<std::string>& current);
		void Forget();

	private:
		std::optional<Core::Oid> m_For;
		std::optional<std::string> m_Text;
	};

	// Where a commit's change came from, as the details say under the "In bases" line: "Squash of feature/x." or "Copy of
	// 1a2b3c4 from feature/x."; empty when the branch is not in the read.
	std::optional<std::string> ContentSourceSentence(const LineRead& read, const ContentSource& source);
}
