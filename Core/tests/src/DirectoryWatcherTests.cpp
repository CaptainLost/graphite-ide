#include "corepch.h"

#include "Core/Fs/DirectoryWatcher.h"

#include "TempDirectory.h"

#include <doctest/doctest.h>

#include <chrono>
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
            if (auto changes = watcher.TakeSettledChanges(); !changes.empty())
                return changes;

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
