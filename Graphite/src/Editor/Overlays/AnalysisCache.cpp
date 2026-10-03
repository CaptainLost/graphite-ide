#include "Editor/Overlays/AnalysisCache.h"

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

	std::size_t BytesOf(const CachedAnalysis& analysis)
	{
		std::size_t bytes = sizeof(CachedAnalysis) + analysis.Semantic.Footprint();

		for (const Declaration& declaration : analysis.Declarations)
		{
			bytes += sizeof(Declaration) + declaration.Name.size();
		}

		for (const KnownCount& count : analysis.Counts)
		{
			bytes += sizeof(KnownCount) + count.Name.size();
		}

		return bytes;
	}

	AnalysisCache::AnalysisCache(std::size_t budgetBytes) : m_Budget(budgetBytes) {}

	void AnalysisCache::SetBudget(std::size_t budgetBytes)
	{
		if (m_Budget == budgetBytes)
		{
			return;
		}

		m_Budget = budgetBytes;

		Evict();
	}

	void AnalysisCache::Put(const std::filesystem::path& path, CachedAnalysis analysis)
	{
		const std::size_t bytes = BytesOf(analysis);

		if (bytes > m_Budget)
		{
			// Refused outright rather than stored and immediately evicted, which would empty the
			// cache on the way to keeping nothing.
			return;
		}

		if (const auto found = m_ByPath.find(path); found != m_ByPath.end())
		{
			m_Bytes -= found->second->Bytes;
			m_Entries.erase(found->second);
			m_ByPath.erase(found);
		}

		m_Entries.push_front(Entry{path, std::move(analysis), bytes});
		m_ByPath.emplace(path, m_Entries.begin());
		m_Bytes += bytes;

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

		CachedAnalysis kept = std::move(found->second->Analysis);

		m_Bytes -= found->second->Bytes;
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
		while (!m_Entries.empty() && m_Bytes > m_Budget)
		{
			m_Bytes -= m_Entries.back().Bytes;
			m_ByPath.erase(m_Entries.back().Path);
			m_Entries.pop_back();
		}
	}
}
