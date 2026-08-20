#include "Editor/AnalysisCache.h"

#include <utility>

namespace Graphite {

	std::optional<CachedAnalysis> AnalysisWorthKeeping(
		CachedAnalysis analysis, bool documentModified,
		std::optional<std::filesystem::file_time_type> modifiedAt)
	{
		if (documentModified || !modifiedAt)
		{
			return std::nullopt;
		}

		if (analysis.Declarations.empty() && analysis.Semantic.IsEmpty())
		{
			// Nothing came back before the tab closed, so there is nothing to restore. Refused
			// rather than stored empty, where it would evict an entry that does hold answers.
			return std::nullopt;
		}

		analysis.ModifiedAt = *modifiedAt;

		return analysis;
	}

	AnalysisCache::AnalysisCache(std::size_t capacity) : m_Capacity(capacity) {}

	void AnalysisCache::Put(const std::filesystem::path& path, CachedAnalysis analysis)
	{
		if (m_Capacity == 0)
		{
			return;
		}

		if (const auto found = m_ByPath.find(path); found != m_ByPath.end())
		{
			m_Entries.erase(found->second);
			m_ByPath.erase(found);
		}

		m_Entries.emplace_front(path, std::move(analysis));
		m_ByPath.emplace(path, m_Entries.begin());

		Evict();
	}

	std::optional<CachedAnalysis> AnalysisCache::Take(const std::filesystem::path& path,
													 std::filesystem::file_time_type modifiedAt)
	{
		const auto found = m_ByPath.find(path);

		if (found == m_ByPath.end())
		{
			return std::nullopt;
		}

		CachedAnalysis kept = std::move(found->second->second);

		m_Entries.erase(found->second);
		m_ByPath.erase(found);

		if (kept.ModifiedAt != modifiedAt)
		{
			// Not only newer: a file restored from a backup, or a branch switched back, carries an
			// older stamp and describes text this no longer matches either.
			return std::nullopt;
		}

		return kept;
	}

	void AnalysisCache::Evict()
	{
		while (m_Entries.size() > m_Capacity)
		{
			m_ByPath.erase(m_Entries.back().first);
			m_Entries.pop_back();
		}
	}
}
