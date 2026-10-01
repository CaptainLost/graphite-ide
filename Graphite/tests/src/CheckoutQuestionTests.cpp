#include <doctest/doctest.h>

#include "Git/CheckoutQuestion.h"

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

	const auto question = Graphite::BuildCheckoutQuestion("feature/x", plan, status, {"Renderer.cpp", "Clean.cpp"});

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

	const auto question = Graphite::BuildCheckoutQuestion("feature/x", plan, status, {});

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
	const auto question = Graphite::BuildCheckoutQuestion("x", plan, status, {"Src\\Renderer.cpp"});
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

	const auto question = Graphite::BuildCheckoutQuestion("x", plan, status, {});

	REQUIRE(question.Untracked.size() == 2);
	CHECK(question.Untracked[0].Blocks);
	CHECK_FALSE(question.Untracked[1].Blocks);
	CHECK(question.UntrackedForced);
}

TEST_CASE("An unsaved untracked file is marked on its untracked row, not listed twice")
{
	Core::WorkingTreeStatus status;
	status.Entries = {Untracked("notes.txt")};

	const auto question = Graphite::BuildCheckoutQuestion("x", {}, status, {"notes.txt"});

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

	const auto question = Graphite::BuildCheckoutQuestion("x", {}, status, {});

	REQUIRE(question.Changed.size() == 2);
	CHECK(question.Changed[0].Path == "conflict.cpp");
	CHECK(question.Changed[1].Path == "staged.cpp");
}

TEST_CASE("A checkout's files reload silently except a document left unsaved that nobody confirmed losing")
{
	const std::vector<std::filesystem::path> changed = {"C:/repo/a.txt", "C:/repo/b.txt", "C:/repo/c.txt"};

	// b.txt was edited after the question was answered; c.txt was in the question and the
	// reader chose what happens to it.
	const auto silent =
		Graphite::SilentReloads(changed, {"C:/repo/b.txt", "C:/repo/c.txt"}, {"C:/repo/c.txt"});

	CHECK(silent == std::vector<std::filesystem::path>{"C:/repo/a.txt", "C:/repo/c.txt"});
}

TEST_CASE("A checkout that asked nothing reloads no document with unsaved edits silently")
{
	const auto silent = Graphite::SilentReloads({"C:/repo/a.txt", "C:/repo/b.txt"}, {"C:/repo/b.txt"}, {});

	CHECK(silent == std::vector<std::filesystem::path>{"C:/repo/a.txt"});
}

TEST_CASE("Silent reloads match documents whatever the case and separators")
{
	const auto silent = Graphite::SilentReloads({"C:/Repo/Src/A.txt", "C:/repo/src/b.txt"},
												{"c:\\repo\\src\\a.TXT", "C:\\REPO\\SRC\\B.txt"},
												{"c:/REPO/src/b.txt"});

	CHECK(silent == std::vector<std::filesystem::path>{"C:/repo/src/b.txt"});
}
