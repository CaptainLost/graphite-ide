#include "Explorer/FolderListing.h"

#include "Core/Base/Result.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Repository.h"

#include <utility>

namespace Graphite {

	namespace {

		constexpr const char* kGitFolderName = ".git";

		struct FolderToRead
		{
			std::filesystem::path Path;
			std::optional<std::size_t> Index;
		};

		std::optional<Core::Repository> OpenRepositoryAt(const std::filesystem::path& root)
		{
			Core::Result<Core::Repository> opened = Core::Repository::Open(root);

			if (!opened)
			{
				return std::nullopt;
			}

			return std::move(opened).GetValue();
		}

		bool IsEntered(const ListedEntry& entry)
		{
			return entry.IsDirectory && !entry.Ignored && entry.Name != kGitFolderName;
		}

		void ReadOneFolder(const FolderToRead& folder, const std::optional<Core::Repository>& repository,
						   std::vector<ListedEntry>& entries, std::vector<FolderToRead>& foldersToRead)
		{
			const Core::Result<std::vector<Core::DirectoryEntry>> listing = Core::ReadDirectory(folder.Path);

			if (!listing)
			{
				return;
			}

			for (const Core::DirectoryEntry& directoryEntry : listing.GetValue())
			{
				entries.push_back(ListedEntry{
					.Path = directoryEntry.Path,
					.Name = directoryEntry.Name,
					.IsDirectory = directoryEntry.IsDirectory,
					.Ignored = repository.has_value() && repository->IsIgnored(directoryEntry.Path),
					.Parent = folder.Index,
				});

				if (IsEntered(entries.back()))
				{
					foldersToRead.push_back(FolderToRead{.Path = directoryEntry.Path, .Index = entries.size() - 1});
				}
			}
		}

		std::set<std::filesystem::path> FoldersNotEntered(const std::vector<ListedEntry>& entries)
		{
			std::set<std::filesystem::path> folders;

			for (const ListedEntry& entry : entries)
			{
				if (entry.IsDirectory && !IsEntered(entry))
				{
					folders.insert(entry.Path);
				}
			}

			return folders;
		}
	}

	std::vector<ListedEntry> ListFolder(const std::filesystem::path& root, const Core::CancellationToken& cancellation)
	{
		// WARNING: A libgit2 handle must not be shared with the thread that draws the tree, so the
		// repository is opened here, on the thread that reads.
		const std::optional<Core::Repository> repository = OpenRepositoryAt(root);

		std::vector<ListedEntry> entries;
		std::vector<FolderToRead> foldersToRead{FolderToRead{.Path = root}};

		while (!foldersToRead.empty() && !cancellation.IsCancelled())
		{
			const FolderToRead folder = std::move(foldersToRead.back());
			foldersToRead.pop_back();
			ReadOneFolder(folder, repository, entries, foldersToRead);
		}

		return entries;
	}

	FolderListing::FolderListing(Core::JobSubmitter submit) : m_Read(std::move(submit))
	{
	}

	void FolderListing::SetRoot(const std::filesystem::path& root)
	{
		if (root == m_Root)
		{
			return;
		}

		m_Read.Forget();

		m_Root = root;
		m_Entries.clear();
		m_UnreadFolders.clear();
		m_HasListing = false;
		m_OutOfDate = true;
		++m_Version;
	}

	void FolderListing::NoteChangedDirectories(const std::vector<std::filesystem::path>& directories)
	{
		for (const std::filesystem::path& directory : directories)
		{
			if (!IsInsideUnreadFolder(directory))
			{
				m_OutOfDate = true;
				return;
			}
		}
	}

	void FolderListing::NoteEverythingChanged()
	{
		m_OutOfDate = true;
	}

	void FolderListing::Update()
	{
		TakeFinishedRead();

		if (m_OutOfDate && !m_Read.IsInFlight() && !m_Root.empty())
		{
			StartRead();

			// WARNING: A job that runs where it is submitted has already finished here.
			TakeFinishedRead();
		}
	}

	bool FolderListing::IsInsideUnreadFolder(const std::filesystem::path& directory) const
	{
		for (std::filesystem::path folder = directory; folder != m_Root; folder = folder.parent_path())
		{
			if (m_UnreadFolders.contains(folder))
			{
				return true;
			}

			const bool reachedTopOfDrive = folder == folder.parent_path();

			if (reachedTopOfDrive)
			{
				return false;
			}
		}

		return false;
	}

	void FolderListing::StartRead()
	{
		m_OutOfDate = false;

		m_Read.Ask([root = m_Root](const Core::CancellationToken& cancellation) { return ListFolder(root, cancellation); });
	}

	void FolderListing::TakeFinishedRead()
	{
		std::optional<std::vector<ListedEntry>> entries = m_Read.TakeNewest();

		if (!entries.has_value())
		{
			return;
		}

		m_Entries = std::move(*entries);
		m_UnreadFolders = FoldersNotEntered(m_Entries);
		m_HasListing = true;
		++m_Version;
	}
}
