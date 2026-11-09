#include "Git/GitDecorations.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <optional>

namespace Graphite {

	namespace {
		using DecorationsByKey = std::unordered_map<Core::PathKeyText, GitDecoration>;

		bool HasAnyStatusFlag(Core::StatusFlags flags, Core::StatusFlags wanted)
		{
			return (flags & wanted) != Core::StatusFlags::None;
		}

		// WARNING: "." is what lexically_normal makes of a path that cancels out; it names the
		// working directory, as the empty path does.
		Core::PathKeyText DecorationKeyOf(const std::filesystem::path& relative)
		{
			if (relative.empty())
			{
				return {};
			}

			Core::PathKeyText key = Core::PathKey(relative);
			const Core::PathKeyText dotKey = std::filesystem::path(".").native();

			if (key == dotKey)
			{
				key.clear();
			}

			return key;
		}

		void Raise(DecorationsByKey& decorationsByKey, const Core::PathKeyText& key, GitDecoration decoration)
		{
			if (!decorationsByKey.contains(key))
			{
				decorationsByKey.emplace(key, decoration);
				return;
			}

			GitDecoration& existingDecoration = decorationsByKey.at(key);
			existingDecoration = std::max(existingDecoration, decoration);
		}

		void RaiseAncestors(DecorationsByKey& folders, const std::filesystem::path& path, GitDecoration weight)
		{
			std::filesystem::path ancestor = path;

			// WARNING: Tested on has_relative_path() so a rooted path cannot make this loop forever.
			while (ancestor.has_relative_path())
			{
				ancestor = ancestor.parent_path();
				Raise(folders, DecorationKeyOf(ancestor), weight);
			}
		}

		std::optional<GitDecoration> FindDecoration(const DecorationsByKey& decorationsByKey,
													const Core::PathKeyText& key)
		{
			const DecorationsByKey::const_iterator found = decorationsByKey.find(key);

			if (found == decorationsByKey.end())
			{
				return std::nullopt;
			}

			return found->second;
		}
	}

	char LetterOf(GitDecoration decoration)
	{
		switch (decoration)
		{
		case GitDecoration::Untracked:
			return 'U';
		case GitDecoration::Added:
			return 'A';
		case GitDecoration::Renamed:
			return 'R';
		case GitDecoration::Modified:
			return 'M';
		case GitDecoration::Conflicted:
			return '!';
		case GitDecoration::None:
			break;
		}

		return '\0';
	}

	GitColor GitColorOf(GitDecoration decoration)
	{
		switch (decoration)
		{
		case GitDecoration::Untracked:
		case GitDecoration::Added:
			return GitColor::GutterAdded;
		case GitDecoration::Conflicted:
			return GitColor::GutterRemoved;
		case GitDecoration::Renamed:
		case GitDecoration::Modified:
		case GitDecoration::None:
			break;
		}

		return GitColor::GutterModified;
	}

	GitDecoration DecorationOf(const Core::FileStatus& entry)
	{
		if (entry.IsConflicted)
		{
			return GitDecoration::Conflicted;
		}

		if (entry.IsUntracked)
		{
			return GitDecoration::Untracked;
		}

		const Core::StatusFlags indexAndWorkTreeFlags = entry.Index | entry.WorkTree;
		const Core::StatusFlags editFlags = Core::StatusFlags::Modified | Core::StatusFlags::TypeChanged;

		if (HasAnyStatusFlag(indexAndWorkTreeFlags, editFlags))
		{
			return GitDecoration::Modified;
		}

		if (HasAnyStatusFlag(indexAndWorkTreeFlags, Core::StatusFlags::Renamed))
		{
			return GitDecoration::Renamed;
		}

		if (HasAnyStatusFlag(indexAndWorkTreeFlags, Core::StatusFlags::New))
		{
			return GitDecoration::Added;
		}

		return GitDecoration::None;
	}

