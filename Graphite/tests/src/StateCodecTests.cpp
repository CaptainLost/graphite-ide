#include <doctest/doctest.h>

#include "Persistence/StateCodec.h"

#include "Core/Base/JsonFile.h"

using namespace Graphite;

namespace {
	const std::filesystem::path kRoot = std::filesystem::path{"D:\\Work\\Repo"};
}

TEST_CASE("Global state survives a write and a read")
{
	GlobalState state;
	state.Window = Core::WindowPlacement{2000, 40, 1600, 900, true};
	state.PanelVisibility = {{"problems", false}, {"terminal", true}};

	Core::JsonFile file;
	WriteGlobalState(state, file);

	CHECK(ReadGlobalState(file) == state);
	CHECK(file.Get("window.maximized") == "1");
	CHECK(file.Get("panel.problems.visible") == "0");
}

TEST_CASE("A panel back at its default leaves no key behind")
{
	Core::JsonFile file;
	WriteGlobalState(GlobalState{.PanelVisibility = {{"problems", false}}}, file);

	WriteGlobalState(GlobalState{}, file);

	CHECK(file.Get("panel.problems.visible").empty());
	CHECK(ReadGlobalState(file).PanelVisibility.empty());
}

TEST_CASE("Settings unrelated to state are left alone")
{
	Core::JsonFile file;
	file.Set("colorScheme", "dark");
	file.Set("lastFolder", "D:\\Work");

	WriteGlobalState(GlobalState{.Window = Core::WindowPlacement{0, 0, 800, 600, false}}, file);
	WriteWorkspaceState(WorkspaceState{}, kRoot, file);

	CHECK(file.Get("colorScheme") == "dark");
	CHECK(file.Get("lastFolder") == "D:\\Work");
}

TEST_CASE("A window with a missing, non-numeric or empty rectangle reads as no window")
{
	Core::JsonFile file;
	file.Set("window.x", "10");
	file.Set("window.y", "10");
	file.Set("window.width", "800");
	CHECK_FALSE(ReadGlobalState(file).Window.has_value());

	file.Set("window.height", "tall");
	CHECK_FALSE(ReadGlobalState(file).Window.has_value());

	file.Set("window.height", "0");
	CHECK_FALSE(ReadGlobalState(file).Window.has_value());

	file.Set("window.height", "600");
	CHECK(ReadGlobalState(file).Window == Core::WindowPlacement{10, 10, 800, 600, false});
}

TEST_CASE("Negative window coordinates, from a monitor left of the primary, are kept")
{
	Core::JsonFile file;
	WriteGlobalState(GlobalState{.Window = Core::WindowPlacement{-1900, -20, 1600, 900, false}}, file);

	CHECK(ReadGlobalState(file).Window == Core::WindowPlacement{-1900, -20, 1600, 900, false});
}

TEST_CASE("Workspace state survives a write and a read")
{
	WorkspaceState state;
	state.Tabs = {TabState{kRoot / "src" / "Main.cpp", false, 199, 12, 180},
				  TabState{kRoot / "README.md", true, 0, 0, 0}};
	state.Active = kRoot / "src" / "Main.cpp";

	Core::JsonFile file;
	WriteWorkspaceState(state, kRoot, file);

	CHECK(ReadWorkspaceState(file, kRoot) == state);
}

TEST_CASE("Paths inside the folder are stored relative to it, so the folder can move")
{
	WorkspaceState state;
	state.Tabs = {TabState{kRoot / "src" / "Main.cpp"}};
	state.Active = kRoot / "src" / "Main.cpp";

	Core::JsonFile file;
	WriteWorkspaceState(state, kRoot, file);

	const std::filesystem::path moved{"E:\\Elsewhere\\Repo"};
	const WorkspaceState read = ReadWorkspaceState(file, moved);

	REQUIRE(read.Tabs.size() == 1);
	CHECK(read.Tabs[0].Path == moved / "src" / "Main.cpp");
	CHECK(read.Active == moved / "src" / "Main.cpp");
}

TEST_CASE("A path outside the folder is stored whole")
{
	const std::filesystem::path outside{"C:\\Other\\notes.txt"};

	Core::JsonFile file;
	WriteWorkspaceState(WorkspaceState{.Tabs = {TabState{outside}}}, kRoot, file);

	CHECK(file.Get("restore.tab.0.path") == "C:\\Other\\notes.txt");
	CHECK(ReadWorkspaceState(file, kRoot).Tabs[0].Path == outside);
}

TEST_CASE("A shorter tab list leaves none of the longer one's keys behind")
{
	Core::JsonFile file;
	WriteWorkspaceState(WorkspaceState{.Tabs = {TabState{kRoot / "a"}, TabState{kRoot / "b"}}},
						kRoot, file);

	WriteWorkspaceState(WorkspaceState{.Tabs = {TabState{kRoot / "a"}}}, kRoot, file);

	CHECK(file.Get("restore.tab.1.path").empty());
	CHECK(ReadWorkspaceState(file, kRoot).Tabs.size() == 1);
}

TEST_CASE("A damaged tab list reads as what can be salvaged")
{
	Core::JsonFile file;
	file.Set("restore.tab.count", "3");
	file.Set("restore.tab.0.path", "a.cpp");
	file.Set("restore.tab.0.line", "many");
	// Tab 1 has no path at all.
	file.Set("restore.tab.2.path", "c.cpp");
	file.Set("restore.tab.2.line", "7");

	const WorkspaceState read = ReadWorkspaceState(file, kRoot);

	REQUIRE(read.Tabs.size() == 2);
	CHECK(read.Tabs[0].Path == kRoot / "a.cpp");
	CHECK(read.Tabs[0].Line == 0);
	CHECK(read.Tabs[1].Path == kRoot / "c.cpp");
	CHECK(read.Tabs[1].Line == 7);
}

TEST_CASE("An absurd tab count does not turn into an absurd loop")
{
	Core::JsonFile file;
	file.Set("restore.tab.count", "4000000000");
	file.Set("restore.tab.0.path", "a.cpp");

	CHECK(ReadWorkspaceState(file, kRoot).Tabs.size() == 1);
}

TEST_CASE("An empty file reads as nothing open")
{
	const Core::JsonFile file;

	CHECK(ReadWorkspaceState(file, kRoot) == WorkspaceState{});
	CHECK(ReadGlobalState(file) == GlobalState{});
}
