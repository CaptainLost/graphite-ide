#include <doctest/doctest.h>

#include "Persistence/StateCodec.h"

#include "Core/Base/JsonFile.h"
#include "Core/FileSystem/FileSystem.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

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

TEST_CASE("Hidden terminals survive a write and a read")
{
	const GlobalState state{.TerminalsHidden = true};

	Core::JsonFile file;
	WriteGlobalState(state, file);

	CHECK(ReadGlobalState(file) == state);
	CHECK(file.Get("terminals.hidden") == "1");
}

TEST_CASE("Shown terminals leave no key behind")
{
	Core::JsonFile file;
	WriteGlobalState(GlobalState{.TerminalsHidden = true}, file);

	WriteGlobalState(GlobalState{}, file);

	CHECK(file.Get("terminals.hidden").empty());
	CHECK_FALSE(ReadGlobalState(file).TerminalsHidden);
}

TEST_CASE("A hidden flag that is not 0 or 1 reads as shown")
{
	Core::JsonFile file;
	file.Set("terminals.hidden", "yes");

	CHECK_FALSE(ReadGlobalState(file).TerminalsHidden);
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
	const bool isPreview = true;
	const bool isPinned = false;
	state.Tabs = {TabState{kRoot / "src" / "Main.cpp", isPinned, 199, 12, 180},
				  TabState{kRoot / "README.md", isPreview, 0, 0, 0}};
	state.Active = kRoot / "src" / "Main.cpp";

	Core::JsonFile file;
	WriteWorkspaceState(state, kRoot, file);

	CHECK(ReadWorkspaceState(file, kRoot) == state);
}

TEST_CASE("A tab's caret column counted in UTF-16 units keeps its unit through a write and a read")
{
	TabState tab{.Path = kRoot / "src" / "Main.cpp", .Line = 3, .Column = 7};
	tab.ColumnIn = Core::ColumnUnit::Utf16CodeUnit;
	const WorkspaceState state{.Tabs = {tab, TabState{kRoot / "README.md"}}};

	Core::JsonFile file;
	WriteWorkspaceState(state, kRoot, file);
	const WorkspaceState read = ReadWorkspaceState(file, kRoot);

	REQUIRE(read.Tabs.size() == 2);
	CHECK(read.Tabs[0].ColumnIn == Core::ColumnUnit::Utf16CodeUnit);
	CHECK(read.Tabs[1].ColumnIn == Core::ColumnUnit::Byte);
}

