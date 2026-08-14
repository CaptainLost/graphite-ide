#include "Search/HitGroups.h"

#include "Core/Fs/FileSystem.h"

#include <utility>

namespace Graphite {

	void HitGroups::Add(SearchHit hit)
	{
		std::string name = Core::ToUtf8(hit.File);

		const auto found = m_Index.find(name);

		if (found == m_Index.end())
		{
			m_Index.emplace(name, m_Groups.size());
			m_Groups.push_back(HitGroup{std::move(name), hit.File, {}});
			m_Groups.back().Hits.push_back(std::move(hit));
		}
		else
		{
			m_Groups[found->second].Hits.push_back(std::move(hit));
		}

		++m_Count;
	}

	void HitGroups::Clear()
	{
		m_Groups.clear();
		m_Index.clear();
		m_Count = 0;
	}
}
