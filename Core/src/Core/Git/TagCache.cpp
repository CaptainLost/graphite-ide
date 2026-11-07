#include "corepch.h"

#include "Core/Git/Tag.h"

#include <utility>

namespace Core {

	std::optional<TagCacheEntry> TagCache::Find(const Oid& tagObject) const
	{
		const std::lock_guard lock(m_Mutex);

		const auto found = m_Entries.find(tagObject);

		if (found == m_Entries.end())
		{
			return std::nullopt;
		}

		return found->second;
	}

	void TagCache::Store(const Oid& tagObject, TagCacheEntry entry)
	{
		const std::lock_guard lock(m_Mutex);
		m_Entries.insert_or_assign(tagObject, std::move(entry));
	}

	std::size_t TagCache::GetSize() const
	{
		const std::lock_guard lock(m_Mutex);
		return m_Entries.size();
	}
}
