#include <doctest/doctest.h>

#include "Review/WorktreeWatch.h"

#include <filesystem>
#include <memory>

namespace {

	// Reports one changed folder on its first look, nothing after.
	class OnceChangedWatch final : public Core::FolderWatch
	{
	public:
		Core::FolderChanges TakeSettled() override
		{
			Core::FolderChanges changes;

			if (!m_Reported)
			{
				changes.Directories.push_back("changed");
				m_Reported = true;
			}

			return changes;
		}

		bool IsRunning() const override { return true; }

	private:
		bool m_Reported = false;
	};
}

TEST_CASE("A worktree watch over any folder watch reports what it settled once")
{
	std::filesystem::path started;
	const Graphite::WorktreeWatchFactory factory = Graphite::WatchWithFolderWatch([&](const std::filesystem::path& root) -> Core::Result<Core::Scope<Core::FolderWatch>> {
		started = root;
		return Core::Scope<Core::FolderWatch>{std::make_unique<OnceChangedWatch>()};
	});

	const Core::Scope<Graphite::WorktreeWatch> watch = factory("wt");

	REQUIRE(watch != nullptr);
	CHECK(started == std::filesystem::path{"wt"});
	CHECK(watch->TakeChanged());
	CHECK_FALSE(watch->TakeChanged());
}

TEST_CASE("A worktree whose folder watch cannot start has no watch")
{
	const Graphite::WorktreeWatchFactory factory = Graphite::WatchWithFolderWatch([](const std::filesystem::path&) -> Core::Result<Core::Scope<Core::FolderWatch>> {
		return Core::Error{Core::ErrorCode::IoFailure, "no agent"};
	});

	CHECK(factory("wt") == nullptr);
}

namespace {

	class StoppedWatch final : public Core::FolderWatch
	{
	public:
		Core::FolderChanges TakeSettled() override { return {}; }
		bool IsRunning() const override { return false; }
	};
}

TEST_CASE("A worktree watch whose folder watch stopped starts it again and reports a change, at most once in a while")
{
	int starts = 0;
	const Graphite::WorktreeWatchFactory factory = Graphite::WatchWithFolderWatch([&](const std::filesystem::path&) -> Core::Result<Core::Scope<Core::FolderWatch>> {
		++starts;
		return Core::Scope<Core::FolderWatch>{std::make_unique<StoppedWatch>()};
	});
	const Core::Scope<Graphite::WorktreeWatch> watch = factory("wt");
	REQUIRE(watch != nullptr);

	CHECK(watch->TakeChanged());
	CHECK(starts == 2);
	CHECK_FALSE(watch->TakeChanged());
	CHECK(starts == 2);
}
