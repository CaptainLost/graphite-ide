#pragma once

#include "Git/History/FoldRow.h"
#include "Git/Lines/HeadInRead.h"
#include "Git/Lines/LinesOnScreen.h"

#include "Core/Git/Head.h"
#include "Core/Git/Lines/LineModel.h"
#include "Core/Git/Oid.h"

#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace Graphite {

	// What the Git window has selected and what its models say now, as the shown history reads it.
	// WARNING: Holds everything by reference or pointer; it is made for one question and must not outlive what it names.
	struct ShownHistoryInput
	{
		const LinesOnScreen& OnScreen;

		const std::optional<std::string>& SelectedRef; // HEAD's history when empty and no stash is selected
		const std::optional<Core::Oid>& SelectedStash;

		// A restored state still waits for the listing that holds its ref: the history names no reference.
		bool RestoreWaits = false;

		// The path box names a file, so the history is that file's, which follows the file alone.
		bool FileHistory = false;

		const Core::HeadState* Head = nullptr; // null while unread and after a failed read

		bool Reading = false;        // a line read is on its way
		bool ListingComing = false;  // the branch listing a read is made from is on its way
		bool ReadFailed = false;     // the last line read failed
		bool BranchesFailed = false; // the branches could not be listed, so no first read is coming

		// The branches whose line's newer commits the reader asked to see, by full name.
		const std::set<std::string>& NewerShown;
	};

	// The two tips whose distance the header counts for the shown reference.
	struct DistanceTips
	{
		Core::Oid From;
		Core::Oid To;
	};

	// Which history the Git window shows and how it stands against the line read on screen: the reference it names, the
	// line it is compared with, and whether it waits for a read before its first rows.
	class ShownHistory
	{
	public:
		explicit ShownHistory(const ShownHistoryInput& input);

		// The selected ref is a tag's.
		bool ShowsTag() const;

		// HEAD's history, or the checked-out branch's, while it is known which history this is.
		bool IsHeadHistory() const;

		// How HEAD's history stands against the read on screen.
		HeadInRead HeadPlacement() const;

		// The full name of the branch whose history is shown: the selected ref, or the checked-out branch for HEAD's.
		// Empty for a stash, a tag, a detached or unborn HEAD, while no read is held, and for HEAD's history while the read
		// names another branch checked out than HEAD does: none of them has a line to compare it with.
		std::string RefName() const;

		// How the shown reference's history shows its line: the reference alone for a stash, a detached HEAD and while no
		// read is held. The filters are not looked at.
		HistoryLine RefLine() const;

		// The line as the history draws it: none for a file's history.
		HistoryLine LineToShow() const;

		// A branch's or HEAD's history waits for the line read that holds it, and HEAD's for the one that names its branch
		// checked out; a stash, a tag and a file's history do not, and neither does a history whose read failed.
		bool WaitsForLines() const;

		// HEAD's or the checked-out branch's history of the whole tree, whose first row is Local changes.
		bool ShowsLocalChanges() const;

		// The two tips whose distance the header counts for `shownRef`: the branch's and its line's, an old copy's and its
		// base's, or a picked line's; none when the header counts nothing.
		std::optional<DistanceTips> DistanceTipsOf(std::string_view shownRef) const;

		// The shown branch's line when it is a candidate for a check of its contents; null for a pick, an old copy, a base,
		// a branch merged by history.
		const Core::BranchLine* ContentCandidate() const;

	private:
		ShownHistoryInput m_Input;
	};
}
