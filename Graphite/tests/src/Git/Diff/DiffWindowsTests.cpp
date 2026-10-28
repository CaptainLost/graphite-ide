#include <doctest/doctest.h>

#include "Git/Diff/DiffWindows.h"
#include "UI/FileIcons.h"

#include "GitFixtures.h"
#include "ImGuiFrames.h"

#include "Core/Git/Status.h"
#include "Core/ImGui/TabCloseRequests.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <string>

namespace {

	Graphite::OpenDiff Diff(std::uint8_t commit, const char* path)
	{
		return Graphite::OpenDiff{.Commit = OidWithFirstByte(commit),
								  .Target = Core::DiffTarget::CommitToParent,
								  .Path = path};
	}
}

TEST_CASE("Two files of one commit have different identities")
{
	CHECK(Graphite::DiffWindowIdentity(Diff(1, "a.txt")) != Graphite::DiffWindowIdentity(Diff(1, "b.txt")));
}

TEST_CASE("One file in two commits has different identities")
{
	CHECK(Graphite::DiffWindowIdentity(Diff(1, "a.txt")) != Graphite::DiffWindowIdentity(Diff(2, "a.txt")));
}

TEST_CASE("An identity is the same every time it is asked for")
{
	CHECK(Graphite::DiffWindowIdentity(Diff(1, "a.txt")) == Graphite::DiffWindowIdentity(Diff(1, "a.txt")));
}

TEST_CASE("An identity is the commit's full id immediately followed by the path")
{
	const std::string identity = Graphite::DiffWindowIdentity(Diff(1, "a.txt"));

	CHECK(identity == OidWithFirstByte(1).ToHex() + "a.txt");
}

TEST_CASE("Paths that differ only in their separators are told apart")
{
	CHECK(Graphite::DiffWindowIdentity(Diff(1, "a/b.txt")) != Graphite::DiffWindowIdentity(Diff(1, "a\\b.txt")));
}

TEST_CASE("The title carries the file's name and the commit's short id")
{
	const std::string title = Graphite::DiffWindowTitle(Diff(0xAB, "src/Editor/Caret.cpp"));

	CHECK(title.find("Caret.cpp") != std::string::npos);
	CHECK(title.find("ab") != std::string::npos);

	CHECK(title.find("Editor") == std::string::npos);
}

TEST_CASE("A window name shows the title and identifies by the identity")
{
	const Graphite::OpenDiff diff = Diff(1, "a.txt");
	const std::string name = Graphite::DiffWindowName(diff);

	const std::size_t marker = name.find("###");

	REQUIRE(marker != std::string::npos);
	CHECK(name.substr(marker + 3) == Graphite::DiffWindowIdentity(diff));
	CHECK(name.find(Graphite::DiffWindowTitle(diff)) < marker);
}

TEST_CASE("A diff's window name starts with its file kind's tab glyph, not its badge, and keeps its identity after the marker")
{
	const Graphite::OpenDiff diff = Diff(1, "docs/README.md");
	const std::string name = Graphite::DiffWindowName(diff);

	CHECK(name.starts_with(Graphite::GlyphOf(Graphite::FileKind::Markdown)));
	CHECK_FALSE(name.starts_with(Graphite::BadgeOf(Graphite::FileKind::Markdown)));
	CHECK(name.ends_with("###" + Graphite::DiffWindowIdentity(diff)));
}

TEST_CASE("A working-tree diff and a commit's diff of the same file are different tabs")
{
	const Graphite::OpenDiff fromCommit{
		.Commit = Core::Oid{}, .Target = Core::DiffTarget::CommitToParent, .Path = "a.txt"};

	const Graphite::OpenDiff fromWorkTree{
		.Commit = std::nullopt, .Target = Core::DiffTarget::WorkTreeToIndex, .Path = "a.txt"};

	CHECK(Graphite::DiffWindowIdentity(fromCommit) != Graphite::DiffWindowIdentity(fromWorkTree));
}

