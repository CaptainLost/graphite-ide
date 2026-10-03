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

	// What a language server said about a document, in the form the editor draws from, kept after
	// the document itself is gone.
	struct CachedAnalysis
	{
		SemanticOverlay Semantic;

		// Where the declarations were. Kept with the counts rather than left to be asked for
		// again, because a count has nowhere to be drawn until these are known: the lens rows are
		// built from them, so restoring counts without them buys nothing until the server answers
		// anyway - which is the wait this exists to remove.
		std::vector<Declaration> Declarations;

		std::vector<KnownCount> Counts;

		// The file's modification time when this was put away. What makes the entry refusable: a
		// file edited outside the editor while its tab was closed would have colours measured
		// against text that is no longer there, and a span on the wrong word is worse than no span
		// at all.
		std::filesystem::file_time_type ModifiedAt{};
	};

	// Whether a closing document's answers are worth keeping, and the stamp to keep them under.
	//
	// Refuses a document with unsaved edits. The analysis was measured against the buffer while
	// the stamp describes the file, and for a document whose edits are being discarded those are
	// two different texts - so the stamp would vouch for text the analysis never saw, which is the
	// one thing it exists to prevent. Refuses equally when the file's time cannot be read at all:
	// "could not check" has to mean "do not restore", or a share that blinked at both ends of a
	// document's life restores an entry nothing ever validated.
	std::optional<CachedAnalysis> AnalysisWorthKeeping(
		CachedAnalysis analysis, bool documentModified,
		std::optional<std::filesystem::file_time_type> modifiedAt);

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

	// Roughly what one entry costs to keep. See SemanticOverlay::Footprint for why "roughly"
	// is the right answer here rather than a shortcoming.
	std::size_t BytesOf(const CachedAnalysis& analysis);

	class AnalysisCache
	{
	public:
		// The limit is bytes of kept analysis, not a count of entries. A count was right while
		// this only served tabs being reopened — sixteen covered the handful of files someone
		// moves between — and wrong the moment something walks the tree behind them, because a
		// pass would evict its own work within seconds of producing it. Bytes are also the only
		// unit a user can be asked about: a settings panel can offer megabytes, and cannot know
		// what a file's analysis will weigh.
		explicit AnalysisCache(std::size_t budgetBytes);

		// Takes effect at once, evicting down to the new figure rather than waiting for the next
		// Put — a budget lowered to free memory that then frees nothing until something else
		// happens is not a budget.
		void SetBudget(std::size_t budgetBytes);

		std::size_t GetBudget() const { return m_Budget; }

		std::size_t GetBytes() const { return m_Bytes; }

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

		struct Entry
		{
			std::filesystem::path Path;
			CachedAnalysis Analysis;

			// Measured once, on the way in. Evict runs over entries it is discarding and would
			// otherwise walk each one's overlay to learn what it is about to free.
			std::size_t Bytes = 0;
		};

		std::size_t m_Budget;
		std::size_t m_Bytes = 0;

		// Most recently kept at the front. A list rather than a vector because entries move to the
		// front on every Put and are erased from the middle on every Take, and iterators into it
		// have to survive both.
		std::list<Entry> m_Entries;

		// Keyed on the path itself, as DocumentWindows::m_States next door is, so that two
		// spellings of one file - separators the other way round, a trailing one - are the same
		// document here and there alike. Keying on the raw string would make this the only map in
		// the class that disagreed, and it would disagree by silently never hitting.
		std::map<std::filesystem::path, std::list<Entry>::iterator> m_ByPath;
	};
}
