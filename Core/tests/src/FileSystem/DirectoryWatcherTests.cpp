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
	constexpr std::chrono::milliseconds kPollInterval{20};

	constexpr std::chrono::seconds kChangeTimeout{5};
	constexpr std::chrono::seconds kMoveTimeout{10};

	// Five seconds against the watcher's two-second move check, so that at least two checks
	// run even on a loaded machine. At three seconds the test could pass without having
	// checked at all.
	constexpr std::chrono::seconds kNoMoveTimeout{5};

	std::vector<std::filesystem::path> WaitForChangedDirectories(DirectoryWatcher& watcher,
																 std::chrono::seconds timeout)
	{
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + timeout;

		while (std::chrono::steady_clock::now() < deadline)
		{
			const DirectoryWatcher::Changes changes = watcher.TakeSettled();

			if (!changes.Directories.empty())
			{
				return changes.Directories;
			}

			std::this_thread::sleep_for(kPollInterval);
		}

		return {};
	}
}

TEST_CASE("Starting a watcher on a missing directory fails")
{
	const Result<Scope<DirectoryWatcher>> started = DirectoryWatcher::Start("C:/no/such/directory/anywhere");

	CHECK_FALSE(started.HasValue());
}

TEST_CASE("A file created in the watched directory is reported")
{
	TempDirectory temporaryDirectory;

	Result<Scope<DirectoryWatcher>> started = DirectoryWatcher::Start(temporaryDirectory.GetPath());
	REQUIRE(started.HasValue());
	DirectoryWatcher& watcher = *started.GetValue();

	temporaryDirectory.MakeFile("created.txt", "x");

	const std::vector<std::filesystem::path> changedDirectories = WaitForChangedDirectories(watcher, kChangeTimeout);

	REQUIRE_FALSE(changedDirectories.empty());
	CHECK(changedDirectories[0] == temporaryDirectory.GetPath());
}

namespace {
#ifdef CORE_PLATFORM_WINDOWS
	DWORD_PTR LowestSetBitOf(DWORD_PTR mask)
	{
		// ~mask + 1 rather than -mask, because a unary minus on an unsigned type is its own
		// compiler warning, and warnings are fatal here.
		return mask & (~mask + 1);
	}
#endif

	class SingleCpuContention
	{
	public:
		SingleCpuContention()
		{
#ifdef CORE_PLATFORM_WINDOWS
			DWORD_PTR processMask = 0;
			DWORD_PTR systemMask = 0;

			const BOOL masksRead = ::GetProcessAffinityMask(::GetCurrentProcess(), &processMask, &systemMask);

			if (masksRead && processMask != 0)
			{
				m_AffinityMaskToRestore = processMask;

				const DWORD_PTR lowestPermittedCpu = LowestSetBitOf(processMask);

				::SetProcessAffinityMask(::GetCurrentProcess(), lowestPermittedCpu);
			}
#endif

			m_Spinner = std::thread{[this] { SpinUntilStopped(); }};
		}

		~SingleCpuContention()
		{
			m_Stop.store(true, std::memory_order_relaxed);
			m_Spinner.join();

#ifdef CORE_PLATFORM_WINDOWS
			if (m_AffinityMaskToRestore != 0)
			{
				::SetProcessAffinityMask(::GetCurrentProcess(), m_AffinityMaskToRestore);
			}
#endif
		}

		SingleCpuContention(const SingleCpuContention&) = delete;
		SingleCpuContention& operator=(const SingleCpuContention&) = delete;

	private:
		// One spinner is enough on a single CPU, and the watcher's worker still gets scheduled:
		// it competes for time slices rather than being locked out, and arming the watch is
		// microseconds of work against the test's five-second budget.
		void SpinUntilStopped()
		{
			// Read through an atomic so that the compiler cannot decide the loop has no effect
			// and delete it.
			while (!m_Stop.load(std::memory_order_relaxed))
			{
			}
		}

