#include "corepch.h"

#include "Core/FileSystem/DirectoryWatcher.h"

#include "TempDirectory.h"

#include <doctest/doctest.h>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

#include <atomic>
#include <chrono>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace Core;

namespace {
	std::vector<std::filesystem::path> WaitForChanges(DirectoryWatcher& watcher,
													  std::chrono::seconds timeout)
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;

		while (std::chrono::steady_clock::now() < deadline)
		{
			if (auto changes = watcher.TakeSettled(); !changes.Directories.empty())
			{
				return changes.Directories;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds{20});
		}

		return {};
	}
}

TEST_CASE("Starting a watcher on a missing directory fails")
{
	const auto watcher = DirectoryWatcher::Start("C:/no/such/directory/anywhere");

	CHECK_FALSE(watcher.HasValue());
}

TEST_CASE("A file created in the watched directory is reported")
{
	TempDirectory temp;

	auto watcher = DirectoryWatcher::Start(temp.GetPath());
	REQUIRE(watcher.HasValue());

	temp.MakeFile("created.txt", "x");

	const auto changes = WaitForChanges(*watcher.GetValue(), std::chrono::seconds{5});

	REQUIRE_FALSE(changes.empty());
	CHECK(changes[0] == temp.GetPath());
}

namespace {
	class SingleCpuContention
	{
	public:
		SingleCpuContention()
		{
#ifdef CORE_PLATFORM_WINDOWS
			DWORD_PTR processMask = 0;
			DWORD_PTR systemMask = 0;

			if (::GetProcessAffinityMask(::GetCurrentProcess(), &processMask, &systemMask) &&
				processMask != 0)
			{
				m_PreviousMask = processMask;

				// The lowest CPU already permitted. Written as ~x + 1 rather than -x
				// because a unary minus on an unsigned type is its own compiler warning,
				// and warnings are fatal here.
				const DWORD_PTR single = processMask & (~processMask + 1);

				::SetProcessAffinityMask(::GetCurrentProcess(), single);
			}
#endif

			// One spinner is enough once there is only one CPU to spin on, and the
			// watcher's worker still gets scheduled — it is competing for slices, not
			// locked out. Everything it has to do before the watch is armed is
			// microseconds of work against the test's five-second budget.
			m_Spinner = std::thread{[this] {
				// Read through an atomic so the compiler cannot
				// decide the loop has no effect and delete it.
				while (!m_Stop.load(std::memory_order_relaxed))
				{
				}
			}};
		}

		~SingleCpuContention()
		{
			m_Stop.store(true, std::memory_order_relaxed);
			m_Spinner.join();

#ifdef CORE_PLATFORM_WINDOWS
			// Restored, or every later test in this process would run on one core.
			if (m_PreviousMask != 0)
			{
				::SetProcessAffinityMask(::GetCurrentProcess(), m_PreviousMask);
			}
#endif
		}

		SingleCpuContention(const SingleCpuContention&) = delete;
		SingleCpuContention& operator=(const SingleCpuContention&) = delete;

	private:
		std::atomic<bool> m_Stop{false};
		std::thread m_Spinner;

#ifdef CORE_PLATFORM_WINDOWS
		DWORD_PTR m_PreviousMask = 0;
#endif
	};
}

// No comma in the name on purpose: doctest's -tc filter treats one as a separator
// between patterns, so a name containing it cannot be selected on its own.
TEST_CASE("A file created the instant Start returns is reported on every start")
{
	const SingleCpuContention contention;

	// The case above already covers this, but only by accident of timing: it starts one
	// watcher, and when Start returned before the watch was armed it still passed six
	// times in seven, so a regression shows up there as an occasional red run nobody can
	// reproduce.
	//
	// The count is not arithmetic from that one-in-seven — under the contention above the
	// per-round loss rate is far higher than an idle single start, and the honest
	// justification is the measurement rather than a calculation. With the fix reverted
	// this case failed 10 runs out of 10; with it in place it passed 10 out of 10.
	// Re-measure the same way before trimming this, because the number is only worth what
	// that experiment says it is.
	constexpr int kRounds = 20;

	for (int round = 0; round < kRounds; ++round)
	{
		CAPTURE(round);

		TempDirectory temp;

		auto watcher = DirectoryWatcher::Start(temp.GetPath());
		REQUIRE(watcher.HasValue());

		// Nothing at all between these two lines on purpose. That gap is the defect: the
		// watch has to be armed by the time Start hands the watcher back, not shortly
		// afterwards. Do not "stabilise" this with a sleep — it would test nothing.
		temp.MakeFile("created.txt", "x");

		const auto changes = WaitForChanges(*watcher.GetValue(), std::chrono::seconds{5});

		REQUIRE_FALSE(changes.empty());
		CHECK(changes[0] == temp.GetPath());
	}
}

