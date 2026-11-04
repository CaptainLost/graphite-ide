#include "Git/History/FoldRow.h"

#include "Git/Compare/VsLabel.h"

#include "Core/Base/Count.h"

#include <algorithm>

namespace Graphite {

	namespace {

		// WARNING: Bounds the marked commits looked up, not the commits shown. A branch with more own commits than
		// this, and further than the page reaches, gets no fold row, because the row would stand in the wrong place.
		constexpr std::size_t kMostMarkedCommitsLooked = 5000;

		constexpr const char* kPending = "\xE2\x80\xA6";
		constexpr const char* kShowThem = "Show them";

		std::optional<LineFold> FoldOfState(Core::BranchState state, bool newerShown)
		{
			switch (state)
			{
			case Core::BranchState::NotMerged:
				return newerShown ? LineFold::Shown : LineFold::Folded;
			case Core::BranchState::MergedByHistory:
			case Core::BranchState::NoOwnCommits:
				return LineFold::Combined;
			case Core::BranchState::NotCompared:
			case Core::BranchState::NoCommonHistory:
			case Core::BranchState::HistoryCutOff:
			case Core::BranchState::TipMissing:
				return std::nullopt;
			}

			return std::nullopt;
		}

		bool ShowsLineBeside(LineFold fold)
		{
			return fold == LineFold::Shown || fold == LineFold::Combined;
		}

		std::string NewerCommitsText(std::optional<std::size_t> behind, const std::string& lineName)
		{
			const std::string count = behind.has_value() ? Core::FormatCount(*behind) : std::string{kPending};
			const char* const noun = behind == std::optional<std::size_t>{1} ? " newer commit on " : " newer commits on ";

			return count + noun + lineName + " since this branch forked";
		}
	}

	HistoryLine HistoryLineOf(const LineRead& read, const BranchLines& lines, std::string_view shownRef, bool newerShown)
	{
		// An old copy and a base are compared with nothing, so they are shown alone.
		if (lines.OldCopyNamed(shownRef) != nullptr || lines.BaseNamed(shownRef) != nullptr)
		{
			return {};
		}

		const Core::BranchLine* const branch = lines.LineOf(shownRef);

		if (branch == nullptr || !branch->ComparedWith.has_value() || branch->Kind == Core::ComparedKind::None)
		{
			return {};
		}

		const std::optional<LineFold> fold = FoldOfState(branch->State, newerShown);

		if (!fold.has_value())
		{
			return {};
		}

		const Core::LineRef& line = read.Refs[*branch->ComparedWith];

		return HistoryLine{.Fold = *fold,
						   .Branch = std::string{shownRef},
						   .Line = line.FullName,
						   .LineName = LineNameOf(read, *branch->ComparedWith),
						   .BranchTip = read.Refs[branch->Ref].Tip,
						   .LineTip = line.Tip};
	}

	void WidenToLine(Core::LogRequest& log, const HistoryLine& line)
	{
		if (ShowsLineBeside(line.Fold))
		{
			log.AlsoFrom.push_back(line.LineTip);
		}
	}

	std::optional<Core::LogRequest> MarkedQueryFor(const HistoryLine& line, const Core::LogRequest& log)
	{
		if (line.Fold == LineFold::None)
		{
			return std::nullopt;
		}

		Core::LogRequest marked;
		marked.FirstParentOnly = log.FirstParentOnly;
		marked.MaxCount = std::max(kMostMarkedCommitsLooked, log.MaxCount);
		marked.MaxWalk = std::max(Core::kLogWalkLimit, log.MaxWalk);

		if (ShowsLineBeside(line.Fold))
		{
			marked.Ref = line.Line;
			marked.Exclude = line.BranchTip;
		}
		else
		{
			marked.Ref = line.Branch;
			marked.Exclude = line.LineTip;
		}

		return marked;
	}

	std::optional<FoldRow> FindFoldRow(const Core::LogPage& page, const Core::LogPage& own, const std::string& lineName,
									   std::optional<std::size_t> behind)
	{
		const bool nothingNewer = behind == std::optional<std::size_t>{0};

		if (!own.Complete || nothingNewer)
		{
			return std::nullopt;
		}

		std::unordered_set<Core::Oid> owned;
		owned.reserve(own.Commits.size());

		for (const Core::CommitSummary& commit : own.Commits)
		{
			owned.insert(commit.Id);
		}

		for (std::size_t commitIndex = 0; commitIndex < page.Commits.size(); ++commitIndex)
		{
			if (owned.contains(page.Commits[commitIndex].Id))
			{
				continue;
			}

			return FoldRow{.BeforeCommit = commitIndex, .Text = NewerCommitsText(behind, lineName), .Action = kShowThem};
		}

		return std::nullopt;
	}

	std::optional<Core::Oid> StartingCommit(const Core::LogPage& page, const std::unordered_set<Core::Oid>* own,
											const std::unordered_set<Core::Oid>* newer)
	{
		if (page.Commits.empty())
		{
			return std::nullopt;
		}

		for (const Core::CommitSummary& commit : page.Commits)
		{
			const bool isOwn = own != nullptr && own->contains(commit.Id);
			const bool isOnlyOnTheLine = newer != nullptr && newer->contains(commit.Id);
			const bool startsHere = (own != nullptr && isOwn) || (newer != nullptr && !isOnlyOnTheLine);

			if (startsHere)
			{
				return commit.Id;
			}
		}

		return page.Commits.front().Id;
	}
}
