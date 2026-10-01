#include <doctest/doctest.h>

#include "Git/RepositoryGitPort.h"

#include "TempDirectory.h"

#include "Core/Fs/FileSystem.h"

#include <git2.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <process.h>
#include <string>
#include <system_error>

namespace {

	// A repository on disk with nothing in it. RepositoryGitPort only needs something
	// Core::Repository::Open can open - no commit is required to exercise its handle
	// cache, so none is made.
	class TempRepo
	{
	public:
		TempRepo()
		{
			static std::atomic<int> counter{0};
			const int id = counter.fetch_add(1);

			// Include the process ID to ensure uniqueness across concurrent test runs.
			// Without it, two concurrent GraphiteTests processes use the same directory names,
			// and each deletes the other's repository out from under it, causing transient
			// failures that pass on a solo rerun.
			m_Path = std::filesystem::temp_directory_path() /
					 ("graphite-tests-repogitport-" + std::to_string(id) + "-" + std::to_string(::_getpid()));

			std::error_code code;
			std::filesystem::remove_all(m_Path, code);
			std::filesystem::create_directories(m_Path, code);

			git_libgit2_init();

			git_repository* repo = nullptr;
			REQUIRE(git_repository_init(&repo, Core::ToUtf8(m_Path).c_str(), 0) == 0);
			git_repository_free(repo);
		}

		~TempRepo()
		{
			std::error_code code;
			std::filesystem::remove_all(m_Path, code);
		}

		TempRepo(const TempRepo&) = delete;
		TempRepo& operator=(const TempRepo&) = delete;

		const std::filesystem::path& GetPath() const { return m_Path; }

	private:
		std::filesystem::path m_Path;
	};
}

// The cache RepositoryGitPort keeps is thread_local, so nothing outside the thread that
// opened a handle can observe whether it is still open - a filesystem assertion after a
// close would pass whether or not the handle was actually released, because a loose-object
// test repository holds no lock either way.
//
// GetOpenCount only ever grows, so it can tell a handle that was silently reused (count
// unchanged) from one that was reopened fresh (count bumped) - useful as a regression guard
// against over-caching, but it cannot tell "the old entry was evicted" from "a second entry
// was opened alongside it": both look like one more open. GetLiveHandleCountForThisThread
// is the one that can, because a leaked entry keeps the live count up even after the new
// root's handle exists - that is what actually proves the fix below.

TEST_CASE("Reopening the same root after a close opens a fresh handle")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	REQUIRE(port.QueryStatus().HasValue());
	CHECK(port.GetOpenCount() == 1);

	port.SetRoot({});
	port.SetRoot(repo.GetPath());

	REQUIRE(port.QueryStatus().HasValue());
	CHECK(port.GetOpenCount() == 2);
}

TEST_CASE("Switching to a different root opens a fresh handle for it")
{
	TempRepo repoA;
	TempRepo repoB;
	Graphite::RepositoryGitPort port{repoA.GetPath()};

	REQUIRE(port.QueryStatus().HasValue());
	CHECK(port.GetOpenCount() == 1);

	port.SetRoot(repoB.GetPath());

	REQUIRE(port.QueryStatus().HasValue());
	CHECK(port.GetOpenCount() == 2);
}

TEST_CASE("Switching to a different root does not leave the old root's handle live")
{
	TempRepo repoA;
	TempRepo repoB;
	Graphite::RepositoryGitPort port{repoA.GetPath()};

	REQUIRE(port.QueryStatus().HasValue());

	port.SetRoot(repoB.GetPath());
	REQUIRE(port.QueryStatus().HasValue());

	// Two opens happened (repoA, then repoB), but only one handle should still be alive:
	// repoA's must have been evicted, not merely left behind alongside repoB's.
	CHECK(port.GetLiveHandleCountForThisThread() == 1);
}

TEST_CASE("Closing the workspace does not leave the last root's handle live")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	REQUIRE(port.QueryStatus().HasValue());
	REQUIRE(port.GetLiveHandleCountForThisThread() == 1);

	port.SetRoot({});

	// A query against an empty root cannot open anything, but it is still this thread's
	// next chance to sweep the entry the closed root left behind.
	CHECK_FALSE(port.QueryStatus().HasValue());
	CHECK(port.GetLiveHandleCountForThisThread() == 0);
}

TEST_CASE("RepositoryGitPort stages through the same handle it queries with")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	{
		std::ofstream file{repo.GetPath() / "a.txt"};
		file << "one\n";
	}

	REQUIRE(port.Stage({"a.txt"}).HasValue());

	const auto status = port.QueryStatus();
	REQUIRE(status.HasValue());
	REQUIRE(status.GetValue().Entries.size() == 1);
	CHECK(status.GetValue().Entries.front().Path == std::filesystem::path{"a.txt"});
	CHECK_FALSE(status.GetValue().Entries.front().IsUntracked);

	// One handle for this thread, reused by the write — the write must not open a second.
	CHECK(port.GetOpenCount() == 1);
}

