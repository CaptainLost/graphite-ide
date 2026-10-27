#pragma once

#include "Review/ReviewPort.h"
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

	struct ReviewSession
	{
		std::string Base;
		SessionSettings Settings;
		BranchMarks Marks;

		// A draft holds the base and settings of a branch that is only looked at. It is not written to
		// reviews.json and shows no badge until a viewed mark keeps it.
		bool Draft = false;

		friend bool operator==(const ReviewSession&, const ReviewSession&) = default;
	};

	class ReviewSessions
	{
	public:
		using Sessions = std::map<std::string, ReviewSession>;

		const ReviewSession* Find(const std::string& branch) const;

		void Start(const std::string& branch, const std::string& base);
		void End(const std::string& branch);

		// A draft holds a branch's base and settings while the branch is only looked at. It opens with
		// IncludeUncommitted on. A branch that already has a draft or a session is left alone.
		void OpenDraft(const std::string& branch, const std::string& base);

		// Turns a draft into a kept session; anything else is left alone.
		void Keep(const std::string& branch);

		bool IsKept(const std::string& branch) const;

		void SetSettings(const std::string& branch, const SessionSettings& settings);

		ViewedState StateOf(const std::string& branch, ReviewGroup group, const Core::FileDiff& file) const;

		std::optional<ViewedMark> MarkOf(const std::string& branch, ReviewGroup group, const Core::FileDiff& file) const;

		void Mark(const std::string& branch, ReviewGroup group, const Core::FileDiff& file);
		void Unmark(const std::string& branch, ReviewGroup group, const Core::FileDiff& file);

		// WARNING: A read racing `git pack-refs` can leave a branch out of a single snapshot, so a branch is
		// gone only once two snapshots in a row do not list it.
		void Prune(const std::vector<Core::Branch>& branches);

		void Rename(const std::string& from, const std::string& to);

		const Sessions& GetAll() const { return m_Sessions; }
		void Replace(Sessions sessions);

		std::uint64_t GetVersion() const { return m_Version; }

	private:
		Sessions m_Sessions;
		std::uint64_t m_Version = 0;
		std::set<std::string> m_MissingLastTime;
	};

	std::optional<SinceViewedRequest> SinceViewedRequestFor(const ReviewSessions& sessions, const std::string& branch,
															ReviewGroup group, const Core::FileDiff& file,
															const ReviewRange& range, Core::WhitespaceInDiff whitespace);

	std::string EncodeReviewSessions(const ReviewSessions::Sessions& sessions);

	enum class ReviewsText
	{
		Read,

		Malformed,

		OtherVersion
	};

	struct DecodedSessions
	{
		ReviewSessions::Sessions Sessions;
		ReviewsText Text = ReviewsText::Read;
	};

	DecodedSessions DecodeReviewSessionsText(std::string_view json);
}
