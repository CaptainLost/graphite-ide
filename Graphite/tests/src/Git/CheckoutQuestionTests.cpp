#include <doctest/doctest.h>

#include "Git/CheckoutQuestion.h"

#include "Core/FileSystem/FileSystem.h"

namespace {

	Core::FileStatus Modified(const char* path)
	{
		Core::FileStatus entry;
		entry.Path = path;
		entry.WorkTree = Core::StatusFlags::Modified;
		return entry;
	}

	Core::FileStatus Untracked(const char* path)
	{
		Core::FileStatus entry;
		entry.Path = path;
		entry.IsUntracked = true;
		return entry;
	}
}

TEST_CASE("No dialog when nothing would be overwritten and no unsaved buffer would be")
{
	Core::CheckoutPlan plan;
	plan.Updated = {"a.txt"};

	CHECK_FALSE(Graphite::NeedsCheckoutDialog(plan, {}));
	CHECK_FALSE(Graphite::NeedsCheckoutDialog(plan, {"b.txt"}));
}

TEST_CASE("The dialog is needed for an overwritten file or an unsaved buffer the checkout would change")
{
	Core::CheckoutPlan overwritten;
	overwritten.Overwritten = {"a.txt"};
	CHECK(Graphite::NeedsCheckoutDialog(overwritten, {}));

	Core::CheckoutPlan updated;
	updated.Updated = {"a.txt"};
	CHECK(Graphite::NeedsCheckoutDialog(updated, {"a.txt"}));
}

TEST_CASE("The question lists every change, marks unsaved ones, and keeps untracked apart")
{
	Core::CheckoutPlan plan;
	plan.Overwritten = {"Renderer.cpp"};

	Core::WorkingTreeStatus status;
	status.Entries = {Modified("Shader.cpp"), Modified("Renderer.cpp"), Untracked("notes.txt")};

	const Graphite::CheckoutQuestion question = Graphite::BuildCheckoutQuestion("feature/x", plan, status, {"Renderer.cpp", "Clean.cpp"});

	CHECK(question.Title == "Checking out feature/x would overwrite your changes");

	REQUIRE(question.Changed.size() == 3);
	CHECK(question.Changed[0].Path == "Clean.cpp");
	CHECK(question.Changed[0].Unsaved);
	CHECK(question.Changed[1].Path == "Renderer.cpp");
	CHECK(question.Changed[1].Unsaved);
	CHECK(question.Changed[1].Blocks);
	CHECK(question.Changed[2].Path == "Shader.cpp");
	CHECK_FALSE(question.Changed[2].Unsaved);

	REQUIRE(question.Untracked.size() == 1);
	CHECK(question.Untracked[0].Path == "notes.txt");
	CHECK_FALSE(question.UntrackedForced);
}

TEST_CASE("An untracked file the checkout would overwrite forces the untracked box")
{
	Core::CheckoutPlan plan;
	plan.Overwritten = {"notes.txt"};

	Core::WorkingTreeStatus status;
	status.Entries = {Untracked("notes.txt")};

	const Graphite::CheckoutQuestion question = Graphite::BuildCheckoutQuestion("feature/x", plan, status, {});

	REQUIRE(question.Untracked.size() == 1);
	CHECK(question.Untracked[0].Blocks);
	CHECK(question.UntrackedForced);
}

TEST_CASE("A branch name is refused when invalid or taken")
{
	Core::Branch main;
	main.Name = "main";
	main.Kind = Core::RefKind::Local;

	Core::Branch remote;
	remote.Name = "origin/topic";
	remote.Kind = Core::RefKind::Remote;

	const std::vector<Core::Branch> branches{main, remote};

	CHECK_FALSE(Graphite::BranchNameProblem("topic", branches).has_value());
	CHECK(Graphite::BranchNameProblem("main", branches) == "A branch named main already exists");
	CHECK(Graphite::BranchNameProblem("", branches) == "Enter a name");
	CHECK(Graphite::BranchNameProblem("has space", branches) == "Not a valid branch name");
}

