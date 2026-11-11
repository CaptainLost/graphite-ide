#pragma once

#include "Search/SearchQuery.h"

#include "Core/Base/Pointers.h"
#include "Core/Base/Result.h"
#include "Core/Jobs/CancellationToken.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Graphite {

	// What searching a folder where its files live found: hits with absolute paths, and the
	// counts the search panel shows.
	struct FolderSearchFound
	{
		std::vector<SearchHit> Hits;

		std::size_t Scanned = 0;
		std::size_t Skipped = 0;
		std::size_t Unreadable = 0;

		// The search stopped at its most hits.
		bool Capped = false;
	};

	// A folder listed and searched where its files live rather than file by file from Windows (a
	// folder inside WSL, through its distro's agent). Both leave out .git, and what git ignores
	// when asked to; both are Cancelled once the token is.
	class FolderSearch
	{
	public:
		virtual ~FolderSearch() = default;

		// Relative to the folder, '/'-separated, UTF-8.
		virtual Core::Result<std::vector<std::string>> ListFiles(bool respectGitIgnore, const Core::CancellationToken& token) = 0;

		// skip: absolute paths searched elsewhere (open documents, whose text is not on disk yet).
		virtual Core::Result<FolderSearchFound> SearchText(const SearchQuery& query, bool respectGitIgnore,
															const std::vector<std::filesystem::path>& skip,
															const Core::CancellationToken& token) = 0;
	};

	// The folder's own search, or null for a folder the host lists and reads itself.
	using FolderSearchOpener = std::function<Core::Scope<FolderSearch>(const std::filesystem::path& root)>;
}