TEST_CASE("The staged and unstaged views of one file are different tabs")
{
	const Graphite::OpenDiff unstaged{
		.Commit = std::nullopt, .Target = Core::DiffTarget::WorkTreeToIndex, .Path = "a.txt"};

	const Graphite::OpenDiff staged{
		.Commit = std::nullopt, .Target = Core::DiffTarget::IndexToHead, .Path = "a.txt"};

	CHECK(Graphite::DiffWindowIdentity(unstaged) != Graphite::DiffWindowIdentity(staged));
}

TEST_CASE("A working-tree tab is titled by its file and what it compares")
{
	const Graphite::OpenDiff unstaged{
		.Commit = std::nullopt, .Target = Core::DiffTarget::WorkTreeToIndex, .Path = "src/a.txt"};

	const std::string title = Graphite::DiffWindowTitle(unstaged);

	CHECK(title.find("a.txt") != std::string::npos);
	CHECK(title.find("src") == std::string::npos);
}

TEST_CASE("A commit tab's identity is unchanged from before OpenDiff could name the working tree")
{
	const Graphite::OpenDiff diff = Diff(1, "a.txt");

	CHECK(Graphite::DiffWindowIdentity(diff) == OidWithFirstByte(1).ToHex() + "a.txt");
}

TEST_CASE("Opening the same working-tree diff twice adds one tab")
{
	Graphite::DiffWindows windows;

	const Graphite::OpenDiff unstaged{
		.Commit = std::nullopt, .Target = Core::DiffTarget::WorkTreeToIndex, .Path = "a.txt"};

	windows.Open(unstaged);
	windows.Open(unstaged);

	CHECK(windows.GetCount() == 1);
}

TEST_CASE("The working-tree tab's diff is known while it is open")
{
	Graphite::DiffWindows windows;
	CHECK_FALSE(windows.WorkingTreeDiff().has_value());

	windows.Open(Diff(1, "commit.txt"));
	CHECK_FALSE(windows.WorkingTreeDiff().has_value());

	const Graphite::OpenDiff staged{.Commit = std::nullopt, .Target = Core::DiffTarget::IndexToHead, .Path = "a.txt"};
	windows.Open(staged);
	CHECK(windows.WorkingTreeDiff() == staged);

	windows.CloseWorkingTree();
	CHECK_FALSE(windows.WorkingTreeDiff().has_value());
}

namespace {

	Graphite::OpenDiff WorkingDiff(Core::DiffTarget target, const char* path)
	{
		return Graphite::OpenDiff{.Commit = std::nullopt, .Target = target, .Path = path};
	}

	Core::WorkingTreeStatus StatusOf(const char* path, Core::StatusFlags index,
									 Core::StatusFlags workTree, bool isConflicted = false)
	{
		Core::WorkingTreeStatus status;
		status.Entries.push_back(Core::FileStatus{.Path = path,
												  .Index = index,
												  .WorkTree = workTree,
												  .IsConflicted = isConflicted});
		return status;
	}
}

TEST_CASE("A staged tab whose file was unstaged follows it to the unstaged side")
{
	const Core::WorkingTreeStatus status =
		StatusOf("a.txt", Core::StatusFlags::None, Core::StatusFlags::Modified);

	CHECK(Graphite::TargetHolding(status, "a.txt", Core::DiffTarget::IndexToHead) == Core::DiffTarget::WorkTreeToIndex);
}

TEST_CASE("An unstaged tab whose file was staged follows it to the staged side")
{
	const Core::WorkingTreeStatus status =
		StatusOf("a.txt", Core::StatusFlags::Modified, Core::StatusFlags::None);

	CHECK(Graphite::TargetHolding(status, "a.txt", Core::DiffTarget::WorkTreeToIndex) == Core::DiffTarget::IndexToHead);
}

TEST_CASE("A tab whose own side still holds a change stays where it is")
{
	const Core::WorkingTreeStatus status =
		StatusOf("a.txt", Core::StatusFlags::Modified, Core::StatusFlags::Modified);

	CHECK(!Graphite::TargetHolding(status, "a.txt", Core::DiffTarget::IndexToHead).has_value());
	CHECK(!Graphite::TargetHolding(status, "a.txt", Core::DiffTarget::WorkTreeToIndex)
			   .has_value());
}

