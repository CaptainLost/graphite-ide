#include "Explorer/PendingFolderRename.h"

#include <utility>

namespace Graphite {

	void PendingFolderRename::Request(std::string path, std::uint64_t currentLoad)
	{
		m_Path = std::move(path);
		m_RequestedAt = currentLoad;
		m_Looking = false;
	}

	void PendingFolderRename::BeginFrame(bool editable, std::uint64_t currentLoad)
	{
		if (!editable)
		{
			Clear();
			return;
		}

		m_Looking = IsPending() && currentLoad != m_RequestedAt;
	}

	bool PendingFolderRename::LeadsTo(const std::string& folderPath) const
	{
		return m_Looking && m_Path.starts_with(folderPath + "/");
	}

	bool PendingFolderRename::Take(const std::string& path)
	{
		if (!m_Looking || path != m_Path)
		{
			return false;
		}

		Clear();
		return true;
	}

	void PendingFolderRename::EndFrame()
	{
		if (m_Looking)
		{
			Clear();
		}
	}

	void PendingFolderRename::Clear()
	{
		m_Path.clear();
		m_RequestedAt = 0;
		m_Looking = false;
	}
}