TEST_CASE("A tab's caret column of an unknown unit is read as counted in bytes")
{
	const WorkspaceState state{.Tabs = {TabState{.Path = kRoot / "src" / "Main.cpp", .Line = 3, .Column = 7}}};

	Core::JsonFile file;
	WriteWorkspaceState(state, kRoot, file);
	file.Set("restore.tab.0.columnUnit", "furlongs");

	const WorkspaceState read = ReadWorkspaceState(file, kRoot);

	REQUIRE(read.Tabs.size() == 1);
	CHECK(read.Tabs[0].ColumnIn == Core::ColumnUnit::Byte);
	CHECK(read.Tabs[0].Column == 7);
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

TEST_CASE("A path differing from the folder only in case is still stored relative to it")
{
	const std::filesystem::path root{"D:\\Work\\Repo"};
	const std::filesystem::path tab{"d:\\work\\REPO\\src\\Main.cpp"};

	Core::JsonFile file;
	WriteWorkspaceState(WorkspaceState{.Tabs = {TabState{tab}}}, root, file);

	CHECK(file.Get("restore.tab.0.path") == "src\\Main.cpp");

	const std::filesystem::path moved{"E:\\Elsewhere\\Repo"};
	CHECK(ReadWorkspaceState(file, moved).Tabs[0].Path == moved / "src" / "Main.cpp");
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

namespace {
	Core::Oid OidOf(std::uint8_t first, std::uint8_t last)
	{
		Core::Oid id;
		id.Bytes.fill(0xAB);
		id.Bytes.front() = first;
		id.Bytes.back() = last;
		return id;
	}

	void WriteSessionCount(Core::JsonFile& file, const std::string& count)
	{
		file.Set("restore.session.count", count);
	}
}

TEST_CASE("Session windows and diff windows survive a write and a read")
{
	WorkspaceState state;
	state.Tabs = {TabState{kRoot / "src" / "Main.cpp"}};
	state.Sessions = {SessionTabState{"0f1e2d3c-aaaa", "Fix the build", SessionTab::Cost},
					  SessionTabState{"4b5a6978-bbbb", "", SessionTab::Requests},
					  SessionTabState{"c0ffee00-cccc", "Talk", SessionTab::Conversation}};
	state.Diffs = {DiffTabState{OidOf(0x01, 0xFE), Core::DiffTarget::CommitToParent, "src\\Main.cpp"},
				   DiffTabState{std::nullopt, Core::DiffTarget::WorkTreeToIndex, "README.md"},
				   DiffTabState{std::nullopt, Core::DiffTarget::IndexToHead, "docs\\a.md"}};

	Core::JsonFile file;
	WriteWorkspaceState(state, kRoot, file);

	CHECK(ReadWorkspaceState(file, kRoot) == state);
	CHECK(file.Get("restore.session.0.tab") == "cost");
	CHECK(file.Get("restore.diff.0.commit") == OidOf(0x01, 0xFE).ToHex());
	CHECK(file.Get("restore.diff.1.commit").empty());
	CHECK(file.Get("restore.diff.1.target") == "unstaged");
}

TEST_CASE("A diff's path is relative to its repository and is kept as it is, wherever the folder moves")
{
	WorkspaceState state;
	state.Diffs = {DiffTabState{std::nullopt, Core::DiffTarget::WorkTreeToHead, "src\\Main.cpp"}};

	Core::JsonFile file;
	WriteWorkspaceState(state, kRoot, file);

	CHECK(file.Get("restore.diff.0.path") == "src\\Main.cpp");

	const WorkspaceState read = ReadWorkspaceState(file, std::filesystem::path{"E:\\Elsewhere\\Repo"});

	REQUIRE(read.Diffs.size() == 1);
	CHECK(read.Diffs[0].Path == std::filesystem::path{"src\\Main.cpp"});
}

TEST_CASE("A session's sub-tab that is not one of the three reads as the conversation")
{
	Core::JsonFile file;
	WriteSessionCount(file, "2");
	file.Set("restore.session.0.id", "a");
	file.Set("restore.session.0.tab", "timeline");
	file.Set("restore.session.1.id", "b");

	const WorkspaceState read = ReadWorkspaceState(file, kRoot);

	REQUIRE(read.Sessions.size() == 2);
	CHECK(read.Sessions[0].Tab == SessionTab::Conversation);
	CHECK(read.Sessions[1].Tab == SessionTab::Conversation);
}

TEST_CASE("A session with no id is dropped")
{
	Core::JsonFile file;
	WriteSessionCount(file, "2");
	file.Set("restore.session.0.title", "Nameless");
	file.Set("restore.session.1.id", "b");
	file.Set("restore.session.1.title", "Named");

	const WorkspaceState read = ReadWorkspaceState(file, kRoot);

	REQUIRE(read.Sessions.size() == 1);
	CHECK(read.Sessions[0].Id == "b");
	CHECK(read.Sessions[0].Title == "Named");
}

TEST_CASE("A diff with an unreadable commit, target or path is dropped")
{
	const std::string good = OidOf(0x10, 0x20).ToHex();

	Core::JsonFile file;
	file.Set("restore.diff.count", "7");

	file.Set("restore.diff.0.commit", "not-a-commit-at-all-not-a-commit-at-all!");
	file.Set("restore.diff.0.target", "commit");
	file.Set("restore.diff.0.path", "a.cpp");

	file.Set("restore.diff.1.commit", good.substr(1));
	file.Set("restore.diff.1.target", "commit");
	file.Set("restore.diff.1.path", "b.cpp");

	file.Set("restore.diff.2.target", "sideways");
	file.Set("restore.diff.2.path", "c.cpp");

	file.Set("restore.diff.3.commit", good);
	file.Set("restore.diff.3.target", "commit");

	file.Set("restore.diff.4.target", "commit");
	file.Set("restore.diff.4.path", "e.cpp");
	file.Set("restore.diff.5.commit", good);
	file.Set("restore.diff.5.target", "staged");
	file.Set("restore.diff.5.path", "f.cpp");

	std::string upper = good;
	for (char& digit : upper)
	{
		digit = static_cast<char>(std::toupper(static_cast<unsigned char>(digit)));
	}
	file.Set("restore.diff.6.commit", upper);
	file.Set("restore.diff.6.target", "commit");
	file.Set("restore.diff.6.path", "g.cpp");

	const WorkspaceState read = ReadWorkspaceState(file, kRoot);

	REQUIRE(read.Diffs.size() == 1);
	CHECK(read.Diffs[0].Commit == OidOf(0x10, 0x20));
	CHECK(read.Diffs[0].Target == Core::DiffTarget::CommitToParent);
	CHECK(read.Diffs[0].Path == std::filesystem::path{"g.cpp"});
}

TEST_CASE("Shorter session and diff lists leave none of the longer ones' keys behind")
{
	WorkspaceState longer;
	longer.Sessions = {SessionTabState{"a"}, SessionTabState{"b"}};
	longer.Diffs = {DiffTabState{std::nullopt, Core::DiffTarget::WorkTreeToIndex, "a.cpp"},
					DiffTabState{OidOf(1, 2), Core::DiffTarget::CommitToParent, "b.cpp"}};

	WorkspaceState shorter;
	shorter.Sessions = {SessionTabState{"a"}};
	shorter.Diffs = {DiffTabState{std::nullopt, Core::DiffTarget::WorkTreeToIndex, "a.cpp"}};

	Core::JsonFile file;
	WriteWorkspaceState(longer, kRoot, file);
	WriteWorkspaceState(shorter, kRoot, file);

	CHECK(file.Get("restore.session.1.id").empty());
	CHECK(file.Get("restore.diff.1.path").empty());
	CHECK(file.Get("restore.diff.1.commit").empty());
	CHECK(ReadWorkspaceState(file, kRoot) == shorter);
}

TEST_CASE("Absurd session and diff counts are capped")
{
	Core::JsonFile file;
	file.Set("restore.session.count", "4000000000");
	file.Set("restore.diff.count", "4000000000");
	file.Set("restore.session.0.id", "a");
	file.Set("restore.diff.0.target", "working");
	file.Set("restore.diff.0.path", "a.cpp");

	const WorkspaceState read = ReadWorkspaceState(file, kRoot);

	CHECK(read.Sessions.size() == 1);
	CHECK(read.Diffs.size() == 1);

	Core::JsonFile full;
	full.Set("restore.session.count", "300");

	for (int sessionIndex = 0; sessionIndex < 300; ++sessionIndex)
	{
		full.Set("restore.session." + std::to_string(sessionIndex) + ".id", "s" + std::to_string(sessionIndex));
	}

	CHECK(ReadWorkspaceState(full, kRoot).Sessions.size() == 256);
}

TEST_CASE("A file written before sessions and diffs were kept reads as none of either")
{
	Core::JsonFile file;
	file.Set("restore.tab.count", "1");
	file.Set("restore.tab.0.path", "a.cpp");

	const WorkspaceState read = ReadWorkspaceState(file, kRoot);

	CHECK(read.Tabs.size() == 1);
	CHECK(read.Sessions.empty());
	CHECK(read.Diffs.empty());
	CHECK_FALSE(read.Terminals.has_value());
}

TEST_CASE("Terminal windows survive a write and a read, their directory relative to the folder")
{
	WorkspaceState state;
	state.Terminals = std::vector<TerminalTabState>{
		TerminalTabState{"{A1}", "PowerShell 7", kRoot / "src"},
		TerminalTabState{"{B2}", "Command Prompt", std::filesystem::path{"E:\\Elsewhere"}}};

	Core::JsonFile file;
	WriteWorkspaceState(state, kRoot, file);

	CHECK(ReadWorkspaceState(file, kRoot) == state);
	CHECK(file.Get("restore.terminal.count") == "2");
	CHECK(file.Get("restore.terminal.0.profile") == "PowerShell 7");
	CHECK(file.Get("restore.terminal.0.directory") == "src");
}

TEST_CASE("A terminal's last title survives a write and a read")
{
	WorkspaceState state;
	state.Terminals = std::vector<TerminalTabState>{
		TerminalTabState{"{A1}", "PowerShell 7", kRoot, "vim main.cpp"},
		TerminalTabState{"{B2}", "Command Prompt", kRoot, ""}};

	Core::JsonFile file;
	WriteWorkspaceState(state, kRoot, file);

	const WorkspaceState read = ReadWorkspaceState(file, kRoot);

	CHECK(read == state);
	REQUIRE(read.Terminals.has_value());
	CHECK((*read.Terminals)[0].Title == "vim main.cpp");
	CHECK((*read.Terminals)[1].Title.empty());
}

TEST_CASE("A terminal written before titles were kept reads with an empty title")
{
	WorkspaceState state;
	state.Terminals = std::vector<TerminalTabState>{TerminalTabState{"{A1}", "PowerShell 7", kRoot, "build"}};

	Core::JsonFile file;
	WriteWorkspaceState(state, kRoot, file);
	file.Remove("restore.terminal.0.title");

	const WorkspaceState read = ReadWorkspaceState(file, kRoot);

	REQUIRE(read.Terminals.has_value());
	REQUIRE(read.Terminals->size() == 1);
	CHECK((*read.Terminals)[0].Id == "{A1}");
	CHECK((*read.Terminals)[0].Title.empty());
}

TEST_CASE("No terminals written at all reads as absent, and an empty list as empty")
{
	Core::JsonFile never;
	WriteWorkspaceState(WorkspaceState{}, kRoot, never);

	CHECK_FALSE(ReadWorkspaceState(never, kRoot).Terminals.has_value());

	WorkspaceState closed;
	closed.Terminals = std::vector<TerminalTabState>{};

	Core::JsonFile file;
	WriteWorkspaceState(closed, kRoot, file);

	const WorkspaceState read = ReadWorkspaceState(file, kRoot);

	REQUIRE(read.Terminals.has_value());
	CHECK(read.Terminals->empty());
}

TEST_CASE("A terminal with no id or no profile is dropped, and the rest are kept")
{
	Core::JsonFile file;
	file.Set("restore.terminal.count", "3");
	file.Set("restore.terminal.0.profile", "cmd");
	file.Set("restore.terminal.1.id", "{B}");
	file.Set("restore.terminal.2.id", "{C}");
	file.Set("restore.terminal.2.profile", "cmd");

	const WorkspaceState read = ReadWorkspaceState(file, kRoot);

	REQUIRE(read.Terminals.has_value());
	REQUIRE(read.Terminals->size() == 1);
	CHECK((*read.Terminals)[0].Id == "{C}");
}

TEST_CASE("A shorter terminal list leaves none of the longer one's keys behind")
{
	WorkspaceState longer;
	longer.Terminals = std::vector<TerminalTabState>{TerminalTabState{"{A}", "cmd", kRoot},
													 TerminalTabState{"{B}", "cmd", kRoot}};

	WorkspaceState shorter;
	shorter.Terminals = std::vector<TerminalTabState>{TerminalTabState{"{A}", "cmd", kRoot}};

	Core::JsonFile file;
	WriteWorkspaceState(longer, kRoot, file);
	WriteWorkspaceState(shorter, kRoot, file);

	CHECK(file.Get("restore.terminal.1.id").empty());
	CHECK(ReadWorkspaceState(file, kRoot) == shorter);
}

TEST_CASE("A flag reads as what was written, and anything else as the stated default")
{
	CHECK(ReadFlag(Flag(true), false));
	CHECK_FALSE(ReadFlag(Flag(false), true));

	for (const std::string stored : {std::string{}, std::string{"yes"}, std::string{"2"}, std::string{"true"}})
	{
		CHECK(ReadFlag(stored, true));
		CHECK_FALSE(ReadFlag(stored, false));
	}
}

TEST_CASE("A negative stored count or caret falls back to zero")
{
	Core::JsonFile file;
	file.Set("restore.tab.count", "-1");
	CHECK(ReadWorkspaceState(file, kRoot).Tabs.empty());

	file.Set("restore.tab.count", "1");
	file.Set("restore.tab.0.path", "a.cpp");
	file.Set("restore.tab.0.line", "-1");

	const WorkspaceState read = ReadWorkspaceState(file, kRoot);

	REQUIRE(read.Tabs.size() == 1);
	CHECK(read.Tabs[0].Line == 0);
}

TEST_CASE("A path that is the folder itself reads back as the folder, a drive root included")
{
	{
		WorkspaceState state;
		state.Terminals = std::vector<TerminalTabState>{TerminalTabState{"{T}", "cmd", kRoot}};

		Core::JsonFile file;
		WriteWorkspaceState(state, kRoot, file);

		const WorkspaceState read = ReadWorkspaceState(file, kRoot);

		REQUIRE(read.Terminals.has_value());
		REQUIRE(read.Terminals->size() == 1);
		CHECK((*read.Terminals)[0].WorkingDirectory == kRoot);
	}

	{
		const std::filesystem::path driveRoot = std::filesystem::path{R"(D:\)"};

		WorkspaceState state;
		state.Terminals = std::vector<TerminalTabState>{TerminalTabState{"{D}", "cmd", driveRoot}};

		Core::JsonFile file;
		WriteWorkspaceState(state, driveRoot, file);

		const WorkspaceState read = ReadWorkspaceState(file, driveRoot);

		REQUIRE(read.Terminals.has_value());
		REQUIRE(read.Terminals->size() == 1);
		CHECK((*read.Terminals)[0].WorkingDirectory == driveRoot);
	}
}

TEST_CASE("Open review tabs survive a write and a read with slashes and non-ASCII characters in their names")
{
	WorkspaceState state;
	state.Reviews.push_back(ReviewTabState{.Branch = "refs/heads/feature/x",
										   .Selected = Core::FromUtf8("\xC5\xB9r\xC3\xB3"
																	  "d\xC5\x82"
																	  "a/plik.cpp"),
										   .SelectedIsUncommitted = true});
	state.Reviews.push_back(ReviewTabState{.Branch = "refs/heads/side"});

	Core::JsonFile file;
	WriteWorkspaceState(state, kRoot, file);

	CHECK(ReadWorkspaceState(file, kRoot).Reviews == state.Reviews);
	CHECK(file.Get("restore.review.0.base").empty());
}

TEST_CASE("A stored review without a branch is dropped")
{
	Core::JsonFile file;
	file.Set("restore.review.count", "2");
	file.Set("restore.review.0.branch", "");
	file.Set("restore.review.1.branch", "refs/heads/side");

	const std::vector<ReviewTabState> reviews = ReadWorkspaceState(file, kRoot).Reviews;

	REQUIRE(reviews.size() == 1);
	CHECK(reviews.front().Branch == "refs/heads/side");
}

TEST_CASE("The branch list's sort and grouping survive a write and a read")
{
	WorkspaceState state;
	state.BranchList = BranchListState{.Sort = BranchSort::Oldest, .Group = BranchGrouping::Author};

	Core::JsonFile file;
	WriteWorkspaceState(state, kRoot, file);

	CHECK(ReadWorkspaceState(file, kRoot).BranchList == state.BranchList);
	CHECK(file.Get("restore.branchList.sort") == "oldest");
	CHECK(file.Get("restore.branchList.group") == "author");
}

TEST_CASE("A folder state without the branch list reads as newest first and ungrouped")
{
	const Core::JsonFile file;

	CHECK(ReadWorkspaceState(file, kRoot).BranchList == BranchListState{});
}

TEST_CASE("An unknown branch list sort reads as newest first")
{
	Core::JsonFile file;
	file.Set("restore.branchList.sort", "sideways");

	CHECK(ReadWorkspaceState(file, kRoot).BranchList.Sort == BranchSort::Newest);
}
