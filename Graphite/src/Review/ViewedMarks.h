#pragma once

#include "Core/Git/Diff.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Ref.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
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

	// A file the reader looked at, as it was then: the pair of blob ids names that exact change.
	struct ViewedMark
	{
		ReviewGroup Group = ReviewGroup::Commits;
		std::filesystem::path Path;
		Core::Oid Old;
		Core::Oid New;

		friend bool operator==(const ViewedMark&, const ViewedMark&) = default;
	};

	// Which files of which branch's review the reader has looked at. A file is keyed by the path the
	// list shows it under: the new path, or the old one for a delete.
	class ViewedMarks
	{
	public:
		// By the branch's full name.
		using Marks = std::map<std::string, std::vector<ViewedMark>>;

		ViewedState StateOf(const std::string& branch, ReviewGroup group, const Core::FileDiff& file) const;

		// Replaces whatever mark the file had in that group.
		void Mark(const std::string& branch, ReviewGroup group, const Core::FileDiff& file);
		void Unmark(const std::string& branch, ReviewGroup group, const Core::FileDiff& file);

		// Only for a snapshot that was read whole: a branch it does not list is gone.
		void Prune(const std::vector<Core::Branch>& branches);

		void Rename(const std::string& from, const std::string& to);

		const Marks& GetAll() const { return m_Marks; }
		void Replace(Marks marks);

		// Grows on every change, so whoever saves can tell there is something to write.
		std::uint64_t GetVersion() const { return m_Version; }

	private:
		Marks m_Marks;
		std::uint64_t m_Version = 0;
	};

	// The text of `.graphite/reviews.json`.
	std::string EncodeViewedMarks(const ViewedMarks::Marks& marks);

	// Anything unreadable - text that is not JSON, another version, an entry with a bad id - is
	// left out, and the rest is kept.
	ViewedMarks::Marks DecodeViewedMarks(std::string_view json);
}
