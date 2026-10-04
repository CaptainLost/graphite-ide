#include "Git/GitDecorations.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>

namespace Graphite {

	namespace {
		using Key = std::filesystem::path::string_type;

		bool Has(Core::StatusFlags flags, Core::StatusFlags wanted)
		{
			return (flags & wanted) != Core::StatusFlags::None;
		}

		// The one comparison form the codebase uses for paths. "." is what lexically_normal
		// makes of a path that cancels out; it names the working directory, as "" does.
		Key KeyOf(const std::filesystem::path& relative)
		{
			if (relative.empty())
			{
				return {};
			}

			Key key = Core::PathKey(relative);

			if (key == std::filesystem::path(".").native())
			{
				key.clear();
			}

			return key;
		}

		void Raise(std::unordered_map<Key, GitDecoration>& map, const Key& key, GitDecoration decoration)
		{
			auto [position, inserted] = map.try_emplace(key, decoration);

			if (!inserted)
			{
				position->second = std::max(position->second, decoration);
			}
		}
	}

	char LetterOf(GitDecoration decoration)
	{
		switch (decoration)
		{
		case GitDecoration::Untracked: return 'U';
		case GitDecoration::Added: return 'A';
		case GitDecoration::Renamed: return 'R';
		case GitDecoration::Modified: return 'M';
		case GitDecoration::Conflicted: return '!';
		case GitDecoration::None: break;
		}

		return '\0';
	}

	GitColor ColourOf(GitDecoration decoration)
	{
		switch (decoration)
		{
		case GitDecoration::Untracked:
		case GitDecoration::Added: return GitColor::GutterAdded;
		case GitDecoration::Conflicted: return GitColor::GutterRemoved;
		case GitDecoration::Renamed:
		case GitDecoration::Modified:
		case GitDecoration::None: break;
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

		const Core::StatusFlags both = entry.Index | entry.WorkTree;

		// A type change is an edit as far as a reader of the tree is concerned.
		if (Has(both, Core::StatusFlags::Modified | Core::StatusFlags::TypeChanged))
		{
			return GitDecoration::Modified;
		}

		if (Has(both, Core::StatusFlags::Renamed))
		{
			return GitDecoration::Renamed;
		}

		if (Has(both, Core::StatusFlags::New))
		{
			return GitDecoration::Added;
		}

		return GitDecoration::None;
	}

	GitDecoration FolderWeightOf(const Core::FileStatus& entry)
	{
		const GitDecoration own = DecorationOf(entry);

		if (own == GitDecoration::None && Has(entry.Index | entry.WorkTree, Core::StatusFlags::Deleted))
		{
			return GitDecoration::Modified;
		}

		return own;
	}

	GitDecorations::GitDecorations(const Core::WorkingTreeStatus& status)
	{
		for (const Core::FileStatus& entry : status.Entries)
		{
			const GitDecoration own = DecorationOf(entry);
			const GitDecoration weight = FolderWeightOf(entry);

			if (entry.IsUntracked && entry.IsDirectory)
			{
				const Key key = KeyOf(entry.Path);
				m_UntrackedFolders.insert(key);
				Raise(m_Folders, key, GitDecoration::Untracked);
			}
			else if (own != GitDecoration::None)
			{
				Raise(m_Files, KeyOf(entry.Path), own);
			}

			if (weight == GitDecoration::None)
			{
				continue;
			}

			// Up to and including the working directory, whose key is empty. Tested on
			// has_relative_path() so a rooted path cannot make this loop forever.
			std::filesystem::path parent = entry.Path;

			while (parent.has_relative_path())
			{
				parent = parent.parent_path();
				Raise(m_Folders, KeyOf(parent), weight);
			}
		}
	}

	bool GitDecorations::IsUntrackedFolder(const std::filesystem::path& relative) const
	{
		if (m_UntrackedFolders.empty())
		{
			return false;
		}

		// Each ancestor is looked up whole, so "build/genx.txt" is never taken for being inside
		// "build/gen".
		std::filesystem::path current = relative;

		while (current.has_relative_path())
		{
			if (m_UntrackedFolders.contains(KeyOf(current)))
			{
				return true;
			}

			current = current.parent_path();
		}

		return false;
	}

	GitDecoration GitDecorations::File(const std::filesystem::path& relative) const
	{
		const auto found = m_Files.find(KeyOf(relative));

		if (found != m_Files.end())
		{
			return found->second;
		}

		return IsUntrackedFolder(relative) ? GitDecoration::Untracked : GitDecoration::None;
	}

	GitDecoration GitDecorations::Folder(const std::filesystem::path& relative) const
	{
		const auto found = m_Folders.find(KeyOf(relative));

		if (found != m_Folders.end())
		{
			return found->second;
		}

		return IsUntrackedFolder(relative) ? GitDecoration::Untracked : GitDecoration::None;
	}

	bool GitDecorations::IsEmpty() const
	{
		return m_Files.empty() && m_Folders.empty();
	}
}
