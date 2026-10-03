#pragma once

#include "Core/Base/Base.h"

#include "Core/Jobs/CancellationToken.h"
#include "Core/Jobs/JobQueue.h"
#include "Search/SearchQuery.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Graphite {

	// An open document's current text, copied on the UI thread before the search starts. The
	// copy is the whole point: DocumentSet's buffers change under the user's hands, so a worker
	// reading them directly would be a data race. Open tabs number in the tens.
	struct UnsavedFile
	{
		std::filesystem::path Path;
		std::string Text;
	};

	struct SearchProgress
	{
		std::size_t FilesScanned = 0;

		// Binary or past the size limit. Not an error, and reported separately from one.
		std::size_t FilesSkipped = 0;

		std::size_t Unreadable = 0;

		bool Running = false;
	};

	struct SearchStart
	{
		SearchQuery Query;
		SearchOptions Options;

		std::filesystem::path Root;

		// Already flattened by the caller when Options.ProjectsOnly is set, because the Solution
		// lives on the UI thread and this must not reach across to it.
		std::vector<std::filesystem::path> ProjectFiles;

		std::vector<UnsavedFile> Unsaved;
	};

	// Runs one search at a time. Starting a second cancels the first, and anything the first
	// produces afterwards is dropped rather than shown: staleness is detected, not prevented.
	class SearchService
	{
	public:
		SearchService();
		~SearchService();

		SearchService(const SearchService&) = delete;
		SearchService& operator=(const SearchService&) = delete;

		void Start(SearchStart request);

		void Cancel();

		// Moves everything found since the last call into `hits` and reports where the work is.
		// UI thread only — this is the one place results cross over.
		void Drain(std::vector<SearchHit>& hits, SearchProgress& progress);

	private:
		// Held through a shared_ptr so a job that is still winding down after Cancel has
		// somewhere valid to write, whether or not anyone is still reading.
		struct Shared
		{
			std::mutex Mutex;
			std::uint64_t Generation = 0;
			std::vector<SearchHit> Hits;
			SearchProgress Progress;
		};

		Core::Ref<Shared> m_Shared = Core::CreateRef<Shared>();

		Core::CancellationToken m_Token;

		// Declared last so its destructor runs first and joins the workers while everything
		// they touch is still alive.
		Core::JobQueue m_Jobs;
	};
}
