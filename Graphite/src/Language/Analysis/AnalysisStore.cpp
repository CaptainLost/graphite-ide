#include "Language/Analysis/AnalysisStore.h"

#include "Core/Base/Base.h"

#include "Language/Analysis/AnalysisRecord.h"
#include "Persistence/FolderStateDirectory.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Ascii.h"
#include "Core/Base/Log.h"
#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileSystem.h"

#include <functional>
#include <iterator>
#include <set>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {

		constexpr std::string_view kIndexMagic = "GRAI";

		constexpr std::uint32_t kIndexVersion = 1;

		constexpr std::string_view kIndexName = "index.bin";

		constexpr std::string_view kRecordExtension = ".rec";

		constexpr std::size_t kRecordsPerIndexWrite = 64;
		constexpr std::chrono::seconds kIndexWriteInterval{30};

		constexpr int kBitsPerHexDigit = 4;
		constexpr int kHighestHashShift = 60;
		constexpr std::uint64_t kHexDigitMask = 0xF;
		constexpr std::size_t kHashDigitCount = 16;
		constexpr std::size_t kRecordNameLength = kHashDigitCount + kRecordExtension.size();

		std::string ReadFileBytesOrEmpty(const std::filesystem::path& path)
		{
			Core::Result<std::string> bytes = Core::ReadFileBytes(path);

			return bytes ? std::move(bytes.GetValue()) : std::string{};
		}

		bool IsRecordFile(const std::filesystem::path& path)
		{
			return path.extension() == std::filesystem::path{kRecordExtension};
		}

		void RemoveEverythingIn(const std::filesystem::path& directory)
		{
			std::error_code listingError;

			for (const std::filesystem::directory_entry& directoryEntry :
				 std::filesystem::directory_iterator{directory, listingError})
			{
				std::error_code removalError;
				std::filesystem::remove_all(directoryEntry.path(), removalError);
			}
		}
	}

	AnalysisStore::AnalysisStore() = default;

	AnalysisStore::~AnalysisStore()
	{
		m_Token.Cancel();

		FlushIndexOnWorker();
	}

	void AnalysisStore::SetRoot(std::filesystem::path root)
	{
		FlushIndexOnWorker();

		m_Token.Cancel();
		m_Token = Core::CancellationToken{};

		m_Root = std::move(root);
		m_Entries.clear();
		m_Listing = Core::CreateRef<Listing>();

		if (m_Root.empty())
		{
			m_Ready = true;
			return;
		}

		m_Ready = false;

		m_Jobs.Submit([listing = m_Listing, token = m_Token, root = m_Root, directory = Directory()] {
			static_cast<void>(EnsureFolderStateDirectory(root)); // logs its own failure
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

		const auto& [foundKey, foundEntry] = *found;
		const std::string record = foundEntry.Record;
		std::optional<CachedAnalysis> analysis =
			DecodeAnalysis(ReadFileBytesOrEmpty(RecordPath(record)), key);

		if (analysis && analysis->ModifiedAt == modifiedAt)
		{
			return analysis;
		}

		DropEntry(key, record);

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

		m_Entries[key] = entry;

		m_OutstandingJobs.fetch_add(1);

		m_Jobs.Submit([this, listing = m_Listing, token = m_Token, directory = Directory(), key,
					   entry, analysis] {
			if (!token.IsCancelled())
			{
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

					FlushIndex(directory, *listing, IndexFlushPolicy::WhenFarBehind);
				}
			}

			m_OutstandingJobs.fetch_sub(1);
		});
	}

	bool AnalysisStore::IsQuiet() const
	{
		return m_OutstandingJobs.load() == 0;
	}

	std::filesystem::path AnalysisStore::Directory() const
	{
		return FolderStateDirectory(m_Root) / "analysis";
	}

	std::filesystem::path AnalysisStore::RecordPath(const std::string& record) const
	{
		return Directory() / record;
	}

	std::string AnalysisStore::RecordNameFor(const std::filesystem::path& key)
	{
		const std::size_t hash = std::hash<std::string>{}(Core::ToUtf8(key));

		std::string name;
		name.reserve(kRecordNameLength);

		for (int shift = kHighestHashShift; shift >= 0; shift -= kBitsPerHexDigit)
		{
			const std::uint64_t digit = (static_cast<std::uint64_t>(hash) >> shift) & kHexDigitMask;

			name.push_back(Core::kLowercaseHexDigits[digit]);
		}

		name += kRecordExtension;

		return name;
	}

	std::string AnalysisStore::EncodeIndex(const EntryMap& entries)
	{
		std::string bytes;

		bytes.append(kIndexMagic);
		AppendUint32(bytes, kIndexVersion);
		AppendUint32(bytes, kAnalysisRecordVersion);
		AppendUint32(bytes, static_cast<std::uint32_t>(entries.size()));

		for (const auto& [path, entry] : entries)
		{
			AppendString(bytes, Core::ToUtf8(path));
			AppendString(bytes, entry.Record);
			AppendUint64(bytes, FileTimeToTicks(entry.ModifiedAt));
		}

		return bytes;
	}

	std::optional<AnalysisStore::EntryMap> AnalysisStore::DecodeIndex(std::string_view bytes)
	{
		Reader reader{bytes};

		if (!reader.TakeMagic(kIndexMagic) || reader.TakeUint32() != kIndexVersion ||
			reader.TakeUint32() != kAnalysisRecordVersion)
		{
			return std::nullopt;
		}

		EntryMap entries;

		const std::uint32_t entryCount = reader.TakeUint32();

		for (std::uint32_t entryIndex = 0; entryIndex < entryCount && !reader.HasFailed();
			 ++entryIndex)
		{
			const std::string path = reader.TakeString();

			Entry entry;
			entry.Record = reader.TakeString();
			entry.ModifiedAt = TicksToFileTime(reader.TakeUint64());

			if (!reader.HasFailed())
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
		const Core::Result<void> written =
			Core::WriteFileAtomically(directory / kIndexName, EncodeIndex(entries));

		if (!written)
		{
			APP_WARN("Cannot write the analysis index in '{}': {}", Core::ToUtf8(directory),
					 written.GetError().Message);
		}
	}

	void AnalysisStore::FlushIndex(const std::filesystem::path& directory, Listing& listing,
								   IndexFlushPolicy policy)
	{
		EntryMap snapshot;

		{
			const std::lock_guard<std::mutex> guard(listing.Mutex);

			if (!listing.IndexDirty)
			{
				return;
			}

			const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

			const bool fewRecordsSinceIndex = listing.RecordsSinceIndex < kRecordsPerIndexWrite;
			const bool indexWrittenRecently = now - listing.IndexWrittenAt < kIndexWriteInterval;

			if (policy == IndexFlushPolicy::WhenFarBehind && fewRecordsSinceIndex &&
				indexWrittenRecently)
			{
				return;
			}

			listing.IndexDirty = false;
			listing.RecordsSinceIndex = 0;
			listing.IndexWrittenAt = now;

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

		m_OutstandingJobs.fetch_add(1);

		m_Jobs.Submit([this, listing = m_Listing, directory = Directory()] {
			FlushIndex(directory, *listing, IndexFlushPolicy::Always);

			m_OutstandingJobs.fetch_sub(1);
		});
	}

	void AnalysisStore::SweepUnnamedRecords(const std::filesystem::path& directory,
											const EntryMap& kept)
	{
		std::set<std::string> namedRecords;

		for (const auto& [path, entry] : kept)
		{
			namedRecords.insert(entry.Record);
		}

		std::error_code listingError;

		for (const std::filesystem::directory_entry& directoryEntry :
			 std::filesystem::directory_iterator{directory, listingError})
		{
			// WARNING: Only record files may be removed here. The index and the temporary file
			// WriteFileAtomically swaps into place live in the same directory, and deleting either
			// would corrupt the write in flight.
			if (!IsRecordFile(directoryEntry.path()))
			{
				continue;
			}

			if (namedRecords.count(Core::ToUtf8(directoryEntry.path().filename())) != 0)
			{
				continue;
			}

			std::error_code removalError;
			std::filesystem::remove(directoryEntry.path(), removalError);
		}
	}

	void AnalysisStore::ReadIndexInto(const std::filesystem::path& directory,
									  const Core::CancellationToken& token, Listing& listing)
	{
		std::error_code creationError;
		std::filesystem::create_directories(directory, creationError);

		EntryMap kept;

		if (const std::optional<EntryMap> indexEntries =
				DecodeIndex(ReadFileBytesOrEmpty(directory / kIndexName)))
		{
			for (const auto& [path, entry] : *indexEntries)
			{
				if (token.IsCancelled())
				{
					return;
				}

				std::error_code lastWriteError;
				const std::filesystem::file_time_type lastWrite =
					std::filesystem::last_write_time(path, lastWriteError);

				if (!lastWriteError && lastWrite == entry.ModifiedAt)
				{
					kept.emplace(path, entry);
				}
				else
				{
					std::error_code removalError;
					std::filesystem::remove(directory / entry.Record, removalError);
				}
			}

			SweepUnnamedRecords(directory, kept);
		}
		else
		{
			RemoveEverythingIn(directory);
		}

		if (token.IsCancelled())
		{
			return;
		}

		WriteIndex(directory, kept);

		const std::lock_guard<std::mutex> guard(listing.Mutex);
		listing.Entries = std::move(kept);

		listing.IndexDirty = false;
		listing.RecordsSinceIndex = 0;
		listing.IndexWrittenAt = std::chrono::steady_clock::now();

		listing.Done = true;
	}

	void AnalysisStore::DropEntry(const std::filesystem::path& key, const std::string& record)
	{
		m_Entries.erase(key);

		m_OutstandingJobs.fetch_add(1);

		m_Jobs.Submit([this, listing = m_Listing, token = m_Token, directory = Directory(), key,
					   record] {
			if (!token.IsCancelled())
			{
				std::error_code removalError;
				std::filesystem::remove(directory / record, removalError);

				{
					const std::lock_guard<std::mutex> guard(listing->Mutex);
					listing->Entries.erase(key);
					listing->IndexDirty = true;
				}

				FlushIndex(directory, *listing, IndexFlushPolicy::Always);
			}

			m_OutstandingJobs.fetch_sub(1);
		});
	}
}