TEST_CASE("A file created in a subdirectory reports that subdirectory")
{
	TempDirectory temp;
	temp.MakeDirectory("nested");

	auto watcher = DirectoryWatcher::Start(temp.GetPath());
	REQUIRE(watcher.HasValue());

	temp.MakeFile("nested/created.txt", "x");

	const auto changes = WaitForChanges(*watcher.GetValue(), std::chrono::seconds{5});

	REQUIRE_FALSE(changes.empty());

	// The subtree is watched, and what is reported is the directory holding the changed
	// entry — that is the node the tree will refresh.
	CHECK(changes[0] == temp.GetPath() / "nested");
}

TEST_CASE("Changes inside a .git directory are not reported")
{
	TempDirectory temp;
	temp.MakeDirectory(".git");

	auto watcher = DirectoryWatcher::Start(temp.GetPath());
	REQUIRE(watcher.HasValue());

	// Written directly inside .git rather than deeper: creating a directory chain under it
	// makes Windows report .git itself, which passes the filter on purpose - that is how
	// `git init` becomes visible, and it is the next test's subject, not this one's.
	temp.MakeFile(".git/COMMIT_EDITMSG", "a commit message");

	// Git rewrites files under .git on every operation, and refreshing for each one
	// would be a storm of no-op work. The few that say what the repository is are the
	// exception, and they are reported apart from the tree - see the repository-state
	// tests below.
	CHECK(WaitForChanges(*watcher.GetValue(), std::chrono::seconds{2}).empty());
}

TEST_CASE("The .git directory appearing is reported")
{
	TempDirectory temp;

	auto watcher = DirectoryWatcher::Start(temp.GetPath());
	REQUIRE(watcher.HasValue());

	// How `git init` in a terminal becomes visible: the directory itself passes the
	// filter that its contents do not.
	temp.MakeDirectory(".git");

	const auto changes = WaitForChanges(*watcher.GetValue(), std::chrono::seconds{5});

	REQUIRE_FALSE(changes.empty());
	CHECK(changes[0] == temp.GetPath());
}

TEST_CASE("Destroying a watcher with no changes pending returns promptly")
{
	TempDirectory temp;

	const auto before = std::chrono::steady_clock::now();

	{
		auto watcher = DirectoryWatcher::Start(temp.GetPath());
		REQUIRE(watcher.HasValue());
	}

	// The thread blocks in ReadDirectoryChangesW waiting for input that never comes, so
	// without an explicit wake-up the destructor would hang until something happened to
	// the directory. This test is the whole reason for the stop event.
	CHECK(std::chrono::steady_clock::now() - before < std::chrono::seconds{2});
}

TEST_CASE("A change to the repository's exclude file is reported as a root change")
{
	TempDirectory temp;
	temp.MakeDirectory(".git/info");

	auto watcher = DirectoryWatcher::Start(temp.GetPath());
	REQUIRE(watcher.HasValue());

	temp.MakeFile(".git/info/exclude", "build/");

	const auto changes = WaitForChanges(*watcher.GetValue(), std::chrono::seconds{5});

	REQUIRE_FALSE(changes.empty());

	// The root, not the directory holding the file: these rules apply to the whole tree,
	// and refreshing the root is what re-asks every loaded node.
	CHECK(changes[0] == temp.GetPath());
}

