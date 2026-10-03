#include <doctest/doctest.h>

#include "Persistence/RestrictedRestore.h"

#include <filesystem>
#include <optional>
#include <vector>

using namespace Graphite;

// Every path below is judged lexically, so none of them has to exist.

namespace {
	const std::filesystem::path kRoot = std::filesystem::path{"D:\\Work\\Repo"};

	TabState Tab(const std::filesystem::path& path)
	{
		return TabState{.Path = path};
	}

	DiffTabState WorkingDiff(const std::filesystem::path& path)
	{
		return DiffTabState{.Target = Core::DiffTarget::WorkTreeToIndex, .Path = path};
	}

	// The state a crafted .graphite/workspace.json reads back as: tabs and diffs pointing
	// outside the folder alongside ones inside it.
	WorkspaceState Crafted()
	{
		WorkspaceState state;
		state.Tabs = {Tab("C:\\Windows\\win.ini"), Tab("D:\\Work\\Other\\secret.txt"),
					  Tab("D:\\Work\\Repo\\..\\x"), Tab("D:\\Work\\Repo2\\a.txt"),
					  Tab(kRoot / "src" / "main.cpp")};
		state.Diffs = {WorkingDiff("C:\\Windows\\win.ini"), WorkingDiff("..\\..\\Windows\\win.ini"),
					   WorkingDiff("src\\..\\..\\x"), WorkingDiff("src\\main.cpp")};
		state.Terminals = std::vector<TerminalTabState>{TerminalTabState{"{A}", "PowerShell 7", "C:\\Windows"}};
		return state;
	}
}

TEST_CASE("A folder in restricted mode restores only the tabs inside it")
{
	const WorkspaceState restorable = RestorableState(Crafted(), kRoot, false);

	CHECK(restorable.Tabs == std::vector<TabState>{Tab(kRoot / "src" / "main.cpp")});
}

TEST_CASE("A folder in restricted mode activates no tab it dropped")
{
	WorkspaceState state = Crafted();
	state.Active = "C:\\Windows\\win.ini";

	CHECK(RestorableState(state, kRoot, false).Active == std::nullopt);

	state.Active = "D:\\Work\\Repo\\..\\x";

	CHECK(RestorableState(state, kRoot, false).Active == std::nullopt);
}

TEST_CASE("A folder in restricted mode keeps an active tab inside it")
{
	WorkspaceState state = Crafted();
	state.Active = kRoot / "src" / "main.cpp";

	CHECK(RestorableState(state, kRoot, false).Active == kRoot / "src" / "main.cpp");
}

TEST_CASE("A folder in restricted mode restores only the diffs whose path stays inside the repository")
{
	const WorkspaceState restorable = RestorableState(Crafted(), kRoot, false);

	CHECK(restorable.Diffs == std::vector<DiffTabState>{WorkingDiff("src\\main.cpp")});
}

TEST_CASE("A folder in restricted mode leaves the rest of its state to the rules that own it")
{
	WorkspaceState state = Crafted();
	state.Sessions = {SessionTabState{"s1", "Session", SessionTab::Cost}};

	const WorkspaceState restorable = RestorableState(state, kRoot, false);

	CHECK(restorable.Sessions == state.Sessions);
	CHECK(restorable.Terminals == state.Terminals);
}

TEST_CASE("A trusted folder restores everything it saved, outside it or not")
{
	WorkspaceState state = Crafted();
	state.Active = "C:\\Windows\\win.ini";

	CHECK(RestorableState(state, kRoot, true) == state);
}
