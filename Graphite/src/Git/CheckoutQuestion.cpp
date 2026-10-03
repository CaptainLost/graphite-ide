#include "Git/CheckoutQuestion.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Git/Repository.h"

#include <algorithm>
#include <map>
#include <set>

namespace Graphite {

	namespace {

		// Editor paths and git paths name the same file in different spellings: separators,
		// and on Windows the case. Every comparison goes through this key. UTF-8 rather than
		// generic_string(), which narrows through the ANSI code page and throws on a name
		// outside it; folding ASCII only leaves every byte of a multi-byte character alone.
		std::string Key(const std::filesystem::path& path)
		{
			std::string key = Core::ToGenericUtf8(path.lexically_normal());
			while (!key.empty() && key.back() == '/')
			{
				key.pop_back();
			}

			for (char& c : key)
			{
				if (c >= 'A' && c <= 'Z')
				{
					c = static_cast<char>(c - 'A' + 'a');
				}
			}

			return key;
		}

		std::set<std::string> Keys(const std::vector<std::filesystem::path>& paths)
		{
			std::set<std::string> keys;
			for (const std::filesystem::path& path : paths)
			{
				keys.insert(Key(path));
			}

			return keys;
		}

		// A collapsed untracked directory is one status entry; the plan names the files in it.
		// The prefix ends in a separator so "dir2/x" is not taken for a file of "dir".
		bool IsUnder(const std::set<std::string>& keys, const std::string& directory)
		{
			const std::string prefix = directory + "/";
			return std::any_of(keys.begin(), keys.end(), [&prefix](const std::string& key) { return key.compare(0, prefix.size(), prefix) == 0; });
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

		const std::set<std::string> updated = Keys(plan.Updated);
		return std::any_of(unsaved.begin(), unsaved.end(),
						   [&updated](const std::filesystem::path& path) { return updated.count(Key(path)) != 0; });
	}

	CheckoutQuestion BuildCheckoutQuestion(const std::string& targetLabel, const Core::CheckoutPlan& plan,
										   const Core::WorkingTreeStatus& status,
										   const std::vector<std::filesystem::path>& unsaved)
	{
		CheckoutQuestion question;
		question.Title = "Checking out " + targetLabel + " would overwrite your changes";

		const std::set<std::string> overwritten = Keys(plan.Overwritten);

		// Keyed so a file that is both changed on disk and unsaved shows once, and so the
		// rows come out sorted.
		std::map<std::string, CheckoutQuestionRow> changed;
		std::map<std::string, CheckoutQuestionRow> untracked;

		for (const Core::FileStatus& entry : status.Entries)
		{
			const std::string key = Key(entry.Path);

			CheckoutQuestionRow row;
			row.Path = entry.Path;
			row.Blocks = overwritten.count(key) != 0 || (entry.IsUntracked && entry.IsDirectory && IsUnder(overwritten, key));

			(entry.IsUntracked ? untracked : changed).emplace(key, std::move(row));
		}

		for (const std::filesystem::path& path : unsaved)
		{
			const std::string key = Key(path);

			// Untracked and unsaved is one file: mark its row rather than list it twice.
			if (auto found = untracked.find(key); found != untracked.end())
			{
				found->second.Unsaved = true;
				continue;
			}

			auto [slot, added] = changed.try_emplace(key);
			CheckoutQuestionRow& row = slot->second;
			if (added)
			{
				// Show git's spelling of the path when the plan has one.
				row.Path = path;
				for (const auto* names : {&plan.Overwritten, &plan.Updated})
				{
					const auto named = std::find_if(names->begin(), names->end(),
													[&key](const std::filesystem::path& name) { return Key(name) == key; });
					if (named != names->end())
					{
						row.Path = *named;
						break;
					}
				}
				row.Blocks = overwritten.count(key) != 0;
			}

			row.Unsaved = true;
		}

		for (auto& [key, row] : changed)
		{
			question.Changed.push_back(std::move(row));
		}

		for (auto& [key, row] : untracked)
		{
			question.UntrackedForced = question.UntrackedForced || row.Blocks;
			question.Untracked.push_back(std::move(row));
		}

		return question;
	}

	std::vector<std::filesystem::path> SilentReloads(const std::vector<std::filesystem::path>& changed,
													 const std::vector<std::filesystem::path>& unsavedNow)
	{
		const auto unsaved = [&unsavedNow](const std::filesystem::path& path) {
			const std::filesystem::path normal = Core::NormalizePath(path);
			return std::any_of(unsavedNow.begin(), unsavedNow.end(), [&normal](const std::filesystem::path& other) { return Core::SamePath(Core::NormalizePath(other), normal); });
		};

		std::vector<std::filesystem::path> silent;
		for (const std::filesystem::path& path : changed)
		{
			if (!unsaved(path))
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

		const bool taken = std::any_of(branches.begin(), branches.end(), [&name](const Core::Branch& branch) { return branch.Kind == Core::RefKind::Local && branch.Name == name; });
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