TEST_CASE("A tab whose file has no change anywhere stays where it is")
{
	CHECK(!Graphite::TargetHolding(Core::WorkingTreeStatus{}, "a.txt",
								   Core::DiffTarget::WorkTreeToIndex)
			   .has_value());
}

TEST_CASE("A conflicted file's tab stays where it is")
{
	constexpr bool conflicted = true;
	const Core::WorkingTreeStatus status =
		StatusOf("a.txt", Core::StatusFlags::Modified, Core::StatusFlags::Modified, conflicted);

	CHECK(!Graphite::TargetHolding(status, "a.txt", Core::DiffTarget::WorkTreeToIndex)
			   .has_value());
}

TEST_CASE("Only the staged and unstaged pair follow anything")
{
	const Core::WorkingTreeStatus status =
		StatusOf("a.txt", Core::StatusFlags::Modified, Core::StatusFlags::None);

	CHECK(!Graphite::TargetHolding(status, "a.txt", Core::DiffTarget::WorkTreeToHead).has_value());
	CHECK(!Graphite::TargetHolding(status, "a.txt", Core::DiffTarget::CommitToParent).has_value());
}

TEST_CASE("An empty working-tree diff says which side is empty, and for which file")
{
	CHECK(Graphite::EmptyDiffMessage(WorkingDiff(Core::DiffTarget::IndexToHead, "src/a.txt")) == "a.txt has no staged changes.");
	CHECK(Graphite::EmptyDiffMessage(WorkingDiff(Core::DiffTarget::WorkTreeToIndex, "src/a.txt")) == "a.txt has no unstaged changes.");
	CHECK(Graphite::EmptyDiffMessage(WorkingDiff(Core::DiffTarget::WorkTreeToHead, "src/a.txt")) == "a.txt has no changes.");
}

TEST_CASE("An empty commit diff still reports a file missing from that commit")
{
	CHECK(Graphite::EmptyDiffMessage(Diff(1, "a.txt")) == "That file is not in this diff.");
}

namespace {

	constexpr const char* kPanelInFront = "Git";

	bool IsFocused(const std::string& name)
	{
		const ImGuiWindow* focused = ImGui::GetCurrentContext()->NavWindow;
		return focused != nullptr && std::string{focused->Name} == name;
	}
}

TEST_CASE("A diff opened while another window has the focus takes the focus as it appears")
{
	ImGuiFrames frames;
	EmptyGitPort port;
	Graphite::GitModel model(port, RunJobsInline());
	const Graphite::GitPalette palette;
	const Graphite::SyntaxPalette syntax;
	Core::TabCloseRequests closeRequests;
	Graphite::DiffWindows windows;

	constexpr ImU32 kNoSelectionColour = 0;
	const Graphite::DiffViewOptions options;
	constexpr unsigned int kNoCentreNode = 0;

	frames.Next([] {
		ImGui::SetNextWindowFocus();
		ImGui::Begin(kPanelInFront);
		ImGui::End();
	});
	REQUIRE(IsFocused(kPanelInFront));

	const Graphite::OpenDiff unstaged = WorkingDiff(Core::DiffTarget::WorkTreeToIndex, "a.txt");
	windows.Open(unstaged);

	frames.Next([&] {
		ImGui::Begin(kPanelInFront);
		ImGui::End();

		windows.Render(model, palette, syntax, kNoSelectionColour, options, kNoCentreNode, closeRequests);
	});
	closeRequests.EndFrame();

	CHECK(IsFocused(Graphite::DiffWindowName(unstaged)));
}

namespace {

	constexpr float kNoTimePasses = 0.0f;

	Core::FileDiff ModifiedFile(const char* path)
	{
		Core::FileDiff file;
		file.Change = Core::FileChange::Modified;
		file.NewPath = path;
		file.OldPath = path;
		return file;
	}
}

