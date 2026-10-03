#include "Search/HitGroups.h"

#include "Core/FileSystem/FileSystem.h"
#include "Search/HitDisplay.h"

#include <utility>

namespace Graphite {

	void HitGroups::SetRoot(std::filesystem::path root)
	{
		m_Root = std::move(root);
	}

	void HitGroups::Add(SearchHit hit)
	{
		std::string id = Core::ToUtf8(hit.File);

		const auto found = m_Index.find(id);

		if (found == m_Index.end())
		{
			DisplayPath shown = SplitPathForDisplay(hit.File, m_Root);

			m_Index.emplace(id, m_Groups.size());
			m_Groups.push_back(HitGroup{std::move(id), std::move(shown.Directory), std::move(shown.Name), hit.File, {}});
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