	GitDecoration FolderWeightOf(const Core::FileStatus& entry)
	{
		const GitDecoration ownDecoration = DecorationOf(entry);
		const Core::StatusFlags indexAndWorkTreeFlags = entry.Index | entry.WorkTree;

		if (ownDecoration == GitDecoration::None &&
			HasAnyStatusFlag(indexAndWorkTreeFlags, Core::StatusFlags::Deleted))
		{
			return GitDecoration::Modified;
		}

		return ownDecoration;
	}

	GitDecorations::GitDecorations(const Core::WorkingTreeStatus& status)
	{
		for (const Core::FileStatus& entry : status.Entries)
		{
			const GitDecoration ownDecoration = DecorationOf(entry);
			const GitDecoration folderWeight = FolderWeightOf(entry);

			if (entry.IsUntracked && entry.IsDirectory && entry.HoldsCheckout)
			{
				m_NestedCheckouts.insert(DecorationKeyOf(entry.Path));
				continue;
			}

			if (entry.IsUntracked && entry.IsDirectory)
			{
				const Core::PathKeyText key = DecorationKeyOf(entry.Path);
				m_UntrackedFolders.insert(key);
				Raise(m_Folders, key, GitDecoration::Untracked);
			}
			else if (ownDecoration != GitDecoration::None)
			{
				Raise(m_Files, DecorationKeyOf(entry.Path), ownDecoration);
			}

			if (folderWeight == GitDecoration::None)
			{
				continue;
			}

			RaiseAncestors(m_Folders, entry.Path, folderWeight);

			if (entry.RenamedFrom)
			{
				RaiseAncestors(m_Folders, *entry.RenamedFrom, folderWeight);
			}
		}
	}

	// WARNING: Each ancestor is looked up whole, so "build/genx.txt" is never taken for being
	// inside "build/gen".
	bool GitDecorations::IsUntrackedFolder(const std::filesystem::path& relative) const
	{
		if (m_UntrackedFolders.empty() || IsInNestedCheckout(relative))
		{
			return false;
		}

		std::filesystem::path current = relative;

		while (current.has_relative_path())
		{
			if (m_UntrackedFolders.contains(DecorationKeyOf(current)))
			{
				return true;
			}

			current = current.parent_path();
		}

		return false;
	}

	bool GitDecorations::IsInNestedCheckout(const std::filesystem::path& relative) const
	{
		if (m_NestedCheckouts.empty())
		{
			return false;
		}

		std::filesystem::path current = relative;

		while (current.has_relative_path())
		{
			if (m_NestedCheckouts.contains(DecorationKeyOf(current)))
			{
				return true;
			}

			current = current.parent_path();
		}

		return false;
	}

	GitDecoration GitDecorations::OwnFile(const std::filesystem::path& relative) const
	{
		if (m_Files.empty() || IsInNestedCheckout(relative))
		{
			return GitDecoration::None;
		}

		return FindDecoration(m_Files, DecorationKeyOf(relative)).value_or(GitDecoration::None);
	}

	// WARNING: Runs for every visible row on every frame: a clean tree builds no key, and
	// IsUntrackedFolder walks no ancestors while nothing is untracked.
	GitDecoration GitDecorations::File(const std::filesystem::path& relative) const
	{
		if (IsEmpty() || IsInNestedCheckout(relative))
		{
			return GitDecoration::None;
		}

		const std::optional<GitDecoration> found = FindDecoration(m_Files, DecorationKeyOf(relative));

		if (found)
		{
			return *found;
		}

		return IsUntrackedFolder(relative) ? GitDecoration::Untracked : GitDecoration::None;
	}

	GitDecoration GitDecorations::Folder(const std::filesystem::path& relative) const
	{
		if (IsEmpty() || IsInNestedCheckout(relative))
		{
			return GitDecoration::None;
		}

		const std::optional<GitDecoration> found = FindDecoration(m_Folders, DecorationKeyOf(relative));

		if (found)
		{
			return *found;
		}

		return IsUntrackedFolder(relative) ? GitDecoration::Untracked : GitDecoration::None;
	}

	bool GitDecorations::IsEmpty() const
	{
		return m_Files.empty() && m_Folders.empty();
	}
}
