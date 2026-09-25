#include <doctest/doctest.h>

#include "Persistence/StateKeeper.h"

#include <utility>
#include <vector>

using namespace Graphite;

namespace {
	struct Recorder
	{
		std::vector<GlobalState> Global;
		std::vector<std::pair<std::filesystem::path, WorkspaceState>> Workspace;

		StateKeeper MakeKeeper()
		{
			return StateKeeper{
				[this](const GlobalState& state) { Global.push_back(state); },
				[this](const std::filesystem::path& root, const WorkspaceState& state) {
					Workspace.emplace_back(root, state);
				}};
		}
	};

	GlobalState WindowAt(int x)
	{
		return GlobalState{.Window = Core::WindowPlacement{x, 0, 800, 600, false}};
	}

	WorkspaceState CaretOn(std::size_t line)
	{
		return WorkspaceState{.Tabs = {TabState{"D:\\Repo\\a.cpp", false, line, 0, 0}}};
	}

	const std::filesystem::path kRepo{"D:\\Repo"};
	const std::filesystem::path kOther{"D:\\Other"};
}

TEST_CASE("Nothing is written while nothing differs from what is on disk")
{
	Recorder recorder;
	StateKeeper keeper = recorder.MakeKeeper();
	keeper.AdoptGlobal(WindowAt(0));
	keeper.AdoptWorkspace(kRepo, CaretOn(1));

	for (int frame = 0; frame < 300; ++frame)
	{
		keeper.Observe(0.016, WindowAt(0), CaretOn(1));
	}

	CHECK(recorder.Global.empty());
	CHECK(recorder.Workspace.empty());
}

TEST_CASE("A burst of changes is written once, two seconds after the first")
{
	Recorder recorder;
	StateKeeper keeper = recorder.MakeKeeper();
	keeper.AdoptGlobal(WindowAt(0));

	keeper.Observe(0.0, WindowAt(10), std::nullopt);
	keeper.Observe(1.0, WindowAt(20), std::nullopt);
	CHECK(recorder.Global.empty());

	keeper.Observe(1.0, WindowAt(30), std::nullopt);
	REQUIRE(recorder.Global.size() == 1);
	CHECK(recorder.Global[0] == WindowAt(30));

	keeper.Observe(5.0, WindowAt(30), std::nullopt);
	CHECK(recorder.Global.size() == 1);
}

TEST_CASE("A change undone before the delay is not written")
{
	Recorder recorder;
	StateKeeper keeper = recorder.MakeKeeper();
	keeper.AdoptGlobal(WindowAt(0));

	keeper.Observe(0.0, WindowAt(10), std::nullopt);
	keeper.Observe(1.0, WindowAt(0), std::nullopt);
	keeper.Observe(5.0, WindowAt(0), std::nullopt);

	CHECK(recorder.Global.empty());
}

TEST_CASE("Moving the caret writes the folder's file and not the global one")
{
	Recorder recorder;
	StateKeeper keeper = recorder.MakeKeeper();
	keeper.AdoptGlobal(WindowAt(0));
	keeper.AdoptWorkspace(kRepo, CaretOn(1));

	keeper.Observe(0.0, WindowAt(0), CaretOn(2));
	keeper.Observe(2.0, WindowAt(0), CaretOn(2));

	CHECK(recorder.Global.empty());
	REQUIRE(recorder.Workspace.size() == 1);
	CHECK(recorder.Workspace[0].first == kRepo);
	CHECK(recorder.Workspace[0].second == CaretOn(2));
}

TEST_CASE("Flush writes what differs at once")
{
	Recorder recorder;
	StateKeeper keeper = recorder.MakeKeeper();
	keeper.AdoptGlobal(WindowAt(0));
	keeper.AdoptWorkspace(kRepo, CaretOn(1));

	keeper.Observe(0.0, WindowAt(5), CaretOn(9));
	keeper.Flush();

	CHECK(recorder.Global.size() == 1);
	CHECK(recorder.Workspace.size() == 1);

	keeper.Flush();
	CHECK(recorder.Global.size() == 1);
	CHECK(recorder.Workspace.size() == 1);
}

TEST_CASE("Closing a folder writes its tabs to it, and the next folder never receives them")
{
	Recorder recorder;
	StateKeeper keeper = recorder.MakeKeeper();
	keeper.AdoptWorkspace(kRepo, CaretOn(1));

	keeper.Observe(0.0, GlobalState{}, CaretOn(7));
	keeper.CloseWorkspace();

	REQUIRE(recorder.Workspace.size() == 1);
	CHECK(recorder.Workspace[0].first == kRepo);
	CHECK(recorder.Workspace[0].second == CaretOn(7));

	keeper.AdoptWorkspace(kOther, CaretOn(3));
	keeper.Observe(0.0, GlobalState{}, CaretOn(3));
	keeper.Observe(5.0, GlobalState{}, CaretOn(3));

	CHECK(recorder.Workspace.size() == 1);
}

TEST_CASE("Adopting another folder while one is open writes the first one first")
{
	Recorder recorder;
	StateKeeper keeper = recorder.MakeKeeper();
	keeper.AdoptWorkspace(kRepo, CaretOn(1));
	keeper.Observe(0.0, GlobalState{}, CaretOn(4));

	keeper.AdoptWorkspace(kOther, CaretOn(0));

	REQUIRE(recorder.Workspace.size() == 1);
	CHECK(recorder.Workspace[0].first == kRepo);
}

TEST_CASE("Tabs seen with no folder open are never written")
{
	Recorder recorder;
	StateKeeper keeper = recorder.MakeKeeper();

	keeper.Observe(0.0, GlobalState{}, CaretOn(4));
	keeper.Observe(5.0, GlobalState{}, CaretOn(4));
	keeper.Flush();

	CHECK(recorder.Workspace.empty());
}

TEST_CASE("A folder that moves is written at its new place")
{
	Recorder recorder;
	StateKeeper keeper = recorder.MakeKeeper();
	keeper.AdoptWorkspace(kRepo, CaretOn(1));

	keeper.MoveWorkspace(kOther);
	keeper.Observe(0.0, GlobalState{}, CaretOn(2));
	keeper.Flush();

	REQUIRE(recorder.Workspace.size() == 1);
	CHECK(recorder.Workspace[0].first == kOther);
}
