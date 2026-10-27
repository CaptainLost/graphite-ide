#include "Hex/HexFiles.h"

#include <utility>

namespace Graphite {

	HexFiles::HexFiles(Core::JobSubmitter submit) : m_Submit(std::move(submit))
	{
	}

	HexFile& HexFiles::For(const std::filesystem::path& path)
	{
		return m_Files.try_emplace(path, path, m_Submit).first->second;
	}

	void HexFiles::Prune(const std::set<std::filesystem::path>& open)
	{
		std::erase_if(m_Files, [&open](const auto& entry) { return !open.contains(entry.first); });
	}

	void HexFiles::CheckDisk(const std::filesystem::path& path)
	{
		if (const auto entry = m_Files.find(path); entry != m_Files.end())
		{
			entry->second.CheckDisk();
		}
	}

	void HexFiles::Update()
	{
		for (auto& entry : m_Files)
		{
			entry.second.Update();
		}
	}
}