		std::atomic<bool> m_Stop{false};
		std::thread m_Spinner;

#ifdef CORE_PLATFORM_WINDOWS
		DWORD_PTR m_AffinityMaskToRestore = 0;
#endif
	};
}

// No comma in the name on purpose: doctest's -tc filter treats one as a separator between
// patterns, so a name containing it cannot be selected on its own.
TEST_CASE("A file created the instant Start returns is reported on every start")
{
	const SingleCpuContention contention;

	// A measured number, not a calculated one: under this contention the case fails on 10 runs out of 10 with a Start that returns before the watch is armed, and passes on 10 runs out of 10 with one that waits for it. Measure the same way again before trimming it.
	constexpr int kMeasuredRounds = 20;

	for (int round = 0; round < kMeasuredRounds; ++round)
	{
		CAPTURE(round);

		TempDirectory temporaryDirectory;

		Result<Scope<DirectoryWatcher>> started = DirectoryWatcher::Start(temporaryDirectory.GetPath());
		REQUIRE(started.HasValue());

		// Nothing may go between Start and this write, a sleep least of all: the watch has to
		// be armed by the time Start returns, and any delay here would let a late arming pass.
		temporaryDirectory.MakeFile("created.txt", "x");

		DirectoryWatcher& watcher = *started.GetValue();
		const std::vector<std::filesystem::path> changedDirectories = WaitForChangedDirectories(watcher, kChangeTimeout);

		REQUIRE_FALSE(changedDirectories.empty());
		CHECK(changedDirectories[0] == temporaryDirectory.GetPath());
	}
}

TEST_CASE("A file created in a subdirectory reports the subdirectory holding it since that is the node the tree refreshes")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeDirectory("nested");

	Result<Scope<DirectoryWatcher>> started = DirectoryWatcher::Start(temporaryDirectory.GetPath());
	REQUIRE(started.HasValue());
	DirectoryWatcher& watcher = *started.GetValue();

	temporaryDirectory.MakeFile("nested/created.txt", "x");

	const std::vector<std::filesystem::path> changedDirectories = WaitForChangedDirectories(watcher, kChangeTimeout);

	REQUIRE_FALSE(changedDirectories.empty());

	CHECK(changedDirectories[0] == temporaryDirectory.GetPath() / "nested");
}

TEST_CASE("Changes inside a .git directory are not reported since git rewrites them on every operation")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeDirectory(".git");

	Result<Scope<DirectoryWatcher>> started = DirectoryWatcher::Start(temporaryDirectory.GetPath());
	REQUIRE(started.HasValue());
	DirectoryWatcher& watcher = *started.GetValue();

	// Written directly inside .git: creating a directory under it would make Windows report
	// .git itself, which is let through on purpose so that a git init shows.
	temporaryDirectory.MakeFile(".git/COMMIT_EDITMSG", "a commit message");

	constexpr std::chrono::seconds kTimeToReportNothing{2};

	CHECK(WaitForChangedDirectories(watcher, kTimeToReportNothing).empty());
}

TEST_CASE("The .git directory appearing is reported which is how a git init in a terminal becomes visible")
{
	TempDirectory temporaryDirectory;

	Result<Scope<DirectoryWatcher>> started = DirectoryWatcher::Start(temporaryDirectory.GetPath());
	REQUIRE(started.HasValue());
	DirectoryWatcher& watcher = *started.GetValue();

	temporaryDirectory.MakeDirectory(".git");

	const std::vector<std::filesystem::path> changedDirectories = WaitForChangedDirectories(watcher, kChangeTimeout);

	REQUIRE_FALSE(changedDirectories.empty());
	CHECK(changedDirectories[0] == temporaryDirectory.GetPath());
}

TEST_CASE("Destroying a watcher with no changes pending returns promptly because the stop event wakes its waiting worker")
{
	TempDirectory temporaryDirectory;

	const std::chrono::steady_clock::time_point before = std::chrono::steady_clock::now();

	{
		Result<Scope<DirectoryWatcher>> started = DirectoryWatcher::Start(temporaryDirectory.GetPath());
		REQUIRE(started.HasValue());
	}

	constexpr std::chrono::seconds kPromptly{2};

	CHECK(std::chrono::steady_clock::now() - before < kPromptly);
}

