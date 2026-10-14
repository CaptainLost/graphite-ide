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
			return;
		}

		if (const EntryIndex::iterator found = m_ByPath.find(path); found != m_ByPath.end())
		{
			const auto& [keptPath, keptEntry] = *found;

			m_Bytes -= keptEntry->Bytes;
			m_Entries.erase(keptEntry);
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
		const EntryIndex::iterator found = m_ByPath.find(path);

		if (found == m_ByPath.end())
		{
			return std::nullopt;
		}

		const auto& [keptPath, keptEntry] = *found;
		CachedAnalysis kept = std::move(keptEntry->Analysis);

		m_Bytes -= keptEntry->Bytes;
		m_Entries.erase(keptEntry);
		m_ByPath.erase(found);

		if (kept.ModifiedAt != modifiedAt)
		{
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
