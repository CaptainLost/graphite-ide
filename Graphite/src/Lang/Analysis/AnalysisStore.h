#pragma once

#include "Editor/AnalysisCache.h"

#include "Core/Jobs/CancellationToken.h"
#include "Core/Jobs/JobQueue.h"

#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	// What the background pass learned about a workspace, kept between sessions.
	//
	// The second tier under AnalysisCache, and deliberately not inside it: that class is memory
	// only, touches no disk, and is tested without one. This is where the disk lives.
	//
	// Lives under the workspace's own `.graphite/analysis`, which WorkspaceSettings already uses
	// and `.gitignore` already covers. One record per analysed file, named by a hash of its path,
	// plus a small index naming the paths — without the index a hash could never be turned back
	// into a path, so nothing could tell that a file had been deleted or renamed, and records for
	// files that no longer exist would accumulate for the life of the project in a store that
	// deliberately has no size cap.
	//
	// Everything here is discardable by construction. A record or an index that is truncated, a
	// version behind, or simply not what it claims to be is thrown away without a message: the
	// caller asks the server again, which is what it would have done had the store been empty.
	class AnalysisStore
	{
	public:
		AnalysisStore();
		~AnalysisStore();

		AnalysisStore(const AnalysisStore&) = delete;
		AnalysisStore& operator=(const AnalysisStore&) = delete;

		// Points the store at a workspace, reading and pruning its index on a worker. An empty
		// root detaches: every lookup misses, every write is dropped, and IsReady is true at once,
		// which is what "no folder is open" looks like from here.
		void SetRoot(std::filesystem::path root);

		// UI thread, once a frame: takes in a finished listing.
		void Poll();

		// True once the index for the current root has been read in. The background pass holds
		// off until then — starting earlier would redo the very files the store exists to skip.
		bool IsReady() const;

		// Whether a valid record exists for this path. A map lookup that touches no disk: the
		// stamps were checked when the index was read, so what survives pruning is known good as
		// of that moment. A file changed since then reads as a hit and is refused later by Read,
		// which costs one skipped file and never shows a stale colour.
		bool Has(const std::filesystem::path& path) const;

		// Reads and decodes one record. Synchronous, on the UI thread, in the frame a document
		// opens — the one deliberate exception to this codebase's rule about file I/O there. It
		// is a single small file, at the moment ProbeFile and ModifiedTimeOf already touch the
		// filesystem; doing it on a worker would cost two frames and hand back the recolour this
		// whole feature exists to remove.
		//
		// Empty unless the record is intact, of this version, for this path, and stamped for
		// exactly this version of the file. An entry that fails any of those does not survive the
		// call: it is dropped here and deleted from the disk on the worker, because an entry
		// nothing will ever accept is one every later open pays to read again.
		std::optional<CachedAnalysis> Read(const std::filesystem::path& path,
										   std::filesystem::file_time_type modifiedAt);

		// Encodes and writes on a worker — tens of kilobytes serialised inside a frame is a
		// visible hitch, once per analysed file. The index is rewritten with it, which is why the
		// worker is one thread and not several.
		void Write(const std::filesystem::path& path, const CachedAnalysis& analysis);

		// True when no write is outstanding. For tests, which have to know when the disk has
		// caught up; nothing in the application asks.
		bool IsQuiet() const;

	private:
		struct Entry
		{
			std::string Record; // the record's filename, hash derived
			std::filesystem::file_time_type ModifiedAt{};
		};

		using EntryMap = std::map<std::filesystem::path, Entry>;

		// The listing a worker produces and Poll takes in. Held through a shared_ptr, as
		// LanguageServices holds its warm-up findings and for the same reason: a listing still
		// finishing after SetRoot has moved on needs somewhere valid to write.
		//
		// It outlives the handover. Once Poll has copied the entries out, this stays the one
		// worker-side picture of what is on the disk: every later write and every dropped entry
		// is applied here under the mutex and the index is rewritten from it, so the file on disk
		// is never assembled from a snapshot some other job has already moved past.
		struct Listing
		{
			std::mutex Mutex;
			EntryMap Entries;
			bool Done = false;
		};

		std::filesystem::path Directory() const;
		std::filesystem::path RecordPath(const std::string& record) const;

		// The record's filename for a path already through NormalizePath. Static because the
		// worker derives no names of its own — every one is decided on the UI thread, where the
		// map that has to agree with it lives.
		static std::string RecordNameFor(const std::filesystem::path& key);

		static std::string EncodeIndex(const EntryMap& entries);

		// Empty for anything that is not exactly an index of this version written against this
		// record version — which is what makes a record format bump throw the whole store away at
		// once rather than one refused record at a time.
		static std::optional<EntryMap> DecodeIndex(std::string_view bytes);

		static void WriteIndex(const std::filesystem::path& directory, const EntryMap& entries);

		// The whole of SetRoot's worker job. Static so it cannot reach for a member that SetRoot
		// may already have pointed somewhere else.
		static void ReadIndexInto(const std::filesystem::path& directory,
								  const Core::CancellationToken& token, Listing& listing);

		// Forgets an entry here and, on the worker, deletes its record and rewrites the index.
		void Drop(const std::filesystem::path& key, const std::string& record);

		std::filesystem::path m_Root;

		EntryMap m_Entries;
		bool m_Ready = true;

		std::shared_ptr<Listing> m_Listing;
		Core::CancellationToken m_Token;

		// Incremented when a write is handed to the worker, decremented when it lands. Atomic
		// because the two halves are on different threads.
		std::atomic<int> m_Outstanding{0};

		// Last, deliberately: members are destroyed in reverse order of declaration, so this one
		// joins its worker before anything the worker might still be touching goes away.
		Core::JobQueue m_Jobs{1};
	};
}
