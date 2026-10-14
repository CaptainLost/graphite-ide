#include <doctest/doctest.h>

#include "Persistence/RestrictedRestore.h"

#include <filesystem>
#include <optional>
#include <vector>

using namespace Graphite;

namespace {
	const std::filesystem::path kRoot = std::filesystem::path{"D:\\Work\\Repo"};
	constexpr bool kTrusted = true;
	constexpr bool kRestricted = false;

	TabState Tab(const std::filesystem::path& path)
	{
		return TabState{.Path = path};
	}

	DiffTabState WorkingDiff(const std::filesystem::path& path)
	{
		return DiffTabState{.Target = Core::DiffTarget::WorkTreeToIndex, .Path = path};
	}

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
	const WorkspaceState restorable = RestorableState(Crafted(), kRoot, kRestricted);

	CHECK(restorable.Tabs == std::vector<TabState>{Tab(kRoot / "src" / "main.cpp")});
}

TEST_CASE("A folder in restricted mode activates no tab it dropped")
{
	WorkspaceState state = Crafted();
	state.Active = "C:\\Windows\\win.ini";

	CHECK(RestorableState(state, kRoot, kRestricted).Active == std::nullopt);

	state.Active = "D:\\Work\\Repo\\..\\x";

	CHECK(RestorableState(state, kRoot, kRestricted).Active == std::nullopt);
}

TEST_CASE("A folder in restricted mode keeps an active tab inside it")
{
	WorkspaceState state = Crafted();
	state.Active = kRoot / "src" / "main.cpp";

	CHECK(RestorableState(state, kRoot, kRestricted).Active == kRoot / "src" / "main.cpp");
}

TEST_CASE("A folder in restricted mode restores only the diffs whose path stays inside the repository")
{
	const WorkspaceState restorable = RestorableState(Crafted(), kRoot, kRestricted);

	CHECK(restorable.Diffs == std::vector<DiffTabState>{WorkingDiff("src\\main.cpp")});
}

TEST_CASE("A folder in restricted mode drops any diff path with a parent step, even one that comes back")
{
	const std::filesystem::path root = kRoot / "sub";

	WorkspaceState state;
	state.Diffs = {WorkingDiff("..\\sub\\secret"), WorkingDiff("src\\..\\main.cpp"), WorkingDiff("../sub/x"),
				   WorkingDiff("sub\\main.cpp")};

	CHECK(RestorableState(state, root, kRestricted).Diffs == std::vector<DiffTabState>{WorkingDiff("sub\\main.cpp")});
}

TEST_CASE("A folder in restricted mode drops a diff path that is drive-relative, root-relative or UNC")
{
	WorkspaceState state;
	state.Diffs = {WorkingDiff("C:foo"), WorkingDiff("\\Windows\\win.ini"), WorkingDiff("\\\\server\\share\\x"),
				   WorkingDiff("src/main.cpp")};

	CHECK(RestorableState(state, kRoot, kRestricted).Diffs == std::vector<DiffTabState>{WorkingDiff("src/main.cpp")});
}

TEST_CASE("A folder in restricted mode keeps an inside tab however its case and slashes are spelled")
{
	WorkspaceState state;
	state.Tabs = {Tab("d:\\work\\repo\\SRC\\a.cpp"), Tab("D:/Work/Repo/src/b.cpp")};

	CHECK(RestorableState(state, kRoot, kRestricted).Tabs == state.Tabs);
}

TEST_CASE("A folder in restricted mode drops a tab path that names no place inside it")
{
	WorkspaceState state;
	state.Tabs = {Tab("src\\main.cpp"), Tab("C:foo"), Tab("\\\\server\\share\\x"), Tab("\\Windows\\win.ini")};
	state.Active = "\\Windows\\win.ini";

	const WorkspaceState restorable = RestorableState(state, kRoot, kRestricted);

	CHECK(restorable.Tabs.empty());
	CHECK(restorable.Active == std::nullopt);
}

TEST_CASE("A folder in restricted mode leaves the rest of its state to the rules that own it")
{
	WorkspaceState state = Crafted();
	state.Sessions = {SessionTabState{"s1", "Session", SessionTab::Cost}};

	const WorkspaceState restorable = RestorableState(state, kRoot, kRestricted);

	CHECK(restorable.Sessions == state.Sessions);
	CHECK(restorable.Terminals == state.Terminals);
}

TEST_CASE("A trusted folder restores everything it saved, outside it or not")
{
	WorkspaceState state = Crafted();
	state.Active = "C:\\Windows\\win.ini";

	CHECK(RestorableState(state, kRoot, kTrusted) == state);
}
