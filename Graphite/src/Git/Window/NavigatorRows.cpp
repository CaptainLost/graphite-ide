#include "Git/Window/NavigatorRows.h"

#include "Git/Work/CardText.h"

#include <algorithm>

namespace Graphite {

	namespace {

		constexpr const char* kWorkLabel = "Work";
		constexpr const char* kShowAllLabel = "Show all";
		constexpr const char* kReadyLabel = "ready to remove";
		constexpr const char* kLockedLabel = "locked by another session";
		constexpr const char* kChangesLabel = "Changes";
		constexpr const char* kBranchesLabel = "Branches";

		std::string CountText(std::size_t count)
		{
			return std::to_string(count);
		}

		std::size_t LocalBranchCount(const std::vector<Core::Branch>& branches)
		{
			return static_cast<std::size_t>(std::ranges::count_if(
				branches, [](const Core::Branch& branch) { return branch.Kind == Core::RefKind::Local; }));
		}

		bool IsThread(const WorkCard& card)
		{
			return card.Kind != WorkCardKind::WorkingCopy;
		}
	}

	std::vector<std::size_t> NewestLocalBranches(const std::vector<Core::Branch>& branches, std::size_t count)
	{
		std::vector<std::size_t> indices;

		for (std::size_t index = 0; index < branches.size(); ++index)
		{
			if (branches[index].Kind == Core::RefKind::Local)
			{
				indices.push_back(index);
			}
		}

		const auto isNewer = [&](std::size_t left, std::size_t right)
		{
			const Core::Branch& a = branches[left];
			const Core::Branch& b = branches[right];

			if (a.IsHead != b.IsHead)
			{
				return a.IsHead;
			}

			if (a.TipCommit.has_value() != b.TipCommit.has_value())
			{
				return a.TipCommit.has_value();
			}

			return a.TipCommit.has_value() && a.TipCommit->Committer.When > b.TipCommit->Committer.When;
		};

		std::ranges::stable_sort(indices, isNewer);

		if (indices.size() > count)
		{
			indices.resize(count);
		}

		return indices;
	}

	std::string ThreadRowLabel(const WorkCard& card)
	{
		if (card.Worktree.has_value())
		{
			if (std::string folder = WorktreeFolderName(*card.Worktree); !folder.empty())
			{
				return folder;
			}
		}

		return Core::ShortBranchName(card.Branch);
	}

	GitWindowSelection SelectionOf(const NavigatorRow& row)
	{
		switch (row.Kind)
		{
		case NavigatorRowKind::Thread:
			if (row.Branch.empty())
			{
				return {GitWindowView::Work, {}};
			}

			return {GitWindowView::Thread, row.Branch};
		case NavigatorRowKind::Changes:
			return {GitWindowView::Changes, {}};
		case NavigatorRowKind::Branch:
			return {GitWindowView::History, row.Branch};
		case NavigatorRowKind::ShowAllBranches:
			return {GitWindowView::Branches, {}};
		case NavigatorRowKind::Work:
		case NavigatorRowKind::ShowAllWork:
		case NavigatorRowKind::Ready:
		case NavigatorRowKind::Locked:
		case NavigatorRowKind::BranchesHeading:
			return {GitWindowView::Work, {}};
		}

		return {};
	}

	std::vector<NavigatorRow> BuildNavigatorRows(const NavigatorInput& input)
	{
		std::vector<NavigatorRow> rows;
		const GitWindowSelection& selection = input.Selection;

		rows.push_back({.Kind = NavigatorRowKind::Work,
						.Label = kWorkLabel,
						.Selected = selection.View == GitWindowView::Work});

		if (input.Board != nullptr)
		{
			const WorkBoard& board = *input.Board;
			std::size_t threads = 0;

			for (std::size_t cardIndex = 0; cardIndex < board.Cards.size(); ++cardIndex)
			{
				const WorkCard& card = board.Cards[cardIndex];

				if (!IsThread(card))
				{
					continue;
				}

				++threads;

				if (threads > kNavigatorThreads)
				{
					continue;
				}

				const bool selected = selection.View == GitWindowView::Thread && !card.Branch.empty() &&
									  selection.Branch == card.Branch;

				rows.push_back({.Kind = NavigatorRowKind::Thread,
								.Label = ThreadRowLabel(card),
								.Index = cardIndex,
								.Branch = card.Kind == WorkCardKind::Branch ? card.Branch : std::string{},
								.State = card.State,
								.Selected = selected});
			}

			if (threads > kNavigatorThreads)
			{
				rows.push_back({.Kind = NavigatorRowKind::ShowAllWork, .Label = kShowAllLabel, .Count = CountText(threads)});
			}

			if (!board.Ready.empty())
			{
				rows.push_back({.Kind = NavigatorRowKind::Ready, .Label = kReadyLabel, .Count = CountText(board.Ready.size())});
			}

			const auto locked = static_cast<std::size_t>(std::ranges::count_if(
				board.Info, [](const WorkInfo& info) { return info.Kind == WorkInfoKind::Locked; }));

			if (locked > 0)
			{
				rows.push_back({.Kind = NavigatorRowKind::Locked, .Label = kLockedLabel, .Count = CountText(locked)});
			}
		}

		rows.push_back({.Kind = NavigatorRowKind::Changes,
						.Label = kChangesLabel,
						.Count = input.ChangeCount.has_value() ? CountText(*input.ChangeCount) : std::string{},
						.Selected = selection.View == GitWindowView::Changes});

		rows.push_back({.Kind = NavigatorRowKind::BranchesHeading, .Label = kBranchesLabel});

		if (input.Branches != nullptr)
		{
			const std::vector<Core::Branch>& branches = *input.Branches;

			for (const std::size_t index : NewestLocalBranches(branches, kNavigatorBranches))
			{
				const Core::Branch& branch = branches[index];

				// HEAD's history names no ref, and it is the history of HEAD's branch.
				const bool shown = selection.Branch == branch.FullName || (selection.Branch.empty() && branch.IsHead);

				rows.push_back({.Kind = NavigatorRowKind::Branch,
								.Label = branch.IsHead ? "* " + branch.Name : branch.Name,
								.Index = index,
								.Branch = branch.FullName,
								.Selected = selection.View == GitWindowView::History && shown});
			}

			rows.push_back({.Kind = NavigatorRowKind::ShowAllBranches,
							.Label = kShowAllLabel,
							.Count = CountText(LocalBranchCount(branches)),
							.Selected = selection.View == GitWindowView::Branches});
		}

		return rows;
	}
}
