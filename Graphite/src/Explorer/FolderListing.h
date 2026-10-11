#pragma once

#include "Core/Jobs/CancellationToken.h"
#include "Core/Jobs/JobSubmitter.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Graphite {

	struct ListedEntry
	{
		std::filesystem::path Path;
		std::string Name;
		bool IsDirectory = false;
		bool Ignored = false;

		// The index, in the same listing, of the folder holding this entry; none for an entry
		// directly in the root.
		std::optional<std::size_t> Parent;
	};

	// Every file and folder under the root, each folder before what it holds. A folder git
	// ignores, and .git itself, are listed but not entered: what they hold is never read.
	std::vector<ListedEntry> ListFolder(const std::filesystem::path& root, const Core::CancellationToken& cancellation);

	// The whole open folder as a list, read on a worker so a folder of any size never holds up a
	// frame. Read only while someone wants it, and read again once the disk has changed.
	class FolderListing final
	{
	public:
		explicit FolderListing(Core::JobSubmitter submit);
		~FolderListing();

		FolderListing(const FolderListing&) = delete;
		FolderListing& operator=(const FolderListing&) = delete;

		void SetRoot(const std::filesystem::path& root);

		void NoteChangedDirectories(const std::vector<std::filesystem::path>& directories);
		void NoteEverythingChanged();

		// Called on every frame the listing is wanted: starts a read when the listing is missing
		// or out of date and none is running, and takes a read that has finished.
		void Update();

		bool HasListing() const { return m_HasListing; }

		// Changes whenever the entries are replaced.
		std::uint64_t GetVersion() const { return m_Version; }

		const std::vector<ListedEntry>& GetEntries() const { return m_Entries; }

	private:
		struct FinishedRead;

		bool IsInsideUnreadFolder(const std::filesystem::path& directory) const;
		void StartRead();
		void TakeFinishedRead();

		Core::JobSubmitter m_Submit;
		std::filesystem::path m_Root;
		std::vector<ListedEntry> m_Entries;
		std::set<std::filesystem::path> m_UnreadFolders;
		bool m_HasListing = false;
		bool m_OutOfDate = true;
		bool m_Reading = false;
		std::uint64_t m_Version = 0;
		std::uint64_t m_ReadGeneration = 0;
		Core::CancellationToken m_Cancellation;
		std::shared_ptr<FinishedRead> m_Finished;
	};
}
