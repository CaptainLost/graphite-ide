#include "Git/CheckoutQuestion.h"

#include "Core/Git/Repository.h"

#include <algorithm>
#include <map>

namespace Graphite {

	namespace {

		bool Contains(const std::vector<std::filesystem::path>& paths, const std::filesystem::path& path)
		{
			return std::find(paths.begin(), paths.end(), path) != paths.end();
		}
	}

	bool NeedsCheckoutDialog(const Core::CheckoutPlan& plan, const std::vector<std::filesystem::path>& unsaved)
	{
		if (!plan.Overwritten.empty())
		{
			return true;
		}

		return std::any_of(unsaved.begin(), unsaved.end(),
		                   [&plan](const std::filesystem::path& path) { return Contains(plan.Updated, path); });
	}

	CheckoutQuestion BuildCheckoutQuestion(const std::string& targetLabel, const Core::CheckoutPlan& plan,
	                                       const Core::WorkingTreeStatus& status,
	                                       const std::vector<std::filesystem::path>& unsaved)
	{
		CheckoutQuestion question;
		question.Title = "Checking out " + targetLabel + " would overwrite your changes";

		// Keyed by path so a file that is both changed on disk and unsaved shows once.
		std::map<std::filesystem::path, CheckoutQuestionRow> changed;
		std::map<std::filesystem::path, CheckoutQuestionRow> untracked;

		for (const Core::FileStatus& entry : status.Entries)
		{
			CheckoutQuestionRow row;
			row.Path = entry.Path;
			row.Blocks = Contains(plan.Overwritten, entry.Path);

			(entry.IsUntracked ? untracked : changed).emplace(entry.Path, std::move(row));
		}

		for (const std::filesystem::path& path : unsaved)
		{
			CheckoutQuestionRow& row = changed.try_emplace(path).first->second;
			row.Path = path;
			row.Unsaved = true;
			row.Blocks = row.Blocks || Contains(plan.Overwritten, path);
		}

		for (auto& [path, row] : changed)
		{
			question.Changed.push_back(std::move(row));
		}

		for (auto& [path, row] : untracked)
		{
			question.UntrackedForced = question.UntrackedForced || row.Blocks;
			question.Untracked.push_back(std::move(row));
		}

		return question;
	}

	std::optional<std::string> BranchNameProblem(const std::string& name, const std::vector<Core::Branch>& branches)
	{
		if (name.empty())
		{
			return "Enter a name";
		}

		if (!Core::Repository::IsValidBranchName(name))
		{
			return "Not a valid branch name";
		}

		const bool taken = std::any_of(branches.begin(), branches.end(), [&name](const Core::Branch& branch)
		                               { return branch.Kind == Core::RefKind::Local && branch.Name == name; });
		if (taken)
		{
			return "A branch named " + name + " already exists";
		}

		return std::nullopt;
	}

	std::string CheckoutTargetLabel(const Core::CheckoutTarget& target)
	{
		if (target.Kind == Core::CheckoutKind::Commit)
		{
			return target.Commit.ShortHex();
		}

		return target.Name;
	}
}