TEST_CASE("A change to the repository's exclude file is reported as a change to the root since its rules apply to the whole tree")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeDirectory(".git/info");

	Result<Scope<DirectoryWatcher>> started = DirectoryWatcher::Start(temporaryDirectory.GetPath());
	REQUIRE(started.HasValue());
	DirectoryWatcher& watcher = *started.GetValue();

	temporaryDirectory.MakeFile(".git/info/exclude", "build/");

	const std::vector<std::filesystem::path> changedDirectories = WaitForChangedDirectories(watcher, kChangeTimeout);

	REQUIRE_FALSE(changedDirectories.empty());

	CHECK(changedDirectories[0] == temporaryDirectory.GetPath());
}

namespace {
	bool WaitUntilStopped(const DirectoryWatcher& watcher, std::chrono::seconds timeout)
	{
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + timeout;

		while (std::chrono::steady_clock::now() < deadline)
		{
			if (!watcher.IsRunning())
			{
				return true;
			}

			std::this_thread::sleep_for(kPollInterval);
		}

		return false;
	}
}

TEST_CASE("A watcher stops running once its directory is deleted which only IsRunning tells apart from a quiet disk")
{
	TempDirectory temporaryDirectory;

	Result<Scope<DirectoryWatcher>> started = DirectoryWatcher::Start(temporaryDirectory.GetPath());
	REQUIRE(started.HasValue());
	const DirectoryWatcher& watcher = *started.GetValue();
	REQUIRE(watcher.IsRunning());

	std::error_code removeError;
	std::filesystem::remove_all(temporaryDirectory.GetPath(), removeError);
	REQUIRE_FALSE(removeError);

	constexpr std::chrono::seconds kStopTimeout{5};

	CHECK(WaitUntilStopped(watcher, kStopTimeout));
}

namespace {
	std::optional<std::filesystem::path> WaitForMove(DirectoryWatcher& watcher,
													 std::chrono::seconds timeout)
	{
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + timeout;

		while (std::chrono::steady_clock::now() < deadline)
		{
			const DirectoryWatcher::Changes changes = watcher.TakeSettled();

			if (changes.MovedTo)
			{
				return changes.MovedTo;
			}

			std::this_thread::sleep_for(kPollInterval);
		}

		return std::nullopt;
	}
}

TEST_CASE("A watched directory that is renamed is reported at its new path")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path watched = temporaryDirectory.MakeDirectory("before");

	Result<Scope<DirectoryWatcher>> started = DirectoryWatcher::Start(watched);
	REQUIRE(started.HasValue());
	DirectoryWatcher& watcher = *started.GetValue();

	std::error_code renameError;
	std::filesystem::rename(watched, temporaryDirectory.GetPath() / "after", renameError);
	REQUIRE_FALSE(renameError);

	const std::optional<std::filesystem::path> movedTo = WaitForMove(watcher, kMoveTimeout);

	REQUIRE(movedTo.has_value());
	CHECK(*movedTo == temporaryDirectory.GetPath() / "after");
}

TEST_CASE("A watched directory moved under another parent is reported at its new path which differs in more than its last component")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path watched = temporaryDirectory.MakeDirectory("origin/watched");
	temporaryDirectory.MakeDirectory("destination");

	Result<Scope<DirectoryWatcher>> started = DirectoryWatcher::Start(watched);
	REQUIRE(started.HasValue());
	DirectoryWatcher& watcher = *started.GetValue();

	std::error_code renameError;
	std::filesystem::rename(watched, temporaryDirectory.GetPath() / "destination" / "watched", renameError);
	REQUIRE_FALSE(renameError);

	const std::optional<std::filesystem::path> movedTo = WaitForMove(watcher, kMoveTimeout);

	REQUIRE(movedTo.has_value());
	CHECK(*movedTo == temporaryDirectory.GetPath() / "destination" / "watched");
}