TEST_CASE("A watcher stops running once its directory is deleted")
{
	TempDirectory temp;

	auto watcher = DirectoryWatcher::Start(temp.GetPath());
	REQUIRE(watcher.HasValue());
	REQUIRE(watcher.GetValue()->IsRunning());

	// Deleting the watched directory is what kills the thread in practice, and the whole
	// point of IsRunning is that the consumer cannot tell this apart from a quiet disk
	// any other way — TakeSettled simply returns nothing, for ever.
	std::error_code code;
	std::filesystem::remove_all(temp.GetPath(), code);
	REQUIRE_FALSE(code);

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
	bool stopped = false;

	while (!stopped && std::chrono::steady_clock::now() < deadline)
	{
		stopped = !watcher.GetValue()->IsRunning();

		if (!stopped)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds{20});
		}
	}

	CHECK(stopped);
}

namespace {
	std::optional<std::filesystem::path> WaitForMove(DirectoryWatcher& watcher,
													 std::chrono::seconds timeout)
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;

		while (std::chrono::steady_clock::now() < deadline)
		{
			if (auto changes = watcher.TakeSettled(); changes.MovedTo)
			{
				return changes.MovedTo;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds{20});
		}

		return std::nullopt;
	}
}

TEST_CASE("A watched directory that is renamed is reported at its new path")
{
	TempDirectory temp;
	const std::filesystem::path watched = temp.MakeDirectory("before");

	auto watcher = DirectoryWatcher::Start(watched);
	REQUIRE(watcher.HasValue());

	std::error_code code;
	std::filesystem::rename(watched, temp.GetPath() / "after", code);
	REQUIRE_FALSE(code);

	const auto moved = WaitForMove(*watcher.GetValue(), std::chrono::seconds{10});

	REQUIRE(moved.has_value());
	CHECK(*moved == temp.GetPath() / "after");
}

TEST_CASE("A watched directory that is moved elsewhere is reported at its new path")
{
	TempDirectory temp;
	const std::filesystem::path watched = temp.MakeDirectory("origin/watched");
	temp.MakeDirectory("destination");

	auto watcher = DirectoryWatcher::Start(watched);
	REQUIRE(watcher.HasValue());

	// Moving and renaming are the same operation to the filesystem, but the resulting
	// path differs in more than its last component, so both are worth pinning.
	std::error_code code;
	std::filesystem::rename(watched, temp.GetPath() / "destination" / "watched", code);
	REQUIRE_FALSE(code);

	const auto moved = WaitForMove(*watcher.GetValue(), std::chrono::seconds{10});

	REQUIRE(moved.has_value());
	CHECK(*moved == temp.GetPath() / "destination" / "watched");
}

TEST_CASE("A directory that stays put is never reported as moved")
{
	TempDirectory temp;
	temp.MakeFile("a.txt", "x");

	auto watcher = DirectoryWatcher::Start(temp.GetPath());
	REQUIRE(watcher.HasValue());

	// Ordinary activity must not be mistaken for a move — and neither must the case
	// difference between the path the caller passed and the one the filesystem stores,
	// which is why the baseline comes from the handle rather than from that argument.
	temp.MakeFile("b.txt", "x");

	// Five seconds against a two-second interval, so at least two polls run even on a
	// loaded machine. At three the test could pass by simply not having polled.
	CHECK_FALSE(WaitForMove(*watcher.GetValue(), std::chrono::seconds{5}).has_value());
}

TEST_CASE("A move is reported once, not on every poll")
{
	TempDirectory temp;
	const std::filesystem::path watched = temp.MakeDirectory("before");

	auto watcher = DirectoryWatcher::Start(watched);
	REQUIRE(watcher.HasValue());

	std::error_code code;
	std::filesystem::rename(watched, temp.GetPath() / "after", code);
	REQUIRE_FALSE(code);

	REQUIRE(WaitForMove(*watcher.GetValue(), std::chrono::seconds{10}).has_value());

	// The consumer acts on it by re-pointing the workspace; being told again afterwards
	// would make it do that work over and over.
	// Five seconds against a two-second interval, so at least two polls run even on a
	// loaded machine. At three the test could pass by simply not having polled.
	CHECK_FALSE(WaitForMove(*watcher.GetValue(), std::chrono::seconds{5}).has_value());
}

