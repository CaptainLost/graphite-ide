#pragma once

#include "Core/Base/Base.h"

#include "Core/Jobs/CancellationToken.h"
#include "Core/Jobs/JobQueue.h"
#include "Search/SearchQuery.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Graphite {

	struct UnsavedFile
	{
		std::filesystem::path Path;
		std::string Text;
	};

	struct SearchProgress
	{
		std::size_t FilesScanned = 0;
		std::size_t FilesSkipped = 0;
		std::size_t Unreadable = 0;

		bool Running = false;
	};

	struct SearchStart
	{
		SearchQuery Query;
		SearchOptions Options;

		std::filesystem::path Root;

		std::vector<std::filesystem::path> ProjectFiles;

		std::vector<UnsavedFile> Unsaved;
	};

	class SearchService
	{
	public:
		SearchService();
		~SearchService();

		SearchService(const SearchService&) = delete;
		SearchService& operator=(const SearchService&) = delete;

		void Start(SearchStart request);

		void Cancel();

		void Drain(std::vector<SearchHit>& hits, SearchProgress& progress);

	private:
		struct Shared
		{
			std::mutex Mutex;
			std::uint64_t Generation = 0;
			std::vector<SearchHit> Hits;
			SearchProgress Progress;
		};

		enum class FileOutcome
		{
			Scanned,
			Skipped,
			Unreadable
		};

		struct ScanContext
		{
			Shared& SharedState;
			const Core::CancellationToken& Token;
			const SearchStart& Input;
			std::uint64_t Generation;
		};

		static void Contribute(Shared& shared, std::uint64_t generation,
							   const std::vector<SearchHit>& found, FileOutcome outcome);
		static void ScanFile(const ScanContext& context, const std::filesystem::path& file);

		Core::Ref<Shared> m_Shared = Core::CreateRef<Shared>();

		Core::CancellationToken m_Token;

		// WARNING: Declared last so its destructor runs first and joins the workers while
		// everything they touch is still alive.
		Core::JobQueue m_Jobs;
	};
}
