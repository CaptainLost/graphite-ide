#include <doctest/doctest.h>

#include "Git/RepositoryGitPort.h"

#include "Core/Fs/FileSystem.h"

#include <git2.h>

#include <atomic>
#include <filesystem>
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