TEST_CASE("A commit diff still on its way has no failure to show, so its tab shows the loader")
{
	EmptyGitPort port;
	Graphite::GitModel model(port, [](std::function<void()>) {});
	model.SetRoot("C:/repo");

	const Graphite::OpenDiff diff = Diff(0xa1, "a.txt");
	model.RequestDiffFor(*diff.Commit);

	CHECK_FALSE(Graphite::DiffFailureOf(diff, model).has_value());
	CHECK(model.GetDiffFor(*diff.Commit) == nullptr);
}

TEST_CASE("A commit diff that failed names the commit and the reason")
{
	EmptyGitPort port;
	port.DiffFailure = Core::Error{Core::ErrorCode::IoFailure, "object not found"};
	Graphite::GitModel model(port, RunJobsInline());
	model.SetRoot("C:/repo");

	const Graphite::OpenDiff diff = Diff(0xa1, "a.txt");
	model.RequestDiffFor(*diff.Commit);
	model.OnUpdate(Core::Timestep{kNoTimePasses});

	CHECK(Graphite::DiffFailureOf(diff, model) ==
		  std::optional<std::string>{"Couldn't read the diff of " + diff.Commit->ShortHex() + ": object not found"});
}

TEST_CASE("Retry forgets a failed commit diff and asks again, so the loader shows at once and the diff can land")
{
	EmptyGitPort port;
	port.DiffFailure = Core::Error{Core::ErrorCode::IoFailure, "busy"};
	Graphite::GitModel model(port, RunJobsInline());
	model.SetRoot("C:/repo");

	const Graphite::OpenDiff diff = Diff(0xa1, "a.txt");
	model.RequestDiffFor(*diff.Commit);
	model.OnUpdate(Core::Timestep{kNoTimePasses});
	REQUIRE(Graphite::DiffFailureOf(diff, model).has_value());

	port.DiffFailure.reset();
	Graphite::RetryDiff(diff, model);

	CHECK_FALSE(Graphite::DiffFailureOf(diff, model).has_value());

	model.OnUpdate(Core::Timestep{kNoTimePasses});

	CHECK(model.GetDiffFor(*diff.Commit) != nullptr);
}

TEST_CASE("A working-tree diff that failed names the file, and Retry shows the loader at once")
{
	EmptyGitPort port;
	port.DiffFailure = Core::Error{Core::ErrorCode::IoFailure, "index locked"};
	Graphite::GitModel model(port, RunJobsInline());
	model.SetRoot("C:/repo");

	const Graphite::OpenDiff diff{.Target = Core::DiffTarget::WorkTreeToIndex, .Path = "src/a.txt"};
	model.RequestWorkingTreeDiff({.Target = diff.Target, .File = diff.Path});
	model.OnUpdate(Core::Timestep{kNoTimePasses});

	CHECK(Graphite::DiffFailureOf(diff, model) ==
		  std::optional<std::string>{"Couldn't read the diff of a.txt: index locked"});

	Graphite::RetryDiff(diff, model);

	CHECK(model.IsWorkingTreeDiffLoading());
	CHECK_FALSE(Graphite::DiffFailureOf(diff, model).has_value());
}

TEST_CASE("A working-tree diff whose texts could not be read says why beside the diff")
{
	EmptyGitPort port;
	port.DiffAnswer.Files.push_back(ModifiedFile("a.txt"));
	port.TextsFailure = Core::Error{Core::ErrorCode::IoFailure, "access denied"};
	Graphite::GitModel model(port, RunJobsInline());
	model.SetRoot("C:/repo");

	model.RequestWorkingTreeDiff({.Target = Core::DiffTarget::WorkTreeToIndex, .File = "a.txt"});
	model.OnUpdate(Core::Timestep{kNoTimePasses});

	REQUIRE(model.GetWorkingTreeDiff() != nullptr);
	CHECK(model.GetWorkingTreeTexts() == nullptr);
	REQUIRE(model.GetWorkingTreeTextsFailure().has_value());
	CHECK(model.GetWorkingTreeTextsFailure()->Message == "access denied");
}