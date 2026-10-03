#include "Lang/Analysis/AnalysisStore.h"

#include "Lang/Analysis/AnalysisRecord.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Log.h"
#include "Core/Fs/AtomicWrite.h"
#include "Core/Fs/FileSystem.h"

#include <functional>
#include <iterator>
#include <set>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {

		// Four bytes of its own, so an index and a record can never be mistaken for one another
		// even though both live in the same directory and are read through the same cursor.
		constexpr std::string_view kIndexMagic = "GRAI";

		// Bumped whenever the triples below change shape. Separate from the record's version
		// because the two formats change for different reasons; both are written into the index,
		// and a mismatch in either throws the whole store away.
		constexpr std::uint32_t kIndexVersion = 1;

		constexpr std::string_view kIndexName = "index.bin";

		// How far the index may fall behind the records before it is rewritten. Two limits rather
		// than one because the pass and a reader fill the store at completely different rates: a
		// first pass over a large tree finishes a file every few seconds and would reach the count
		// long before the interval, while a session where the only writer is a closing tab would
		// reach the interval and never the count.
		//
		// The numbers are not delicate. Anything in the tens turns the quadratic into something
		// linear enough not to matter, and anything in the tens of seconds bounds how much a crash
		// costs at a handful of files re-analysed. See AnalysisStore::FlushIndex.
		constexpr std::size_t kRecordsPerIndexWrite = 64;
		constexpr std::chrono::seconds kIndexWriteInterval{30};

		// Whole-file read with no error to report: everything above treats unreadable and corrupt
		// as the same answer, which is "ask the server again".
		std::string ReadWholeFile(const std::filesystem::path& path)
		{
			Core::Result<std::string> bytes = Core::ReadFileBytes(path);

			return bytes ? std::move(bytes.GetValue()) : std::string{};
		}

		// Empties the directory rather than removing it: the caller has just created it, and a
		// store that deleted its own home would have to create it again on the next write.
		void RemoveEverythingIn(const std::filesystem::path& directory)
		{
			std::error_code listing;

			for (const std::filesystem::directory_entry& entry :
				 std::filesystem::directory_iterator{directory, listing})
			{
				std::error_code removal;
				std::filesystem::remove_all(entry.path(), removal);
			}
		}
	}

	AnalysisStore::AnalysisStore() = default;

	AnalysisStore::~AnalysisStore()
	{
		// The listing can be a long walk over a large workspace, and the JobQueue destructor below
		// waits for whatever is running. Telling it to stop first is the difference between a
		// closing window and a closing window that hangs.
		m_Token.Cancel();

		// After the cancel, and the one job that ignores the token: the records are already on the
		// disk and the index that names them is the only thing missing. A clean exit that dropped
		// it would hand the next launch a sweep of everything this session wrote, which is exactly
		// the work the store exists to save.
		FlushIndexOnWorker();
	}

	void AnalysisStore::SetRoot(std::filesystem::path root)
	{
		// Before the cancel below drops every reference to the old listing: whatever the last root
		// bought and has not written an index for is still worth writing, for the reason the
		// destructor gives.
		FlushIndexOnWorker();

		// Anything still in flight was submitted against the old root. It keeps its own listing
		// alive through the shared_ptr and writes into that, and the token tells it not to touch
		// the disk on the way out.
		m_Token.Cancel();
		m_Token = Core::CancellationToken{};

		m_Root = std::move(root);
		m_Entries.clear();
		m_Listing = std::make_shared<Listing>();

		if (m_Root.empty())
		{
			// No folder is open. There is nothing to read and nowhere to read it from, so waiting
			// for a listing that will never be submitted would hold the background pass forever.
			m_Ready = true;
			return;
		}

		m_Ready = false;

		m_Jobs.Submit([listing = m_Listing, token = m_Token, directory = Directory()] {
			ReadIndexInto(directory, token, *listing);
			Core::Redraw::Request();
		});
	}

	void AnalysisStore::Poll()
	{
		if (m_Ready || !m_Listing)
		{
			return;
		}

		const std::lock_guard<std::mutex> guard(m_Listing->Mutex);

		if (!m_Listing->Done)
		{
			return;
		}

		// Copied rather than moved: the listing stays the worker's picture of the disk for the
		// rest of this root's life, and moving out of it would leave the next index written from
		// an empty map.
		m_Entries = m_Listing->Entries;
		m_Ready = true;
	}

	bool AnalysisStore::IsReady() const
	{
		return m_Ready;
	}

	bool AnalysisStore::Has(const std::filesystem::path& path) const
	{
		if (m_Root.empty())
		{
			return false;
		}

		return m_Entries.count(Core::NormalizePath(path)) != 0;
	}

	std::optional<CachedAnalysis> AnalysisStore::Read(const std::filesystem::path& path,
													  std::filesystem::file_time_type modifiedAt)
	{
		if (m_Root.empty())
		{
			return std::nullopt;
		}

		const std::filesystem::path key = Core::NormalizePath(path);
		const EntryMap::const_iterator found = m_Entries.find(key);

		if (found == m_Entries.end())
		{
			return std::nullopt;
		}

		const std::string record = found->second.Record;
		std::optional<CachedAnalysis> analysis =
			DecodeAnalysis(ReadWholeFile(RecordPath(record)), key);

		// The stamp is checked here as well as at pruning time because the file may have been
		// written since the index was read — this frame's ProbeFile knows that, the index does not.
		if (analysis && analysis->ModifiedAt == modifiedAt)
		{
			return analysis;
		}

		Drop(key, record);

		return std::nullopt;
	}

	void AnalysisStore::Write(const std::filesystem::path& path, const CachedAnalysis& analysis)
	{
		if (m_Root.empty())
		{
			return;
		}

		const std::filesystem::path key = Core::NormalizePath(path);

		Entry entry;
		entry.Record = RecordNameFor(key);
		entry.ModifiedAt = analysis.ModifiedAt;

		// Recorded here rather than when the worker lands, so a document opened moments after its
		// analysis arrived finds it instead of asking the server for what is already on its way to
		// the disk.
		m_Entries[key] = entry;

		m_Outstanding.fetch_add(1);

		m_Jobs.Submit([this, listing = m_Listing, token = m_Token, directory = Directory(), key,
					   entry, analysis] {
			if (!token.IsCancelled())
			{
				// Encoded here, not at the call site: an overlay of thousands of spans is tens of
				// kilobytes of string building, and the whole point of this queue is that the
				// frame does not pay for it.
				const Core::Result<void> written =
					Core::WriteFileAtomically(directory / entry.Record, EncodeAnalysis(key, analysis));

				if (written)
				{
					{
						const std::lock_guard<std::mutex> guard(listing->Mutex);
						listing->Entries[key] = entry;
						listing->IndexDirty = true;
						++listing->RecordsSinceIndex;
					}

					// Not with every record: the index names them all, so rewriting it here would
					// make the bytes written over a first pass quadratic in the size of the
					// workspace. FlushIndex decides when it is far enough behind to be worth
					// writing, and says what that costs.
					FlushIndex(directory, *listing, false);
				}
			}

			m_Outstanding.fetch_sub(1);
		});
	}

	bool AnalysisStore::IsQuiet() const
	{
		return m_Outstanding.load() == 0;
	}

	std::filesystem::path AnalysisStore::Directory() const
	{
		return m_Root / ".graphite" / "analysis";
	}

	std::filesystem::path AnalysisStore::RecordPath(const std::string& record) const
	{
		return Directory() / record;
	}

	std::string AnalysisStore::RecordNameFor(const std::filesystem::path& key)
	{
		// A hash rather than the path itself because a path is not a filename: it carries
		// separators and a drive letter, and flattening it would run past the length a directory
		// entry may have. Collisions are what the path written inside the record guards against.
		const std::size_t hash = std::hash<std::string>{}(Core::ToUtf8(key));

		constexpr std::string_view kDigits = "0123456789abcdef";

		std::string name;
		name.reserve(20);

		for (int shift = 60; shift >= 0; shift -= 4)
		{
			name.push_back(kDigits[(static_cast<std::uint64_t>(hash) >> shift) & 0xF]);
		}

		name += ".rec";

		return name;
	}

	std::string AnalysisStore::EncodeIndex(const EntryMap& entries)
	{
		std::string bytes;

		bytes.append(kIndexMagic);
		PutU32(bytes, kIndexVersion);
		PutU32(bytes, kAnalysisRecordVersion);
		PutU32(bytes, static_cast<std::uint32_t>(entries.size()));

		for (const std::pair<const std::filesystem::path, Entry>& entry : entries)
		{
			PutString(bytes, Core::ToUtf8(entry.first));
			PutString(bytes, entry.second.Record);
			PutU64(bytes, FileTimeToTicks(entry.second.ModifiedAt));
		}

		return bytes;
	}

	std::optional<AnalysisStore::EntryMap> AnalysisStore::DecodeIndex(std::string_view bytes)
	{
		Reader reader{bytes};

		if (!reader.TakeMagic(kIndexMagic) || reader.TakeU32() != kIndexVersion ||
			reader.TakeU32() != kAnalysisRecordVersion)
		{
			return std::nullopt;
		}

		EntryMap entries;

		const std::uint32_t count = reader.TakeU32();

		for (std::uint32_t index = 0; index < count && !reader.Failed(); ++index)
		{
			const std::string path = reader.TakeString();

			Entry entry;
			entry.Record = reader.TakeString();
			entry.ModifiedAt = TicksToFileTime(reader.TakeU64());

			// Inserted only once the reader is known to have had the bytes for all three, so a
			// count claiming more than is there cannot fill this map with empty paths.
			if (!reader.Failed())
			{
				entries.emplace(Core::FromUtf8(path), std::move(entry));
			}
		}

		if (!reader.AtEnd())
		{
			return std::nullopt;
		}

		return entries;
	}

	void AnalysisStore::WriteIndex(const std::filesystem::path& directory, const EntryMap& entries)
	{
		// Atomically, because a half-written index is exactly the corruption the next session then
		// has to throw the whole store away over — and the store it would throw away is the one
		// this session just spent a workspace walk filling.
		const Core::Result<void> written =
			Core::WriteFileAtomically(directory / kIndexName, EncodeIndex(entries));

		// Nothing the user has to act on: a store that could not be written is a store the next
		// session finds empty, which is what it would have found anyway. Logged, so a store that
		// never fills has a reason on record.
		if (!written)
		{
			APP_WARN("Cannot write the analysis index in '{}': {}", Core::ToUtf8(directory),
					 written.GetError().Message);
		}
	}

	void AnalysisStore::FlushIndex(const std::filesystem::path& directory, Listing& listing,
								   bool force)
	{
		EntryMap snapshot;

		{
			const std::lock_guard<std::mutex> guard(listing.Mutex);

			if (!listing.IndexDirty)
			{
				return;
			}

			const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

			if (!force && listing.RecordsSinceIndex < kRecordsPerIndexWrite &&
				now - listing.IndexWrittenAt < kIndexWriteInterval)
			{
				return;
			}

			// Marked written before the write rather than after, and safe to be: this all happens
			// on the one worker thread, so nothing can slip a record in between, and a failed
			// write is a store the next session finds short a few entries — which is a miss, and a
			// miss is only ever a miss.
			listing.IndexDirty = false;
			listing.RecordsSinceIndex = 0;
			listing.IndexWrittenAt = now;

			// Copied out before writing, so Poll on the UI thread never waits behind a disk write
			// for this mutex.
			snapshot = listing.Entries;
		}

		WriteIndex(directory, snapshot);
	}

	void AnalysisStore::FlushIndexOnWorker()
	{
		if (m_Root.empty() || !m_Listing)
		{
			return;
		}

		m_Outstanding.fetch_add(1);

		// No token check inside: this runs precisely because the token has been, or is about to
		// be, cancelled, and what it writes describes records already on the disk.
		m_Jobs.Submit([this, listing = m_Listing, directory = Directory()] {
			FlushIndex(directory, *listing, true);

			m_Outstanding.fetch_sub(1);
		});
	}

	void AnalysisStore::SweepUnnamedRecords(const std::filesystem::path& directory,
											const EntryMap& kept)
	{
		std::set<std::string> named;

		for (const std::pair<const std::filesystem::path, Entry>& entry : kept)
		{
			named.insert(entry.second.Record);
		}

		std::error_code listing;

		for (const std::filesystem::directory_entry& entry :
			 std::filesystem::directory_iterator{directory, listing})
		{
			// Only records. The index itself lives here, and so briefly does the `.tmp` neighbour
			// WriteFileAtomically swaps into place — deleting either out from under a write would
			// be this sweep causing the corruption it exists to clean up after.
			if (entry.path().extension() != ".rec")
			{
				continue;
			}

			if (named.count(Core::ToUtf8(entry.path().filename())) != 0)
			{
				continue;
			}

			std::error_code removal;
			std::filesystem::remove(entry.path(), removal);
		}
	}

	void AnalysisStore::ReadIndexInto(const std::filesystem::path& directory,
									  const Core::CancellationToken& token, Listing& listing)
	{
		std::error_code creation;
		std::filesystem::create_directories(directory, creation);

		EntryMap kept;

		if (const std::optional<EntryMap> read = DecodeIndex(ReadWholeFile(directory / kIndexName)))
		{
			for (const std::pair<const std::filesystem::path, Entry>& entry : *read)
			{
				if (token.IsCancelled())
				{
					return;
				}

				std::error_code stat;
				const std::filesystem::file_time_type stamp =
					std::filesystem::last_write_time(entry.first, stat);

				// "Could not be read" counts as gone. A record vouched for by a stamp nobody
				// could check is a record that would draw colours measured against unknown text.
				if (!stat && stamp == entry.second.ModifiedAt)
				{
					kept.emplace(entry.first, entry.second);
				}
				else
				{
					std::error_code removal;
					std::filesystem::remove(directory / entry.second.Record, removal);
				}
			}

			SweepUnnamedRecords(directory, kept);
		}
		else
		{
			// No index, or one that is not this format: the records beside it are named by a hash
			// nothing can turn back into a path, so there is no way left to tell an orphan from a
			// live entry. Everything goes.
			RemoveEverythingIn(directory);
		}

		if (token.IsCancelled())
		{
			return;
		}

		WriteIndex(directory, kept);

		const std::lock_guard<std::mutex> guard(listing.Mutex);
		listing.Entries = std::move(kept);

		// The index on disk and Entries agree exactly, as of now: the coalescing window starts
		// here rather than at the epoch, so the first record written into a fresh root waits with
		// the rest of its batch instead of forcing a rewrite of what was just written.
		listing.IndexDirty = false;
		listing.RecordsSinceIndex = 0;
		listing.IndexWrittenAt = std::chrono::steady_clock::now();

		listing.Done = true;
	}

	void AnalysisStore::Drop(const std::filesystem::path& key, const std::string& record)
	{
		m_Entries.erase(key);

		m_Outstanding.fetch_add(1);

		m_Jobs.Submit([this, listing = m_Listing, token = m_Token, directory = Directory(), key,
					   record] {
			if (!token.IsCancelled())
			{
				std::error_code removal;
				std::filesystem::remove(directory / record, removal);

				{
					const std::lock_guard<std::mutex> guard(listing->Mutex);
					listing->Entries.erase(key);
					listing->IndexDirty = true;
				}

				// Forced, unlike a record write: an index still naming a record that is no longer
				// there makes every later launch keep the entry through pruning and every later
				// open pay to read a file that is gone. Rare enough to afford — this is one
				// refused entry at a time, not one per analysed file — and it flushes whatever
				// records had coalesced behind it as well.
				FlushIndex(directory, *listing, true);
			}

			m_Outstanding.fetch_sub(1);
		});
	}
}