TEST_CASE("The unstaged texts are the index against the disk")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	{
		std::ofstream file{repo.GetPath() / "a.txt", std::ios::binary};
		file << "staged\n";
	}

	REQUIRE(port.Stage({"a.txt"}).HasValue());

	{
		std::ofstream file{repo.GetPath() / "a.txt", std::ios::binary};
		file << "edited since\n";
	}

	const auto texts = port.ReadWorkingTreeTexts(Core::DiffTarget::WorkTreeToIndex, "a.txt", "a.txt");
	REQUIRE(texts.HasValue());

	CHECK(texts.GetValue().HasOld);
	CHECK(texts.GetValue().Old == "staged\n");
	CHECK(texts.GetValue().HasNew);
	CHECK(texts.GetValue().New == "edited since\n");
}

TEST_CASE("An untracked file has no older side to show")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	{
		std::ofstream file{repo.GetPath() / "new.txt", std::ios::binary};
		file << "brand new\n";
	}

	const auto texts = port.ReadWorkingTreeTexts(Core::DiffTarget::WorkTreeToIndex, "new.txt", "new.txt");
	REQUIRE(texts.HasValue());

	CHECK_FALSE(texts.GetValue().HasOld);
	CHECK(texts.GetValue().HasNew);
	CHECK(texts.GetValue().New == "brand new\n");
}

TEST_CASE("A file deleted from the working tree has no newer side to show")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	{
		std::ofstream file{repo.GetPath() / "a.txt", std::ios::binary};
		file << "staged\n";
	}

	REQUIRE(port.Stage({"a.txt"}).HasValue());

	std::error_code code;
	std::filesystem::remove(repo.GetPath() / "a.txt", code);
	REQUIRE_FALSE(code);

	const auto texts = port.ReadWorkingTreeTexts(Core::DiffTarget::WorkTreeToIndex, "a.txt", "a.txt");
	REQUIRE(texts.HasValue());

	CHECK(texts.GetValue().HasOld);
	CHECK_FALSE(texts.GetValue().HasNew);
}

TEST_CASE("A staged new file has an index side and no HEAD side")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	{
		std::ofstream file{repo.GetPath() / "a.txt", std::ios::binary};
		file << "one\n";
	}

	REQUIRE(port.Stage({"a.txt"}).HasValue());

	const auto bases = port.ReadBases(repo.GetPath() / "a.txt");
	REQUIRE(bases.HasValue());

	CHECK(bases.GetValue().Tracked);
	CHECK_FALSE(bases.GetValue().Head.has_value());
	CHECK(bases.GetValue().Index == std::optional<std::string>{"one\n"});
}

TEST_CASE("An untracked file is tracked with neither side")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	{
		std::ofstream file{repo.GetPath() / "a.txt", std::ios::binary};
		file << "one\n";
	}

	const auto bases = port.ReadBases(repo.GetPath() / "a.txt");
	REQUIRE(bases.HasValue());

	CHECK(bases.GetValue().Tracked);
	CHECK_FALSE(bases.GetValue().Head.has_value());
	CHECK_FALSE(bases.GetValue().Index.has_value());
}

TEST_CASE("An ignored file is not tracked")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	{
		std::ofstream ignore{repo.GetPath() / ".gitignore", std::ios::binary};
		ignore << "*.log\n";
		std::ofstream file{repo.GetPath() / "build.log", std::ios::binary};
		file << "noise\n";
	}

	const auto bases = port.ReadBases(repo.GetPath() / "build.log");
	REQUIRE(bases.HasValue());

	CHECK_FALSE(bases.GetValue().Tracked);
}

TEST_CASE("A file outside the repository is not tracked")
{
	TempRepo repo;
	TempRepo elsewhere;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	const auto bases = port.ReadBases(elsewhere.GetPath() / "a.txt");
	REQUIRE(bases.HasValue());

	CHECK_FALSE(bases.GetValue().Tracked);
}

TEST_CASE("An oversized staged file is tracked but unusable rather than a failure")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	{
		std::ofstream file{repo.GetPath() / "a.txt", std::ios::binary};
		const std::string bytes(static_cast<std::size_t>(Core::MaximumDiffTextSize) + 1, 'a');
		file << bytes << "\n";
	}

	REQUIRE(port.Stage({"a.txt"}).HasValue());

	const auto bases = port.ReadBases(repo.GetPath() / "a.txt");
	REQUIRE(bases.HasValue());

	CHECK(bases.GetValue().Tracked);
	CHECK(bases.GetValue().Unusable);
}

