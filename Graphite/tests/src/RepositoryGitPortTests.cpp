#include <doctest/doctest.h>

#include "Git/RepositoryGitPort.h"

#include "Core/Fs/FileSystem.h"

#include <git2.h>

#include <atomic>
#include <filesystem>
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

			m_Path = std::filesystem::temp_directory_path() /
					 ("graphite-tests-repogitport-" + std::to_string(counter.fetch_add(1)));

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
// test repository holds no lock either way. GetOpenCount is the observable that can
// actually tell the two apart: a handle reused silently leaves the count unchanged, while a
// fresh Open() bumps it.

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
