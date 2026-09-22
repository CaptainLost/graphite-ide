#pragma once

#include "Core/Git/Status.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// Which entries of a WorkingTreeStatus belong under which heading, as indices into that
	// status rather than copies of it. Indices because the panel needs the whole FileStatus
	// to draw a row, and copying every entry into three vectors to answer a question about
	// grouping would be a second copy of the same data.
	//
	// A file can be in Staged and Unstaged at once — staged one way and edited again since
	// — which is the case that makes this worth a function rather than a filter written
	// inline three times.
	struct ChangeSections
	{
		std::vector<std::size_t> Conflicted;
		std::vector<std::size_t> Staged;
		std::vector<std::size_t> Unstaged;
	};

	// A conflicted entry goes under Conflicted and nowhere else: what its index and
	// work-tree flags say is not a description of two ordinary halves, and showing it three
	// times would suggest three separate things to act on.
	ChangeSections PartitionStatus(const Core::WorkingTreeStatus& status);

	// The single letter a row is marked with. One letter and not several: a row shows the
	// one fact a reader cannot get from the path, and a file that is both renamed and
	// modified is, to someone scanning a list, a rename.
	char ChangeLetter(Core::StatusFlags flags, bool untracked);

	// Where a selection lands after the status changed under it. `before` and `after` are
	// the paths one section drew last frame and this frame, in drawn order.
	//
	// Kept by path rather than by index because the status is rebuilt on every refresh and
	// its entries reorder; an index would quietly move the selection onto a different file
	// every time a save landed. When the selected path is gone, the row that took its
	// position is selected, so acting on a run of rows leaves the selection where the hand
	// already is. Nothing is returned when the section emptied, or when the selection was
	// not in it to begin with.
	std::optional<std::filesystem::path> NextSelection(
		const std::filesystem::path& selected,
		const std::vector<std::filesystem::path>& before,
		const std::vector<std::filesystem::path>& after);

	// The row a discard was asked about, as the few facts the decision turns on rather than
	// as the FileStatus it came from — so the decision can be made, and tested, without a
	// status, a model or a panel.
	struct DiscardSubject
	{
		// As the status names it: relative to the repository's working directory.
		std::filesystem::path Path;

		bool IsDirectory = false;
		bool IsUntracked = false;

		// The files git listed under a directory, git-relative, when the caller holds a
		// listing it is willing to stand behind. Absent for a file, and for a directory
		// nobody has opened — which is not the same as an empty listing, and is why this is
		// an optional rather than a vector that happens to be empty.
		std::optional<std::vector<std::filesystem::path>> Listing;
	};

	// What one discard would do and what it would cost, worked out before the question is
	// put to the reader.
	struct DiscardPlan
	{
		// The two outcomes a discard has, kept apart for the reason DiscardRequest keeps
		// them apart. Copied into a DiscardRequest by the caller; this header stays clear of
		// GitModel so the decision can be tested without one.
		std::vector<std::filesystem::path> Restore;
		std::vector<std::filesystem::path> Delete;

		// Every file that disappears from disk or is rewritten under it, git-relative: the
		// subject itself for a file, and the listing's entries for a directory. Empty for a
		// directory with no listing, because nothing on this side can honestly say what was
		// in it. The caller uses this to tell the editor which files a confirmed discard
		// touched — a directory's own path names none of them.
		std::vector<std::filesystem::path> Files;

		// How many files the dialog may claim are in the directory. Absent wherever there is
		// no honest number, which is every case but a directory with a listing.
		std::optional<std::size_t> FileCount;

		// The open documents with unsaved edits this would overwrite, as the caller spelled
		// them.
		std::vector<std::filesystem::path> Dirty;
	};

	// `openDirty` is the absolute path of every open document with unsaved changes.
	// `workingDirectory` is what the subject's git-relative path is relative to — the
	// repository's working directory, not the folder the user opened, which
	// Core::Repository::Open may have resolved upward from.
	//
	// A document counts as overwritten when its path IS the subject or LIES UNDER it. The
	// second half is what a directory discard turns on: exact equality can never match a
	// file under a directory, so a whole-folder delete — the case where the most is lost —
	// would be the one that warns about nothing.
	DiscardPlan PlanDiscard(const DiscardSubject& subject,
							const std::filesystem::path& workingDirectory,
							const std::vector<std::filesystem::path>& openDirty);

	// The sentence the discard question opens with. Named loss, not a general "are you
	// sure": what is about to go is different in each of these cases, and only the sentence
	// says which one the reader is looking at.
	std::string DiscardQuestionText(const DiscardSubject& subject, const DiscardPlan& plan);
}
