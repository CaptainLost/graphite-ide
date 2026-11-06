#include "corepch.h"

#include "Core/Git/PatchMatch.h"

#include <utility>
#include <vector>

namespace Core {

	std::optional<std::optional<Oid>> PatchIdCache::Find(const Oid& commit) const
	{
		const std::lock_guard lock(m_Mutex);

		const auto found = m_Ids.find(commit);

		if (found == m_Ids.end())
		{
			return std::nullopt;
		}

		return found->second;
	}

	void PatchIdCache::Store(const Oid& commit, std::optional<Oid> patchId)
	{
		const std::lock_guard lock(m_Mutex);
		m_Ids.insert_or_assign(commit, std::move(patchId));
	}

	std::size_t PatchIdCache::GetSize() const
	{
		const std::lock_guard lock(m_Mutex);
		return m_Ids.size();
	}

	std::vector<PatchIdEntry> PatchIdCache::Entries() const
	{
		const std::lock_guard lock(m_Mutex);

		std::vector<PatchIdEntry> entries;
		entries.reserve(m_Ids.size());

		for (const auto& [commit, patchId] : m_Ids)
		{
			entries.push_back(PatchIdEntry{.Commit = commit, .PatchId = patchId});
		}

		return entries;
	}
}
