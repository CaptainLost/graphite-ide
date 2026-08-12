#include "corepch.h"

#include "Core/Fs/DirectoryWatcher.h"

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

namespace
{
    /// Polls until the watcher reports something or the deadline passes. A fixed sleep
    /// would either be flaky on a loaded machine or slow on an idle one.
    std::vector<std::filesystem::path> WaitForChanges(DirectoryWatcher& watcher,
                                                      std::chrono::seconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;

        while (std::chrono::steady_clock::now() < deadline)
        {
            if (auto changes = watcher.TakeSettled(); !changes.directories.empty())
                return changes.directories;

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

namespace
{
    /// Squeezes this process onto a single CPU and keeps one thread spinning on it, for
    /// as long as it lives.
    ///
    /// The window the next test is about is the gap before the watcher's worker thread is
    /// first scheduled, and on an idle multi-core machine that gap barely exists: the new
    /// thread simply starts on a free core. Without contention the defect hid — the first
    /// start in a process is slow enough to lose the race about one time in seven, but the
    /// ones after it are warm and lost it about one time in a hundred, which no practical
    /// number of rounds turns into a reliable failure.
    ///
    /// Contention is therefore the point, and *where* it comes from is what makes this
    /// safe. The first attempt spun one thread per hardware_concurrency() core, which is
    /// the wrong question twice over: that number reports the host's cores rather than a
    /// container's CPU quota, so a 1-CPU CI container carved out of a large host would
    /// spawn dozens of spinners against a single core and starve the very worker the test
    /// waits on — failing on *correct* code. Capping the spinners instead was measured and
    /// does not work: at 2 and at 4 on a 16-core machine the defect went undetected 10
    /// runs out of 10, because a free core was always available.
    ///
    /// Narrowing the process to one CPU gets the contention without the appetite. The
    /// whole test then consumes a single core no matter how large the host is, so it
    /// cannot take more than the smallest plausible CI container already has, and the
    /// single CPU is chosen out of the mask the process is *already* restricted to, so
    /// this tightens an existing limit rather than fighting it.
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
            m_Spinner = std::thread{[this]
                                    {
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
                ::SetProcessAffinityMask(::GetCurrentProcess(), m_PreviousMask);
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
    temp.MakeDir("nested");

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
    temp.MakeDir(".git");

    auto watcher = DirectoryWatcher::Start(temp.GetPath());
    REQUIRE(watcher.HasValue());

    temp.MakeFile(".git/index", "x");

    // Git rewrites files under .git on every operation, and refreshing for each one
    // would be a storm of no-op work.
    CHECK(WaitForChanges(*watcher.GetValue(), std::chrono::seconds{2}).empty());
}

TEST_CASE("The .git directory appearing is reported")
{
    TempDirectory temp;

    auto watcher = DirectoryWatcher::Start(temp.GetPath());
    REQUIRE(watcher.HasValue());

    // How `git init` in a terminal becomes visible: the directory itself passes the
    // filter that its contents do not.
    temp.MakeDir(".git");

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
    temp.MakeDir(".git/info");

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
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }

    CHECK(stopped);
}

namespace
{
    /// Polls until the watcher reports a move or the deadline passes. The detection is a
    /// timed poll on the worker thread, so this has to allow at least one interval.
    std::optional<std::filesystem::path> WaitForMove(DirectoryWatcher& watcher,
                                                     std::chrono::seconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;

        while (std::chrono::steady_clock::now() < deadline)
        {
            if (auto changes = watcher.TakeSettled(); changes.movedTo)
                return changes.movedTo;

            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        }

        return std::nullopt;
    }
}

TEST_CASE("A watched directory that is renamed is reported at its new path")
{
    TempDirectory temp;
    const std::filesystem::path watched = temp.MakeDir("before");

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
    const std::filesystem::path watched = temp.MakeDir("origin/watched");
    temp.MakeDir("destination");

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
    const std::filesystem::path watched = temp.MakeDir("before");

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
    const std::filesystem::path watched = temp.MakeDir("busy");

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

        moved = watcher.GetValue()->TakeSettled().movedTo;
    }

    REQUIRE(moved.has_value());
    CHECK(*moved == temp.GetPath() / "busy-moved");
}
