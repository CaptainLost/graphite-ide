#include <doctest/doctest.h>

#include "Search/QuickOpenListing.h"

#include "TempDirectory.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

	// A folder searched where it lives: lists what it was set up with, and remembers what it was asked.
	class ListedFolder final : public Graphite::FolderSearch
	{
	public:
		explicit ListedFolder(Core::Result<std::vector<std::string>> files) : m_Files(std::move(files)) {}

		Core::Result<std::vector<std::string>> ListFiles(bool respectGitIgnore, const Core::CancellationToken&) override
		{
			AskedToRespectGitIgnore = respectGitIgnore;
			return m_Files;
		}

		Core::Result<Graphite::FolderSearchFound> SearchText(const Graphite::SearchQuery&, bool, const std::vector<std::filesystem::path>&,
															 const Core::CancellationToken&) override
		{
			return Core::Error{Core::ErrorCode::Unsupported, "not searched here"};
		}

		bool AskedToRespectGitIgnore = false;

	private:
		Core::Result<std::vector<std::string>> m_Files;
	};
}

TEST_CASE("Quick open lists a folder on disk without .git, relative to the folder")
{
	const TempDirectory temp;
	std::filesystem::create_directories(temp.GetPath() / ".git");
	std::filesystem::create_directories(temp.GetPath() / "src");
	std::ofstream{temp.GetPath() / ".git" / "HEAD"} << "ref: refs/heads/main\n";
	std::ofstream{temp.GetPath() / "src" / "a.cpp"} << "a\n";

	Graphite::QuickOpenListing listing;
	Graphite::ListFolderFiles(temp.GetPath(), nullptr, Core::CancellationToken{}, listing);

	CHECK(listing.Done);
	CHECK(listing.Paths == std::vector<std::string>{"src/a.cpp"});
}

TEST_CASE("Quick open lists a folder searched where it lives through that folder's own listing")
{
	ListedFolder folder{std::vector<std::string>{"a.txt", "src/b.cpp"}};

	Graphite::QuickOpenListing listing;
	Graphite::ListFolderFiles(std::filesystem::path{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p"}, &folder, Core::CancellationToken{}, listing);

	CHECK(listing.Done);
	CHECK(listing.Paths == std::vector<std::string>{"a.txt", "src/b.cpp"});
	CHECK(folder.AskedToRespectGitIgnore);
}

TEST_CASE("Quick open lists nothing, and is done, when a folder's own listing fails")
{
	ListedFolder folder{Core::Error{Core::ErrorCode::IoFailure, "the WSL agent stopped"}};

	Graphite::QuickOpenListing listing;
	Graphite::ListFolderFiles(std::filesystem::path{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p"}, &folder, Core::CancellationToken{}, listing);

	CHECK(listing.Done);
	CHECK(listing.Paths.empty());
}