TEST_CASE("A directory that stays put and sees ordinary activity is never reported as moved")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("a.txt", "x");

	Result<Scope<DirectoryWatcher>> started = DirectoryWatcher::Start(temporaryDirectory.GetPath());
	REQUIRE(started.HasValue());
	DirectoryWatcher& watcher = *started.GetValue();

	temporaryDirectory.MakeFile("b.txt", "x");

	CHECK_FALSE(WaitForMove(watcher, kNoMoveTimeout).has_value());
}

TEST_CASE("A move is reported once and not on every poll so the consumer re-points the workspace only once")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path watched = temporaryDirectory.MakeDirectory("before");

	Result<Scope<DirectoryWatcher>> started = DirectoryWatcher::Start(watched);
	REQUIRE(started.HasValue());
	DirectoryWatcher& watcher = *started.GetValue();

	std::error_code renameError;
	std::filesystem::rename(watched, temporaryDirectory.GetPath() / "after", renameError);
	REQUIRE_FALSE(renameError);

	REQUIRE(WaitForMove(watcher, kMoveTimeout).has_value());

	CHECK_FALSE(WaitForMove(watcher, kNoMoveTimeout).has_value());
}

TEST_CASE("A move is noticed even while the directory is written to faster than the move check interval")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path watched = temporaryDirectory.MakeDirectory("busy");

	Result<Scope<DirectoryWatcher>> started = DirectoryWatcher::Start(watched);
	REQUIRE(started.HasValue());
	DirectoryWatcher& watcher = *started.GetValue();

	std::error_code renameError;
	std::filesystem::rename(watched, temporaryDirectory.GetPath() / "busy-moved", renameError);
	REQUIRE_FALSE(renameError);

	constexpr std::chrono::milliseconds kWriteInterval{100};

	const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kMoveTimeout;
	std::optional<std::filesystem::path> movedTo;

	for (int churnFileNumber = 0; !movedTo && std::chrono::steady_clock::now() < deadline; ++churnFileNumber)
	{
		std::ofstream{temporaryDirectory.GetPath() / "busy-moved" / ("churn" + std::to_string(churnFileNumber) + ".txt")}
			<< "x";

		std::this_thread::sleep_for(kWriteInterval);

		movedTo = watcher.TakeSettled().MovedTo;
	}

	REQUIRE(movedTo.has_value());
	CHECK(*movedTo == temporaryDirectory.GetPath() / "busy-moved");
}

TEST_CASE("The files a commit or checkout or fetch or rebase rewrites are named as repository state")
{
	CHECK(Core::NamesRepositoryState(std::filesystem::path{".git"} / "HEAD"));
	CHECK(Core::NamesRepositoryState(std::filesystem::path{".git"} / "index"));
	CHECK(Core::NamesRepositoryState(std::filesystem::path{".git"} / "packed-refs"));
	CHECK(Core::NamesRepositoryState(std::filesystem::path{".git"} / "refs" / "heads" / "main"));
	CHECK(Core::NamesRepositoryState(std::filesystem::path{".git"} / "refs" / "remotes" /
									 "origin" / "main"));
}

TEST_CASE("The rest of a repository's private directory says nothing since git rewrites it constantly without changing what is on screen")
{
	CHECK_FALSE(Core::NamesRepositoryState(std::filesystem::path{".git"} / "objects" / "ab" /
										   "cdef"));
	CHECK_FALSE(Core::NamesRepositoryState(std::filesystem::path{".git"} / "index.lock"));
	CHECK_FALSE(Core::NamesRepositoryState(std::filesystem::path{".git"} / "COMMIT_EDITMSG"));
	CHECK_FALSE(Core::NamesRepositoryState(std::filesystem::path{".git"} / "logs" / "HEAD"));
}

