#include "Git/CheckoutQuestion.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Repository.h"

#include <map>
#include <set>

namespace Graphite {

	namespace {

		using RowsByKey = std::map<std::string, CheckoutQuestionRow>;

		// WARNING: Folds ASCII only, so every byte of a multi-byte character is left alone.
		std::string ComparablePathKey(const std::filesystem::path& path)
		{
			std::string key = Core::ToGenericUtf8(path.lexically_normal());

			while (!key.empty() && key.back() == '/')
			{
				key.pop_back();
			}

			return Core::Lowered(key);
		}

		std::set<std::string> ComparablePathKeys(const std::vector<std::filesystem::path>& paths)
		{
			std::set<std::string> keys;

			for (const std::filesystem::path& path : paths)
			{
				keys.insert(ComparablePathKey(path));
			}

			return keys;
		}

		bool AnyKeyIsUnderDirectory(const std::set<std::string>& keys, const std::string& directoryKey)
		{
			const std::string directoryPrefix = directoryKey + "/";

			for (const std::string& key : keys)
			{
				if (key.starts_with(directoryPrefix))
				{
					return true;
				}
			}

			return false;
		}

		std::optional<std::filesystem::path> GitSpellingOf(const std::vector<std::filesystem::path>& names,
														   const std::string& key)
		{
			for (const std::filesystem::path& name : names)
			{
				if (ComparablePathKey(name) == key)
				{
					return name;
				}
			}

			return std::nullopt;
		}

		bool ContainsSamePath(const std::vector<std::filesystem::path>& paths,
							  const std::filesystem::path& normalisedPath)
		{
			for (const std::filesystem::path& path : paths)
			{
				if (Core::SamePath(Core::NormalizePath(path), normalisedPath))
				{
					return true;
				}
			}

			return false;
		}

		bool HasLocalBranchNamed(const std::vector<Core::Branch>& branches, const std::string& name)
		{
			for (const Core::Branch& branch : branches)
			{
				if (branch.Kind == Core::RefKind::Local && branch.Name == name)
				{
					return true;
				}
			}

			return false;
		}
	}

	std::string CheckoutRowTag(const CheckoutQuestionRow& row)
	{
		if (row.Unsaved && row.Blocks)
		{
			return "unsaved, blocks checkout";
		}

		if (row.Blocks)
		{
			return "blocks checkout";
		}

		return row.Unsaved ? "unsaved" : "";
	}

	bool NeedsCheckoutDialog(const Core::CheckoutPlan& plan, const std::vector<std::filesystem::path>& unsaved)
	{
		if (!plan.Overwritten.empty())
		{
			return true;
		}

		const std::set<std::string> updatedKeys = ComparablePathKeys(plan.Updated);

		for (const std::filesystem::path& unsavedPath : unsaved)
		{
			if (updatedKeys.contains(ComparablePathKey(unsavedPath)))
			{
				return true;
			}
		}

		return false;
	}

