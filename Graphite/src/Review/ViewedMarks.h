#pragma once

#include "Core/Git/Diff.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Ref.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// The two lists a review shows: what the branch's commits changed since the merge base, and
	// what its worktree changed on top of the tip.
	enum class ReviewGroup
	{
		Commits,
		Uncommitted
	};

	inline constexpr std::size_t kReviewGroupCount = 2;

	inline std::size_t IndexOf(ReviewGroup group)
	{
		return static_cast<std::size_t>(group);
	}

	enum class ViewedState
	{
		NotViewed,
		Viewed,
		ChangedSinceViewed
	};

	// Where a mark sits: one file of one group, by the path the list shows it under - the new path,
	// or the old one for a delete.
	struct MarkKey
	{
		ReviewGroup Group = ReviewGroup::Commits;
		std::filesystem::path Path;

		friend bool operator==(const MarkKey&, const MarkKey&) = default;
		friend std::strong_ordering operator<=>(const MarkKey&, const MarkKey&) = default;
	};

	// The file as the reader looked at it: the pair of blob ids names that exact change.
	struct ViewedMark
	{
		Core::Oid Old;
		Core::Oid New;

		friend bool operator==(const ViewedMark&, const ViewedMark&) = default;
	};

	// Which files of which branch's review the reader has looked at.
	class ViewedMarks
	{
	public:
		// Ordered by key, so a row finds its mark without walking the branch's other marks.
		using BranchMarks = std::map<MarkKey, ViewedMark>;

		// By the branch's full name.
		using Marks = std::map<std::string, BranchMarks>;

		ViewedState StateOf(const std::string& branch, ReviewGroup group, const Core::FileDiff& file) const;

		// Replaces whatever mark the file had in that group.
		void Mark(const std::string& branch, ReviewGroup group, const Core::FileDiff& file);
		void Unmark(const std::string& branch, ReviewGroup group, const Core::FileDiff& file);

		// Only for a snapshot that was read whole. A branch is gone once two such snapshots in a row
		// do not list it: a read racing `git pack-refs` can leave one out of a single snapshot.
		void Prune(const std::vector<Core::Branch>& branches);

		void Rename(const std::string& from, const std::string& to);

		const Marks& GetAll() const { return m_Marks; }
		void Replace(Marks marks);

		// Grows on every change, so whoever saves can tell there is something to write.
		std::uint64_t GetVersion() const { return m_Version; }

	private:
		Marks m_Marks;
		std::uint64_t m_Version = 0;

		// The branches holding marks that the last snapshot did not list.
		std::set<std::string> m_MissingLastTime;
	};

	// The text of `.graphite/reviews.json`.
	std::string EncodeViewedMarks(const ViewedMarks::Marks& marks);

	// What reading the text of `.graphite/reviews.json` found.
	enum class MarksText
	{
		// This build's version. An entry with a bad id is left out and the rest are kept.
		Read,

		// Not JSON, or JSON without the version and the branches the file holds.
		Malformed,

		// A version this build does not know, written by another build.
		OtherVersion
	};

	struct DecodedMarks
	{
		ViewedMarks::Marks Marks;
		MarksText Text = MarksText::Read;
	};

	// No marks for a text that is not Read. Of two entries for one file of one group, the later
	// one is kept.
	DecodedMarks DecodeViewedMarksText(std::string_view json);

	// The marks of DecodeViewedMarksText, for a caller that needs nothing else.
	ViewedMarks::Marks DecodeViewedMarks(std::string_view json);
}