TEST_CASE("A move is noticed even while the directory is busy")
{
	TempDirectory temp;
	const std::filesystem::path watched = temp.MakeDirectory("busy");

	auto watcher = DirectoryWatcher::Start(watched);
	REQUIRE(watcher.HasValue());

	std::error_code code;
	std::filesystem::rename(watched, temp.GetPath() / "busy-moved", code);
	REQUIRE_FALSE(code);

	// Writing faster than kMoveCheckInterval. The check used to run only when the wait
	// expired, and every notification restarts that wait — so a directory under a build
	// would never be checked at all, which is exactly when a move is most likely and least affordable to miss.
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
	std::optional<std::filesystem::path> moved;

	for (int i = 0; !moved && std::chrono::steady_clock::now() < deadline; ++i)
	{
		std::ofstream{temp.GetPath() / "busy-moved" / ("churn" + std::to_string(i) + ".txt")}
			<< "x";

		std::this_thread::sleep_for(std::chrono::milliseconds{100});

		moved = watcher.GetValue()->TakeSettled().MovedTo;
	}

	REQUIRE(moved.has_value());
	CHECK(*moved == temp.GetPath() / "busy-moved");
}

TEST_CASE("The files that say what a repository is are named as such")
{
	// What a commit, a checkout, a fetch or a rebase rewrites. Few, and each changes
	// exactly when the answer to "what does this repository contain" changes.
	CHECK(Core::NamesRepositoryState(std::filesystem::path{".git"} / "HEAD"));
	CHECK(Core::NamesRepositoryState(std::filesystem::path{".git"} / "index"));
	CHECK(Core::NamesRepositoryState(std::filesystem::path{".git"} / "packed-refs"));
	CHECK(Core::NamesRepositoryState(std::filesystem::path{".git"} / "refs" / "heads" / "main"));
	CHECK(Core::NamesRepositoryState(std::filesystem::path{".git"} / "refs" / "remotes" /
									 "origin" / "main"));
}

TEST_CASE("The rest of a repository's private directory says nothing")
{
	// Git rewrites these constantly and none of them changes what is on screen. Letting
	// them through would be the storm of no-op work the .git filter exists to prevent.
	CHECK_FALSE(Core::NamesRepositoryState(std::filesystem::path{".git"} / "objects" / "ab" /
										   "cdef"));
	CHECK_FALSE(Core::NamesRepositoryState(std::filesystem::path{".git"} / "index.lock"));
	CHECK_FALSE(Core::NamesRepositoryState(std::filesystem::path{".git"} / "COMMIT_EDITMSG"));
	CHECK_FALSE(Core::NamesRepositoryState(std::filesystem::path{".git"} / "logs" / "HEAD"));
}

TEST_CASE("A file of the same name outside the repository's directory says nothing")
{
	// A project with its own HEAD or index file is a project with two ordinary files, and
	// editing them is an edit to the working tree like any other.
	CHECK_FALSE(Core::NamesRepositoryState("HEAD"));
	CHECK_FALSE(Core::NamesRepositoryState(std::filesystem::path{"src"} / "index"));
	CHECK_FALSE(Core::NamesRepositoryState(std::filesystem::path{"refs"} / "heads" / "main"));
}

TEST_CASE("A commit made outside the application is reported as a repository change")
{
	TempDirectory temp;
	temp.MakeDirectory(".git/refs/heads");

	auto watcher = DirectoryWatcher::Start(temp.GetPath());
	REQUIRE(watcher.HasValue());

	// What `git commit` in a terminal leaves behind: the branch points somewhere new.
	temp.MakeFile(".git/refs/heads/main", "0123456789abcdef0123456789abcdef01234567\n");

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
	bool reported = false;

	while (std::chrono::steady_clock::now() < deadline && !reported)
	{
		const DirectoryWatcher::Changes changes = watcher.GetValue()->TakeSettled();
		reported = changes.RepositoryState;

		// The file tree has nothing to redraw for this: no directory of the working tree
		// changed, and refreshing one would be work with no reason behind it.
		CHECK(changes.Directories.empty());

		if (!reported)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds{50});
		}
	}

	CHECK(reported);
}