	CheckoutQuestion BuildCheckoutQuestion(const std::string& targetLabel, const Core::CheckoutPlan& plan,
										   const Core::WorkingTreeStatus& status,
										   const std::vector<std::filesystem::path>& unsaved)
	{
		CheckoutQuestion question;
		question.Title = "Checking out " + targetLabel + " would overwrite your changes";

		const std::set<std::string> overwrittenKeys = ComparablePathKeys(plan.Overwritten);

		RowsByKey changedRowsByKey;
		RowsByKey untrackedRowsByKey;

		for (const Core::FileStatus& entry : status.Entries)
		{
			const std::string key = ComparablePathKey(entry.Path);

			CheckoutQuestionRow row;
			row.Path = entry.Path;

			const bool isOverwritten = overwrittenKeys.contains(key);
			const bool isCollapsedUntrackedDirectory = entry.IsUntracked && entry.IsDirectory;
			row.Blocks = isOverwritten || (isCollapsedUntrackedDirectory && AnyKeyIsUnderDirectory(overwrittenKeys, key));

			RowsByKey& rowsOfSection = entry.IsUntracked ? untrackedRowsByKey : changedRowsByKey;
			rowsOfSection.emplace(key, std::move(row));
		}

		for (const std::filesystem::path& unsavedPath : unsaved)
		{
			const std::string key = ComparablePathKey(unsavedPath);

			RowsByKey::iterator untrackedRow = untrackedRowsByKey.find(key);

			if (untrackedRow != untrackedRowsByKey.end())
			{
				auto& [untrackedKey, untrackedQuestionRow] = *untrackedRow;
				untrackedQuestionRow.Unsaved = true;
				continue;
			}

			auto [changedRow, added] = changedRowsByKey.try_emplace(key);
			auto& [changedKey, row] = *changedRow;

			if (added)
			{
				const std::optional<std::filesystem::path> spellingInOverwritten = GitSpellingOf(plan.Overwritten, key);
				const std::optional<std::filesystem::path> spellingInUpdated = GitSpellingOf(plan.Updated, key);

				if (spellingInOverwritten.has_value())
				{
					row.Path = *spellingInOverwritten;
				}
				else if (spellingInUpdated.has_value())
				{
					row.Path = *spellingInUpdated;
				}
				else
				{
					row.Path = unsavedPath;
				}

				row.Blocks = overwrittenKeys.contains(key);
			}

			row.Unsaved = true;
		}

		for (auto& [key, row] : changedRowsByKey)
		{
			question.Changed.push_back(std::move(row));
		}

		for (auto& [key, row] : untrackedRowsByKey)
		{
			question.UntrackedForced = question.UntrackedForced || row.Blocks;
			question.Untracked.push_back(std::move(row));
		}

		return question;
	}

	std::vector<std::filesystem::path> SilentReloads(const std::vector<std::filesystem::path>& changed,
													 const std::vector<std::filesystem::path>& unsavedNow)
	{
		std::vector<std::filesystem::path> silent;

		for (const std::filesystem::path& path : changed)
		{
			const std::filesystem::path normalisedPath = Core::NormalizePath(path);

			if (!ContainsSamePath(unsavedNow, normalisedPath))
			{
				silent.push_back(path);
			}
		}

		return silent;
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

		if (HasLocalBranchNamed(branches, name))
		{
			return "A branch named " + name + " already exists";
		}

		return std::nullopt;
	}

	bool AsksBeforeDetaching(const Core::CheckoutTarget& target)
	{
		return target.Kind == Core::CheckoutKind::Commit;
	}

	std::string DetachedCheckoutBody(const std::string& commitLabel)
	{
		return "Checking out " + commitLabel +
			   " leaves HEAD on that commit with no branch. Commits made there belong to no branch, "
			   "and are easy to lose once you switch away.";
	}

	std::string CheckoutTargetLabel(const Core::CheckoutTarget& target)
	{
		if (target.Kind == Core::CheckoutKind::Commit)
		{
			return target.Commit.ShortHex();
		}

		return target.Name;
	}

	CheckoutPlanVerdict DecideOnCheckoutPlan(const LandedCheckoutPlan& plan)
	{
		if (plan.Failed)
		{
			return CheckoutPlanVerdict::ReportFailure;
		}

		if (!plan.NeedsDialog)
		{
			return CheckoutPlanVerdict::CheckOut;
		}

		return plan.PanelOnScreen ? CheckoutPlanVerdict::Ask : CheckoutPlanVerdict::HoldForPanel;
	}

	std::optional<Core::CheckoutTarget> HeldCheckout::TakeOnceOnScreen(bool panelOnScreen)
	{
		if (!panelOnScreen || !m_Target.has_value())
		{
			return std::nullopt;
		}

		std::optional<Core::CheckoutTarget> taken = std::move(m_Target);
		m_Target.reset();
		return taken;
	}
}