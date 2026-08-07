#include "corepch.h"

#include "Core/Fs/DirectoryWatcher.h"

#include "TempDirectory.h"

#include <doctest/doctest.h>

#include <chrono>
#include <fstream>
#include <optional>
#include <string>
#include <thread>

using namespace core;

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

    auto watcher = DirectoryWatcher::Start(temp.Path());
    REQUIRE(watcher.HasValue());

    temp.MakeFile("created.txt", "x");

    const auto changes = WaitForChanges(*watcher.Value(), std::chrono::seconds{5});

    REQUIRE_FALSE(changes.empty());
    CHECK(changes[0] == temp.Path());
}

TEST_CASE("A file created in a subdirectory reports that subdirectory")
{
    TempDirectory temp;
    temp.MakeDir("nested");

    auto watcher = DirectoryWatcher::Start(temp.Path());
    REQUIRE(watcher.HasValue());

    temp.MakeFile("nested/created.txt", "x");

    const auto changes = WaitForChanges(*watcher.Value(), std::chrono::seconds{5});

    REQUIRE_FALSE(changes.empty());

    // The subtree is watched, and what is reported is the directory holding the changed
    // entry — that is the node the tree will refresh.
    CHECK(changes[0] == temp.Path() / "nested");
}

TEST_CASE("Changes inside a .git directory are not reported")
{
    TempDirectory temp;
    temp.MakeDir(".git");

    auto watcher = DirectoryWatcher::Start(temp.Path());
    REQUIRE(watcher.HasValue());

    temp.MakeFile(".git/index", "x");

    // Git rewrites files under .git on every operation, and refreshing for each one
    // would be a storm of no-op work.
    CHECK(WaitForChanges(*watcher.Value(), std::chrono::seconds{2}).empty());
}

TEST_CASE("The .git directory appearing is reported")
{
    TempDirectory temp;

    auto watcher = DirectoryWatcher::Start(temp.Path());
    REQUIRE(watcher.HasValue());

    // How `git init` in a terminal becomes visible: the directory itself passes the
    // filter that its contents do not.
    temp.MakeDir(".git");

    const auto changes = WaitForChanges(*watcher.Value(), std::chrono::seconds{5});

    REQUIRE_FALSE(changes.empty());
    CHECK(changes[0] == temp.Path());
}

TEST_CASE("Destroying a watcher with no changes pending returns promptly")
{
    TempDirectory temp;

    const auto before = std::chrono::steady_clock::now();

    {
        auto watcher = DirectoryWatcher::Start(temp.Path());
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

    auto watcher = DirectoryWatcher::Start(temp.Path());
    REQUIRE(watcher.HasValue());

    temp.MakeFile(".git/info/exclude", "build/");

    const auto changes = WaitForChanges(*watcher.Value(), std::chrono::seconds{5});

    REQUIRE_FALSE(changes.empty());

    // The root, not the directory holding the file: these rules apply to the whole tree,
    // and refreshing the root is what re-asks every loaded node.
    CHECK(changes[0] == temp.Path());
}

TEST_CASE("A watcher stops running once its directory is deleted")
{
    TempDirectory temp;

    auto watcher = DirectoryWatcher::Start(temp.Path());
    REQUIRE(watcher.HasValue());
    REQUIRE(watcher.Value()->IsRunning());

    // Deleting the watched directory is what kills the thread in practice, and the whole
    // point of IsRunning is that the consumer cannot tell this apart from a quiet disk
    // any other way — TakeSettled simply returns nothing, for ever.
    std::error_code code;
    std::filesystem::remove_all(temp.Path(), code);
    REQUIRE_FALSE(code);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    bool stopped = false;

    while (!stopped && std::chrono::steady_clock::now() < deadline)
    {
        stopped = !watcher.Value()->IsRunning();

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
    std::filesystem::rename(watched, temp.Path() / "after", code);
    REQUIRE_FALSE(code);

    const auto moved = WaitForMove(*watcher.Value(), std::chrono::seconds{10});

    REQUIRE(moved.has_value());
    CHECK(*moved == temp.Path() / "after");
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
    std::filesystem::rename(watched, temp.Path() / "destination" / "watched", code);
    REQUIRE_FALSE(code);

    const auto moved = WaitForMove(*watcher.Value(), std::chrono::seconds{10});

    REQUIRE(moved.has_value());
    CHECK(*moved == temp.Path() / "destination" / "watched");
}

TEST_CASE("A directory that stays put is never reported as moved")
{
    TempDirectory temp;
    temp.MakeFile("a.txt", "x");

    auto watcher = DirectoryWatcher::Start(temp.Path());
    REQUIRE(watcher.HasValue());

    // Ordinary activity must not be mistaken for a move — and neither must the case
    // difference between the path the caller passed and the one the filesystem stores,
    // which is why the baseline comes from the handle rather than from that argument.
    temp.MakeFile("b.txt", "x");

    CHECK_FALSE(WaitForMove(*watcher.Value(), std::chrono::seconds{3}).has_value());
}

TEST_CASE("A move is reported once, not on every poll")
{
    TempDirectory temp;
    const std::filesystem::path watched = temp.MakeDir("before");

    auto watcher = DirectoryWatcher::Start(watched);
    REQUIRE(watcher.HasValue());

    std::error_code code;
    std::filesystem::rename(watched, temp.Path() / "after", code);
    REQUIRE_FALSE(code);

    REQUIRE(WaitForMove(*watcher.Value(), std::chrono::seconds{10}).has_value());

    // The consumer acts on it by re-pointing the workspace; being told again afterwards
    // would make it do that work over and over.
    CHECK_FALSE(WaitForMove(*watcher.Value(), std::chrono::seconds{3}).has_value());
}

TEST_CASE("A move is noticed even while the directory is busy")
{
    TempDirectory temp;
    const std::filesystem::path watched = temp.MakeDir("busy");

    auto watcher = DirectoryWatcher::Start(watched);
    REQUIRE(watcher.HasValue());

    std::error_code code;
    std::filesystem::rename(watched, temp.Path() / "busy-moved", code);
    REQUIRE_FALSE(code);

    // Writing faster than kMoveCheckInterval. The check used to run only when the wait
    // expired, and every notification restarts that wait — so a directory under a build
    // would never be checked at all, which is exactly when this matters least to miss.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    std::optional<std::filesystem::path> moved;

    for (int i = 0; !moved && std::chrono::steady_clock::now() < deadline; ++i)
    {
        std::ofstream{temp.Path() / "busy-moved" / ("churn" + std::to_string(i) + ".txt")}
            << "x";

        std::this_thread::sleep_for(std::chrono::milliseconds{100});

        moved = watcher.Value()->TakeSettled().movedTo;
    }

    REQUIRE(moved.has_value());
    CHECK(*moved == temp.Path() / "busy-moved");
}
