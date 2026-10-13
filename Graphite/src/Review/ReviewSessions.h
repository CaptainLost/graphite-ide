#pragma once

#include "Review/ReviewTree.h"
#include "Review/ViewedMarks.h"

#include "Core/Git/Diff.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Requests.h"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// What a changed file's diff shows first.
	enum class ChangedFileView
	{
		AllChanges,
		SinceViewed
	};

	struct SessionSettings
	{
		bool IncludeUncommitted = false;
		ListShape Shape = ListShape::Tree;
		bool HideViewed = false;
		Core::WhitespaceInDiff Whitespace = Core::WhitespaceInDiff::Shown;
		ChangedFileView ChangedFiles = ChangedFileView::SinceViewed;

		friend bool operator==(const SessionSettings&, const SessionSettings&) = default;
	};

	// One branch under review: the base it was started against, how it is shown, and what was viewed.
	struct ReviewSession
	{
		std::string Base;
		SessionSettings Settings;
		BranchMarks Marks;

		friend bool operator==(const ReviewSession&, const ReviewSession&) = default;
	};

	// The open folder's review sessions, by branch full name. A session lasts until it is ended,
	// whether or not its tab is open.
	class ReviewSessions
	{
	public:
		using Sessions = std::map<std::string, ReviewSession>;

		const ReviewSession* Find(const std::string& branch) const;

		// A session already under way for the branch is kept as it is, base included.
		void Start(const std::string& branch, const std::string& base);
		void End(const std::string& branch);

		void SetSettings(const std::string& branch, const SessionSettings& settings);

		// A branch without a session has nothing viewed, and marking one of its files does nothing.
		ViewedState StateOf(const std::string& branch, ReviewGroup group, const Core::FileDiff& file) const;

		// The mark the file has in that group, kept even when it no longer matches the file.
		std::optional<ViewedMark> MarkOf(const std::string& branch, ReviewGroup group, const Core::FileDiff& file) const;

		// Replaces whatever mark the file had in that group.
		void Mark(const std::string& branch, ReviewGroup group, const Core::FileDiff& file);
		void Unmark(const std::string& branch, ReviewGroup group, const Core::FileDiff& file);

		// Only for a snapshot that was read whole. A branch is gone once two such snapshots in a row
		// do not list it: a read racing `git pack-refs` can leave one out of a single snapshot.
		void Prune(const std::vector<Core::Branch>& branches);

		// Moves the renamed branch's session, and points sessions reviewing against it at the new name.
		void Rename(const std::string& from, const std::string& to);

		const Sessions& GetAll() const { return m_Sessions; }
		void Replace(Sessions sessions);

		// Grows on every change, so whoever saves can tell there is something to write.
		std::uint64_t GetVersion() const { return m_Version; }

	private:
		Sessions m_Sessions;
		std::uint64_t m_Version = 0;
		std::set<std::string> m_MissingLastTime;
	};

	// The text of `.graphite/reviews.json`, version 2.
	std::string EncodeReviewSessions(const ReviewSessions::Sessions& sessions);

	enum class ReviewsText
	{
		// This build's version. A session without a base, or a mark with a bad id, is left out.
		Read,

		// Not JSON, or JSON without the version and the sessions the file holds.
		Malformed,

		// A version this build does not read, written by another build - version 1 among them.
		OtherVersion
	};

	struct DecodedSessions
	{
		ReviewSessions::Sessions Sessions;
		ReviewsText Text = ReviewsText::Read;
	};

	// No sessions for a text that is not Read. Of two marks for one file of one group, the later is kept.
	DecodedSessions DecodeReviewSessionsText(std::string_view json);
}
