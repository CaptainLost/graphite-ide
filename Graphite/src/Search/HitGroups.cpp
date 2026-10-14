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

		const bool isKnownGroup = m_Index.contains(id);

		if (!isKnownGroup)
		{
			DisplayPath displayPath = SplitPathForDisplay(hit.File, m_Root);

			m_Index.emplace(id, m_Groups.size());

			HitGroup group{.Id = std::move(id),
						   .Directory = std::move(displayPath.Directory),
						   .Name = std::move(displayPath.Name),
						   .File = hit.File,
						   .Hits = {}};

			m_Groups.push_back(std::move(group));
			m_Groups.back().Hits.push_back(std::move(hit));
		}
		else
		{
			m_Groups[m_Index.at(id)].Hits.push_back(std::move(hit));
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