TEST_CASE("Checkout targets are labelled the way the ref list names them")
{
	CHECK(Graphite::CheckoutTargetLabel({.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature/x"}) == "feature/x");
	CHECK(Graphite::CheckoutTargetLabel({.Kind = Core::CheckoutKind::RemoteBranch, .Name = "origin/x"}) == "origin/x");

	Core::Oid id;
	id.Bytes[0] = 0x7d;
	id.Bytes[1] = 0x49;
	CHECK(Graphite::CheckoutTargetLabel({.Kind = Core::CheckoutKind::Commit, .Commit = id}) == "7d49000");
}

TEST_CASE("An unsaved path spelled with another case and separators still matches the plan")
{
	Core::CheckoutPlan plan;
	plan.Updated = {"src/Renderer.cpp"};

	CHECK(Graphite::NeedsCheckoutDialog(plan, {"Src\\Renderer.cpp"}));

	Core::WorkingTreeStatus status;
	const Graphite::CheckoutQuestion question = Graphite::BuildCheckoutQuestion("x", plan, status, {"Src\\Renderer.cpp"});
	REQUIRE(question.Changed.size() == 1);
	CHECK(question.Changed[0].Path == "src/Renderer.cpp");
}

TEST_CASE("A collapsed untracked directory blocks when the plan overwrites a file inside it")
{
	Core::FileStatus dir = Untracked("dir");
	dir.IsDirectory = true;
	Core::FileStatus other = Untracked("dir2");
	other.IsDirectory = true;

	Core::CheckoutPlan plan;
	plan.Overwritten = {"dir/file.txt"};

	Core::WorkingTreeStatus status;
	status.Entries = {dir, other};

	const Graphite::CheckoutQuestion question = Graphite::BuildCheckoutQuestion("x", plan, status, {});

	REQUIRE(question.Untracked.size() == 2);
	CHECK(question.Untracked[0].Blocks);
	CHECK_FALSE(question.Untracked[1].Blocks);
	CHECK(question.UntrackedForced);
}

TEST_CASE("An unsaved untracked file is marked on its untracked row, not listed twice")
{
	Core::WorkingTreeStatus status;
	status.Entries = {Untracked("notes.txt")};

	const Graphite::CheckoutQuestion question = Graphite::BuildCheckoutQuestion("x", {}, status, {"notes.txt"});

	CHECK(question.Changed.empty());
	REQUIRE(question.Untracked.size() == 1);
	CHECK(question.Untracked[0].Unsaved);
}

TEST_CASE("Staged-only and conflicted entries are listed as changes")
{
	Core::FileStatus staged;
	staged.Path = "staged.cpp";
	staged.Index = Core::StatusFlags::Modified;

	Core::FileStatus conflicted;
	conflicted.Path = "conflict.cpp";
	conflicted.IsConflicted = true;

	Core::WorkingTreeStatus status;
	status.Entries = {staged, conflicted};

	const Graphite::CheckoutQuestion question = Graphite::BuildCheckoutQuestion("x", {}, status, {});

	REQUIRE(question.Changed.size() == 2);
	CHECK(question.Changed[0].Path == "conflict.cpp");
	CHECK(question.Changed[1].Path == "staged.cpp");
}

TEST_CASE("A checkout's files reload silently except a document with unsaved edits, even one the dialog reverted or saved")
{
	const std::vector<std::filesystem::path> changed = {"C:/repo/a.txt", "C:/repo/b.txt", "C:/repo/c.txt"};

	const std::vector<std::filesystem::path> silent =
		Graphite::SilentReloads(changed, {"C:/repo/b.txt", "C:/repo/c.txt"});

	CHECK(silent == std::vector<std::filesystem::path>{"C:/repo/a.txt"});
}

TEST_CASE("A checkout that asked nothing reloads no document with unsaved edits silently")
{
	const std::vector<std::filesystem::path> silent = Graphite::SilentReloads({"C:/repo/a.txt", "C:/repo/b.txt"}, {"C:/repo/b.txt"});

	CHECK(silent == std::vector<std::filesystem::path>{"C:/repo/a.txt"});
}

TEST_CASE("Silent reloads match documents whatever the case and separators")
{
	const std::vector<std::filesystem::path> silent = Graphite::SilentReloads({"C:/Repo/Src/A.txt", "C:/repo/src/b.txt"},
																			  {"c:\\repo\\src\\a.TXT"});

	CHECK(silent == std::vector<std::filesystem::path>{"C:/repo/src/b.txt"});
}

namespace {

	std::string Utf8(const char8_t* text)
	{
		return std::string(reinterpret_cast<const char*>(text));
	}
}

TEST_CASE("Paths outside the ANSI code page are matched and labelled as UTF-8")
{
	Core::CheckoutPlan plan;
	plan.Updated = {std::filesystem::path{u8"źródło/日本.cpp"}};

	// WARNING: The ANSI code page narrows both of these names to question marks.
	CHECK(Graphite::NeedsCheckoutDialog(plan, {std::filesystem::path{u8"źródło/日本.cpp"}}));
	CHECK_FALSE(Graphite::NeedsCheckoutDialog(plan, {std::filesystem::path{u8"źródło/中国.cpp"}}));

	Core::CheckoutPlan overwritten;
	overwritten.Overwritten = {std::filesystem::path{u8"źródło/plik.cpp"}};

	Core::FileStatus entry;
	entry.Path = std::filesystem::path{u8"źródło/plik.cpp"};
	entry.WorkTree = Core::StatusFlags::Modified;

	Core::WorkingTreeStatus status;
	status.Entries = {entry};

	const Graphite::CheckoutQuestion question = Graphite::BuildCheckoutQuestion("feature", overwritten, status,
																				{std::filesystem::path{u8"źródło/plik.cpp"}});

	REQUIRE(question.Changed.size() == 1);
	CHECK(question.Changed[0].Blocks);
	CHECK(question.Changed[0].Unsaved);
	CHECK(Core::ToGenericUtf8(question.Changed[0].Path) == Utf8(u8"źródło/plik.cpp"));
	CHECK(Graphite::CheckoutRowTag(question.Changed[0]) == "unsaved, blocks checkout");
}

TEST_CASE("A row's tag says what makes it matter, and nothing for a plain change")
{
	CHECK(Graphite::CheckoutRowTag({.Path = "a.txt", .Unsaved = false, .Blocks = false}).empty());
	CHECK(Graphite::CheckoutRowTag({.Path = "a.txt", .Unsaved = true, .Blocks = false}) == "unsaved");
	CHECK(Graphite::CheckoutRowTag({.Path = "a.txt", .Unsaved = false, .Blocks = true}) == "blocks checkout");
	CHECK(Graphite::CheckoutRowTag({.Path = "a.txt", .Unsaved = true, .Blocks = true}) ==
		  "unsaved, blocks checkout");
}

TEST_CASE("Only checking out a bare commit asks about detaching, since a remote branch is checked out as a local one that tracks it")
{
	Core::CheckoutTarget commit;
	commit.Kind = Core::CheckoutKind::Commit;
	CHECK(Graphite::AsksBeforeDetaching(commit));

	CHECK_FALSE(Graphite::AsksBeforeDetaching({.Kind = Core::CheckoutKind::LocalBranch, .Name = "main"}));

	CHECK_FALSE(Graphite::AsksBeforeDetaching({.Kind = Core::CheckoutKind::RemoteBranch, .Name = "origin/x"}));
}

TEST_CASE("The detached question names the commit and says it belongs to no branch")
{
	const std::string body = Graphite::DetachedCheckoutBody("1a2b3c4");

	CHECK(body.find("1a2b3c4") != std::string::npos);
	CHECK(body.find("no branch") != std::string::npos);
}

TEST_CASE("A plan that needs the reader's answer while the Git panel is hidden is held, never checked out")
{
	const Graphite::CheckoutPlanVerdict verdict =
		Graphite::DecideOnCheckoutPlan({.Failed = false, .NeedsDialog = true, .PanelOnScreen = false});

	CHECK(verdict == Graphite::CheckoutPlanVerdict::HoldForPanel);
}

TEST_CASE("A plan is asked about on screen, checked out when it needs no answer, and reported when it failed")
{
	CHECK(Graphite::DecideOnCheckoutPlan({.Failed = false, .NeedsDialog = true, .PanelOnScreen = true}) ==
		  Graphite::CheckoutPlanVerdict::Ask);
	CHECK(Graphite::DecideOnCheckoutPlan({.Failed = false, .NeedsDialog = false, .PanelOnScreen = false}) ==
		  Graphite::CheckoutPlanVerdict::CheckOut);
	CHECK(Graphite::DecideOnCheckoutPlan({.Failed = true, .NeedsDialog = true, .PanelOnScreen = true}) ==
		  Graphite::CheckoutPlanVerdict::ReportFailure);
}

TEST_CASE("A held checkout gives nothing back however long the panel stays hidden, then its target once on screen")
{
	constexpr bool kHidden = false;
	constexpr bool kOnScreen = true;
	constexpr int kFramesHidden = 600;
	const Core::CheckoutTarget feature{.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature/x"};

	Graphite::HeldCheckout held;
	held.Hold(feature);

	bool handedOutWhileHidden = false;

	for (int frame = 0; frame < kFramesHidden; ++frame)
	{
		handedOutWhileHidden = handedOutWhileHidden || held.TakeOnceOnScreen(kHidden).has_value();
	}

	CHECK_FALSE(handedOutWhileHidden);
	CHECK(held.HeldTarget() == feature);

	CHECK(held.TakeOnceOnScreen(kOnScreen) == feature);
	CHECK_FALSE(held.TakeOnceOnScreen(kOnScreen).has_value());
	CHECK_FALSE(held.HeldTarget().has_value());
}