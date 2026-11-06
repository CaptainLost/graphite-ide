#pragma once

#include "Core/Base/Base.h"

#include "Git/Content/ContentStore.h"
#include "Git/Lines/LineReader.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Git/PatchMatch.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>

namespace Graphite {

	// The patch ids of one repository's store, grown by the checks on workers and written with the verdicts.
	struct PatchIdStore
	{
		std::filesystem::path File;
		Core::PatchIdCache Ids;
		std::once_flag Loaded;
	};

	// The first caller reads the file into the store; every later one reads nothing. Never on the UI thread: the file
	// grows with every commit a check reads.
	void LoadPatchIdsOnce(PatchIdStore& store);

	// Only what the read's branches can use: the verdicts of its candidates' pairs, and what was found for its branch tips.
	// The rest would pile up in the file as the lines move on.
	ContentCache VerdictsUsedBy(const LineRead& read, const ContentCache& cache);

	struct ContentWrite
	{
		std::uint64_t Sequence = 0;     // later writes have higher numbers
		std::filesystem::path VerdictsFile;
		std::optional<ContentCache> Verdicts; // empty: unchanged since the last write
		Core::Ref<PatchIdStore> PatchIds;     // null: unchanged since the last write
	};

	// Writes the cache files, on workers.
	// WARNING: Writes may run at once and out of order; they take turns, and one asked earlier never writes over a later
	// one of the same file. Another store's later write stops nothing.
	class ContentWriter : Core::NonCopyable
	{
	public:
		void Write(const ContentWrite& write);

	private:
		// Whether this write of the file is the newest yet; records it when it is.
		bool IsNewest(const std::filesystem::path& file, std::uint64_t sequence);

		std::mutex m_Mutex;
		std::map<std::filesystem::path, std::uint64_t> m_Written; // the newest sequence written, by file; under m_Mutex
	};
}
