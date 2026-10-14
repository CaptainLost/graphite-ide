#include "Explorer/FilteredTree.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>

namespace Graphite {

	namespace {

		using EntryIndices = std::vector<std::size_t>;

		constexpr bool kNotKept = false;
		constexpr bool kHoldsNoMatchYet = false;

		class FilteredTreeBuilder
		{
		public:
			FilteredTreeBuilder(const std::vector<ListedEntry>& entries, const ListedEntryFilter& passes)
				: m_Entries(entries), m_Kept(entries.size(), kNotKept), m_HoldsMatch(entries.size(), kHoldsNoMatchYet),
				  m_ChildrenOf(entries.size())
			{
				for (std::size_t entryIndex = 0; entryIndex < entries.size(); ++entryIndex)
				{
					if (passes(entries[entryIndex]))
					{
						m_Kept[entryIndex] = true;
						KeepFoldersAbove(entryIndex);
					}
				}

				for (std::size_t entryIndex = 0; entryIndex < entries.size(); ++entryIndex)
				{
					if (m_Kept[entryIndex])
					{
						ChildrenOf(entries[entryIndex].Parent).push_back(entryIndex);
					}
				}
			}

			Core::FileNode Build(const Core::FileNode& root)
			{
				Core::FileNode filteredRoot{
					.Path = root.Path,
					.Name = root.Name,
					.IsDirectory = true,
					.Expanded = true,
					.ChildrenLoaded = true,
					.Readable = root.Readable,
					.Ignored = root.Ignored,
				};

				AddChildren(filteredRoot, m_RootChildren);
				return filteredRoot;
			}

		private:
			void KeepFoldersAbove(std::size_t entryIndex)
			{
				std::optional<std::size_t> folderIndex = m_Entries[entryIndex].Parent;

				while (folderIndex.has_value() && !m_HoldsMatch[*folderIndex])
				{
					m_HoldsMatch[*folderIndex] = true;
					m_Kept[*folderIndex] = true;
					folderIndex = m_Entries[*folderIndex].Parent;
				}
			}

			EntryIndices& ChildrenOf(std::optional<std::size_t> folderIndex)
			{
				if (folderIndex.has_value())
				{
					return m_ChildrenOf[*folderIndex];
				}

				return m_RootChildren;
			}

			void AddChildren(Core::FileNode& folder, const EntryIndices& childIndices)
			{
				folder.Children.reserve(childIndices.size());

				for (const std::size_t childIndex : childIndices)
				{
					const ListedEntry& entry = m_Entries[childIndex];

					Core::FileNode child{
						.Path = entry.Path,
						.Name = entry.Name,
						.IsDirectory = entry.IsDirectory,
						.Expanded = m_HoldsMatch[childIndex],
						.ChildrenLoaded = entry.IsDirectory,
						.Ignored = entry.Ignored,
					};

					AddChildren(child, m_ChildrenOf[childIndex]);
					folder.Children.push_back(std::move(child));
				}

				std::sort(folder.Children.begin(), folder.Children.end(), Core::IsFileNodeListedBefore);
			}

			const std::vector<ListedEntry>& m_Entries;
			std::vector<bool> m_Kept;
			std::vector<bool> m_HoldsMatch;
			std::vector<EntryIndices> m_ChildrenOf;
			EntryIndices m_RootChildren;
		};
	}

	Core::FileNode BuildFilteredTree(const Core::FileNode& root, const std::vector<ListedEntry>& entries,
									 const ListedEntryFilter& passes)
	{
		FilteredTreeBuilder builder{entries, passes};
		return builder.Build(root);
	}
}
