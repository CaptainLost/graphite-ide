#include "Git/Work/CardText.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Ref.h"

#include <string>

namespace Graphite {

	namespace {

		constexpr const char* kEllipsis = "\xE2\x80\xA6";
		constexpr const char* kMiddleDot = "\xC2\xB7";
		constexpr const char* kUpArrow = "\xE2\x86\x91";
		constexpr const char* kDownArrow = "\xE2\x86\x93";

		const char* OperationText(Core::RepositoryOperation operation)
		{
			switch (operation)
			{
			case Core::RepositoryOperation::Rebase:
				return "rebase in progress";
			case Core::RepositoryOperation::Merge:
				return "merge in progress";
			case Core::RepositoryOperation::CherryPick:
				return "cherry-pick in progress";
			case Core::RepositoryOperation::Revert:
				return "revert in progress";
			case Core::RepositoryOperation::Bisect:
				return "bisect in progress";
			case Core::RepositoryOperation::ApplyMailbox:
				return "patch in progress";
			case Core::RepositoryOperation::None:
				break;
			}

			return nullptr;
		}

		void AddDistanceChips(const WorkCard& card, std::vector<std::string>& chips)
		{
			if (card.Kind == WorkCardKind::WorkingCopy)
			{
				chips.push_back(card.Uncommitted.has_value()
									? std::to_string(*card.Uncommitted) + (*card.Uncommitted == 1 ? " change" : " changes")
									: std::string("counting changes") + kEllipsis);

				if (card.DistanceKnown && card.Ahead > 0)
				{
					chips.push_back(std::string(kUpArrow) + std::to_string(card.Ahead) + " to push");
				}

				if (card.DistanceKnown && card.Behind > 0)
				{
					chips.push_back(std::string(kDownArrow) + std::to_string(card.Behind) + " to pull");
				}

				return;
			}

			if (card.DistanceKnown)
			{
				chips.push_back(std::to_string(card.Ahead) + " ahead " + kMiddleDot + " " + std::to_string(card.Behind) +
								" behind");
			}
		}
	}

	std::vector<std::string> CardChips(const WorkCard& card, std::string_view trunkName)
	{
		std::vector<std::string> chips;
		const std::string trunk(trunkName);

		AddDistanceChips(card, chips);

		// The working copy already counts its changes in the distance chip.
		if (card.Kind != WorkCardKind::WorkingCopy && card.Worktree.has_value())
		{
			if (!card.Uncommitted.has_value())
			{
				chips.push_back(std::string("counting changes") + kEllipsis);
			}
			else if (*card.Uncommitted > 0)
			{
				chips.push_back(std::to_string(*card.Uncommitted) + " uncommitted");
			}
		}

		if (card.InTrunk > 0)
		{
			chips.push_back(std::to_string(card.InTrunk) + " of " + std::to_string(card.OwnChanges) + " already in " +
							trunk);
		}

		// Only a branch's commits are matched against the trunk's.
		if (card.Kind == WorkCardKind::Branch && !card.Checked)
		{
			chips.push_back("not checked against " + trunk);
		}

		if (card.Locked)
		{
			chips.push_back("locked");
		}

		if (card.DetachedAt.has_value())
		{
			chips.push_back("detached at " + card.DetachedAt->ShortHex());
		}

		if (const char* const operation = OperationText(card.Operation); operation != nullptr)
		{
			chips.emplace_back(operation);
		}

		return chips;
	}

	std::string WorktreeFolderName(const std::filesystem::path& worktree)
	{
		const std::filesystem::path name = worktree.has_filename() ? worktree.filename() : worktree.parent_path().filename();
		return Core::ToUtf8(name);
	}

	std::string CardBranchLine(const WorkCard& card)
	{
		std::string line = card.Branch.empty() ? std::string() : Core::ShortBranchName(card.Branch);

		if (card.Worktree.has_value())
		{
			const std::string folder = WorktreeFolderName(*card.Worktree);

			if (!folder.empty())
			{
				line += (line.empty() ? "" : std::string(" ") + kMiddleDot + " ") + folder;
			}
		}

		return line;
	}

	std::string ReviewProgressText(const std::optional<ViewedCount>& viewed)
	{
		if (!viewed.has_value())
		{
			return std::string("Reviewed ") + kEllipsis;
		}

		return "Reviewed " + std::to_string(viewed->Viewed) + "/" + std::to_string(viewed->Total);
	}
}
