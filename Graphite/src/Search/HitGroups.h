#pragma once

#include "Search/SearchQuery.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Graphite {

	struct HitGroup
	{
		// WARNING: The tree node's id, never drawn. It must stay the same for the life of a
		// search, or the node closes itself every time a hit arrives.
		std::string Id;

		std::string Directory;
		std::string Name;
		std::filesystem::path File;
		std::vector<SearchHit> Hits;
	};

	class HitGroups
	{
	public:
		// WARNING: Set when a search starts. Changing it mid-search leaves the list showing
		// two different kinds of path.
		void SetRoot(std::filesystem::path root);

		void Add(SearchHit hit);

		void Clear();

		const std::vector<HitGroup>& Get() const { return m_Groups; }

		std::vector<HitGroup>& Get() { return m_Groups; }

		std::size_t GetCount() const { return m_Count; }

	private:
		std::vector<HitGroup> m_Groups;

		// WARNING: Holds an index, not an iterator or a pointer. m_Groups grows, and either
		// would dangle the moment it reallocated.
		std::unordered_map<std::string, std::size_t> m_Index;

		std::filesystem::path m_Root;

		std::size_t m_Count = 0;
	};
}
