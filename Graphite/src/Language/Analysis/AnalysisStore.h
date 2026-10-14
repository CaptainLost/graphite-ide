#pragma once

#include "Core/Base/Base.h"

#include "Editor/Overlays/AnalysisCache.h"

#include "Core/Jobs/CancellationToken.h"
#include "Core/Jobs/JobQueue.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	class AnalysisStore
	{
	public:
		AnalysisStore();
		~AnalysisStore();

		AnalysisStore(const AnalysisStore&) = delete;
		AnalysisStore& operator=(const AnalysisStore&) = delete;

		void SetRoot(std::filesystem::path root);

		const std::filesystem::path& GetRoot() const { return m_Root; }

		void Poll();

		bool IsReady() const;

		bool Has(const std::filesystem::path& path) const;

		std::optional<CachedAnalysis> Read(const std::filesystem::path& path,
										   std::filesystem::file_time_type modifiedAt);

		void Write(const std::filesystem::path& path, const CachedAnalysis& analysis);

		bool IsQuiet() const;

	private:
		struct Entry
		{
			std::string Record;
			std::filesystem::file_time_type ModifiedAt{};
		};

		using EntryMap = std::map<std::filesystem::path, Entry>;

		struct Listing
		{
			std::mutex Mutex;
			EntryMap Entries;
			bool Done = false;

			bool IndexDirty = false;
			std::size_t RecordsSinceIndex = 0;
			std::chrono::steady_clock::time_point IndexWrittenAt{};
		};

		enum class IndexFlushPolicy
		{
			WhenFarBehind,
			Always
		};

		std::filesystem::path Directory() const;
		std::filesystem::path RecordPath(const std::string& record) const;

		static std::string RecordNameFor(const std::filesystem::path& key);

		static std::string EncodeIndex(const EntryMap& entries);

		static std::optional<EntryMap> DecodeIndex(std::string_view bytes);

		static void WriteIndex(const std::filesystem::path& directory, const EntryMap& entries);

		static void FlushIndex(const std::filesystem::path& directory, Listing& listing,
							   IndexFlushPolicy policy);

		void FlushIndexOnWorker();

		static void ReadIndexInto(const std::filesystem::path& directory,
								  const Core::CancellationToken& token, Listing& listing);

		static void SweepUnnamedRecords(const std::filesystem::path& directory,
										const EntryMap& kept);

		void DropEntry(const std::filesystem::path& key, const std::string& record);

		std::filesystem::path m_Root;

		EntryMap m_Entries;
		bool m_Ready = true;

		Core::Ref<Listing> m_Listing;
		Core::CancellationToken m_Token;

		std::atomic<int> m_OutstandingJobs{0};

		// WARNING: Declared last on purpose. Members are destroyed in reverse order, so this joins
		// its worker before anything the worker still touches goes away.
		Core::JobQueue m_Jobs{1};
	};
}