TEST_CASE("A file inside a submodule is not tracked")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	std::error_code code;
	std::filesystem::create_directories(repo.GetPath() / "vendor" / "lib", code);
	REQUIRE_FALSE(code);

	{
		std::ofstream file{repo.GetPath() / "vendor" / "lib" / "lib.cpp", std::ios::binary};
		file << "one\n";
	}

	// The gitlink `git submodule add` leaves in the index: mode 160000, naming a commit of
	// the submodule's own repository.
	{
		git_repository* raw = nullptr;
		REQUIRE(git_repository_open(&raw, Core::ToUtf8(repo.GetPath()).c_str()) == 0);

		git_index* index = nullptr;
		REQUIRE(git_repository_index(&index, raw) == 0);

		git_index_entry entry{};
		entry.path = "vendor/lib";
		entry.mode = GIT_FILEMODE_COMMIT;
		REQUIRE(git_oid_fromstr(&entry.id, "0123456789abcdef0123456789abcdef01234567") == 0);

		REQUIRE(git_index_add(index, &entry) == 0);
		REQUIRE(git_index_write(index) == 0);

		git_index_free(index);
		git_repository_free(raw);
	}

	const auto bases = port.ReadBases(repo.GetPath() / "vendor" / "lib" / "lib.cpp");
	REQUIRE(bases.HasValue());

	CHECK_FALSE(bases.GetValue().Tracked);
}

TEST_CASE("A file read with no repository open at all is not tracked")
{
	TempDirectory plain;
	Graphite::RepositoryGitPort port{plain.GetPath()};

	const auto bases = port.ReadBases(plain.GetPath() / "a.txt");
	REQUIRE(bases.HasValue());

	CHECK_FALSE(bases.GetValue().Tracked);
}

TEST_CASE("The port initialises a repository at its root and then answers from it")
{
	TempDirectory folder;
	Graphite::RepositoryGitPort port{folder.GetPath()};

	CHECK(port.QueryStatus().GetError().Code == Core::ErrorCode::NotFound);

	REQUIRE(port.Init().HasValue());

	// The failed open above was not cached, so this one finds the new repository.
	auto head = port.QueryHead();
	REQUIRE(head.HasValue());
	CHECK(head.GetValue().Kind == Core::HeadKind::Unborn);
	CHECK(port.QueryStatus().HasValue());
}

TEST_CASE("The port has nothing to initialise with no folder open")
{
	Graphite::RepositoryGitPort port{std::filesystem::path{}};

	auto result = port.Init();
	REQUIRE_FALSE(result.HasValue());
	CHECK(result.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("A folder inside a repository reads the repository's head")
{
	TempRepo repo;
	std::filesystem::create_directories(repo.GetPath() / "sub");

	Graphite::RepositoryGitPort port{repo.GetPath() / "sub"};

	auto head = port.QueryHead();
	REQUIRE(head.HasValue());
	CHECK(head.GetValue().Kind == Core::HeadKind::Unborn);
}

namespace {

	// A repository whose .git is there but which libgit2 refuses to open: a format version
	// it does not understand fails Open the same way dubious ownership does - with an error
	// that is not NotFound - and needs no second account or special drive to reproduce.
	void MakeUnopenable(const std::filesystem::path& root)
	{
		std::ofstream config{root / ".git" / "config", std::ios::binary | std::ios::trunc};
		config << "[core]\n\trepositoryformatversion = 99\n";
	}

	std::string ReadAll(const std::filesystem::path& path)
	{
		std::ifstream in{path, std::ios::binary};
		return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
	}
}

TEST_CASE("A repository that cannot be opened is reported as its own failure, not as missing")
{
	TempRepo repo;
	MakeUnopenable(repo.GetPath());

	Graphite::RepositoryGitPort port{repo.GetPath()};

	auto status = port.QueryStatus();
	REQUIRE_FALSE(status.HasValue());
	CHECK(status.GetError().Code != Core::ErrorCode::NotFound);

	auto head = port.QueryHead();
	REQUIRE_FALSE(head.HasValue());
	CHECK(head.GetError().Code != Core::ErrorCode::NotFound);
}

TEST_CASE("The port refuses to initialise over a repository it cannot open")
{
	TempRepo repo;
	MakeUnopenable(repo.GetPath());
	const std::string before = ReadAll(repo.GetPath() / ".git" / "config");

	Graphite::RepositoryGitPort port{repo.GetPath()};

	auto result = port.Init();
	REQUIRE_FALSE(result.HasValue());
	CHECK(result.GetError().Code != Core::ErrorCode::NotFound);
	CHECK(ReadAll(repo.GetPath() / ".git" / "config") == before);
}

TEST_CASE("The port refuses to initialise a folder that is already inside a repository")
{
	TempRepo repo;
	std::filesystem::create_directories(repo.GetPath() / "sub");

	Graphite::RepositoryGitPort port{repo.GetPath() / "sub"};

	auto result = port.Init();
	REQUIRE_FALSE(result.HasValue());
	CHECK(result.GetError().Code == Core::ErrorCode::InvalidArgument);
	CHECK_FALSE(std::filesystem::exists(repo.GetPath() / "sub" / ".git"));
}

TEST_CASE("The port refuses to initialise a folder inside a repository it cannot open")
{
	TempRepo repo;
	MakeUnopenable(repo.GetPath());
	std::filesystem::create_directories(repo.GetPath() / "sub");

	Graphite::RepositoryGitPort port{repo.GetPath() / "sub"};

	auto result = port.Init();
	REQUIRE_FALSE(result.HasValue());
	CHECK(result.GetError().Code != Core::ErrorCode::NotFound);
	CHECK_FALSE(std::filesystem::exists(repo.GetPath() / "sub" / ".git"));
}
