#pragma once

#include "Editor/LensModel.h"
#include "Editor/SemanticOverlay.h"

#include <cstddef>
#include <filesystem>
#include <list>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Graphite {

	// What a language server said about a document, in the form the editor draws from, kept after
	// the document itself is gone.
	struct CachedAnalysis
	{
		SemanticOverlay Semantic;

		std::vector<KnownCount> Counts;

		// The file's modification time when this was put away. What makes the entry refusable: a
		// file edited outside the editor while its tab was closed would have colours measured
		// against text that is no longer there, and a span on the wrong word is worse than no span
		// at all.
		std::filesystem::file_time_type ModifiedAt{};
	};

	// Analysis of documents whose tabs have closed, so reopening one paints in the frame it opens
	// rather than after a round trip to the server.
	//
	// A first frame, not a source of truth. Reference counts depend on the whole project, not on
	// the file they are drawn in, so an edit anywhere can make one wrong and no stamp on this file
	// would show it. The caller therefore asks the server as it always did and lets the answer
	// overwrite what was restored; what this buys is the seconds in between, during which the
	// screen shows the last known answer instead of nothing.
	//
	// Bounded because the alternative is remembering every file ever opened for the life of the
	// session, and the value of an entry falls off quickly - the documents worth restoring are the
	// handful someone is moving between.
	class AnalysisCache
	{
	public:
		explicit AnalysisCache(std::size_t capacity);

		// Replaces any entry for the same path, and makes it the most recently kept.
		void Put(const std::filesystem::path& path, CachedAnalysis analysis);

		// Empty when nothing was kept for this path, or when what was kept describes a version of
		// the file older than `modifiedAt`. Either way the entry does not survive the call:
		// restoring is a handover, and an entry refused as out of date is one nothing will ever
		// accept.
		std::optional<CachedAnalysis> Take(const std::filesystem::path& path,
										   std::filesystem::file_time_type modifiedAt);

	private:
		void Evict();

		std::size_t m_Capacity;

		// Most recently kept at the front. A list rather than a vector because entries move to the
		// front on every Put and are erased from the middle on every Take, and iterators into it
		// have to survive both.
		std::list<std::pair<std::filesystem::path, CachedAnalysis>> m_Entries;

		std::unordered_map<std::filesystem::path::string_type,
						   decltype(m_Entries)::iterator> m_ByPath;
	};
}
