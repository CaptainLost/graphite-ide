#pragma once

#include "Editor/Overlays/LensModel.h"
#include "Language/Api/LanguageService.h"
#include "Editor/Overlays/SemanticOverlay.h"

#include <cstddef>
#include <filesystem>
#include <list>
#include <map>
#include <optional>
#include <vector>

namespace Graphite {

	struct CachedAnalysis
	{
		SemanticOverlay Semantic;

		std::vector<Declaration> Declarations;

		std::vector<KnownCount> Counts;

		std::filesystem::file_time_type ModifiedAt{};
	};

	// WARNING: Refuses a modified document and an unreadable file time: the stamp would vouch for
	// text the analysis never saw.
	std::optional<CachedAnalysis> AnalysisWorthKeeping(
		CachedAnalysis analysis, bool documentModified,
		std::optional<std::filesystem::file_time_type> modifiedAt);

	std::size_t BytesOf(const CachedAnalysis& analysis);

	class AnalysisCache
	{
	public:
		explicit AnalysisCache(std::size_t budgetBytes);

		void SetBudget(std::size_t budgetBytes);

		std::size_t GetBudget() const { return m_Budget; }

		std::size_t GetBytes() const { return m_Bytes; }

		void Put(const std::filesystem::path& path, CachedAnalysis analysis);

		std::optional<CachedAnalysis> Take(const std::filesystem::path& path,
										   std::filesystem::file_time_type modifiedAt);

	private:
		void Evict();

		struct Entry
		{
			std::filesystem::path Path;
			CachedAnalysis Analysis;

			std::size_t Bytes = 0;
		};

		using EntryIndex = std::map<std::filesystem::path, std::list<Entry>::iterator>;

		std::size_t m_Budget;
		std::size_t m_Bytes = 0;

		std::list<Entry> m_Entries;

		EntryIndex m_ByPath;
	};
}