TEST_CASE("A folder's rules file, the repository's exclude file and its config are named as ignore rules")
{
	CHECK(Core::NamesIgnoreRules(".gitignore"));
	CHECK(Core::NamesIgnoreRules(std::filesystem::path{"src"} / "deep" / ".gitignore"));
	CHECK(Core::NamesIgnoreRules(std::filesystem::path{".git"} / "info" / "exclude"));
	CHECK(Core::NamesIgnoreRules(std::filesystem::path{".git"} / "config"));
}

TEST_CASE("Other files, and a rules file inside the repository's private directory, are not named as ignore rules")
{
	CHECK_FALSE(Core::NamesIgnoreRules("main.cpp"));
	CHECK_FALSE(Core::NamesIgnoreRules(std::filesystem::path{"src"} / ".gitignore.bak"));
	CHECK_FALSE(Core::NamesIgnoreRules(std::filesystem::path{"src"} / "config"));
	CHECK_FALSE(Core::NamesIgnoreRules(std::filesystem::path{".git"} / "objects" / ".gitignore"));
	CHECK_FALSE(Core::NamesIgnoreRules(std::filesystem::path{".git"} / "HEAD"));
}

TEST_CASE("A rules file written in a subfolder is reported as a change of ignore rules with its folder")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeDirectory("sub");

	Result<Scope<DirectoryWatcher>> started = DirectoryWatcher::Start(temporaryDirectory.GetPath());
	REQUIRE(started.HasValue());
	DirectoryWatcher& watcher = *started.GetValue();

	temporaryDirectory.MakeFile("sub/.gitignore", "*.log\n");

	const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kChangeTimeout;
	bool rulesChanged = false;
	std::vector<std::filesystem::path> changedDirectories;

	while (std::chrono::steady_clock::now() < deadline && changedDirectories.empty())
	{
		const DirectoryWatcher::Changes changes = watcher.TakeSettled();
		rulesChanged = rulesChanged || changes.IgnoreRulesChanged;
		changedDirectories = changes.Directories;

		std::this_thread::sleep_for(kPollInterval);
	}

	CHECK(rulesChanged);
	REQUIRE_FALSE(changedDirectories.empty());
	CHECK(changedDirectories[0] == temporaryDirectory.GetPath() / "sub");
}

TEST_CASE("A file of the same name outside the repository's directory says nothing since it is an ordinary file of the working tree")
{
	CHECK_FALSE(Core::NamesRepositoryState("HEAD"));
	CHECK_FALSE(Core::NamesRepositoryState(std::filesystem::path{"src"} / "index"));
	CHECK_FALSE(Core::NamesRepositoryState(std::filesystem::path{"refs"} / "heads" / "main"));
}

TEST_CASE("A commit made outside the application is reported as a repository change and not as a changed directory")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeDirectory(".git/refs/heads");

	Result<Scope<DirectoryWatcher>> started = DirectoryWatcher::Start(temporaryDirectory.GetPath());
	REQUIRE(started.HasValue());
	DirectoryWatcher& watcher = *started.GetValue();

	const std::string newBranchTip = "0123456789abcdef0123456789abcdef01234567\n";
	temporaryDirectory.MakeFile(".git/refs/heads/main", newBranchTip);

	constexpr std::chrono::milliseconds kRepositoryPollInterval{50};

	const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kChangeTimeout;
	bool reported = false;

	while (std::chrono::steady_clock::now() < deadline && !reported)
	{
		const DirectoryWatcher::Changes changes = watcher.TakeSettled();
		reported = changes.RepositoryStateChanged;

		CHECK(changes.Directories.empty());

		if (!reported)
		{
			std::this_thread::sleep_for(kRepositoryPollInterval);
		}
	}

	CHECK(reported);
}

TEST_CASE("A folder inside WSL is refused at once rather than watched without a report")
{
	const auto started = Core::DirectoryWatcher::Start(L"\\\\wsl.localhost\\Ubuntu\\home\\me\\proj");

	REQUIRE_FALSE(started.HasValue());
	CHECK(started.GetError().Code == Core::ErrorCode::Unsupported);
	CHECK(started.GetError().Message.find("not reported to Windows") != std::string::npos);
}
