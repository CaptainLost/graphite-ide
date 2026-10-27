#include <doctest/doctest.h>

#include "Git/Diff/FileTexts.h"

#include "GitFixtures.h"
#include "Git/GitModel.h"

TEST_CASE("A successful read is present with its text")
{
	Core::Result<Graphite::SideText> side = Graphite::ReadSideText(std::string{"hello"});

	REQUIRE(side.HasValue());
	CHECK(side.GetValue().Present);
	CHECK(side.GetValue().Text == "hello");
}

TEST_CASE("An empty successful read is present and empty, not absent")
{
	Core::Result<Graphite::SideText> side = Graphite::ReadSideText(std::string{});

	REQUIRE(side.HasValue());
	CHECK(side.GetValue().Present);
	CHECK(side.GetValue().Text.empty());
}

TEST_CASE("A NotFound read is absent without failing")
{
	Core::Result<Graphite::SideText> side =
		Graphite::ReadSideText(Core::Error{Core::ErrorCode::NotFound, "no such path in this tree"});

	REQUIRE(side.HasValue());
	CHECK(!side.GetValue().Present);
	CHECK(side.GetValue().Text.empty());
}

TEST_CASE("Any other error stays an error")
{
	Core::Result<Graphite::SideText> side =
		Graphite::ReadSideText(Core::Error{Core::ErrorCode::IoFailure, "disk fell over"});

	REQUIRE(!side.HasValue());
	CHECK(side.GetError().Code == Core::ErrorCode::IoFailure);
}

TEST_CASE("Unstaged changes compare the index against the disk")
{
	const Graphite::WorkingTreeSides sides = Graphite::SidesFor(Core::DiffTarget::WorkTreeToIndex);

	CHECK(sides.Old == Graphite::TextSide::Index);
	CHECK(sides.New == Graphite::TextSide::WorkingTree);
}

TEST_CASE("Staged changes compare HEAD against the index")
{
	const Graphite::WorkingTreeSides sides = Graphite::SidesFor(Core::DiffTarget::IndexToHead);

	CHECK(sides.Old == Graphite::TextSide::Head);
	CHECK(sides.New == Graphite::TextSide::Index);
}

TEST_CASE("Everything since the last commit compares HEAD against the disk")
{
	const Graphite::WorkingTreeSides sides = Graphite::SidesFor(Core::DiffTarget::WorkTreeToHead);

	CHECK(sides.Old == Graphite::TextSide::Head);
	CHECK(sides.New == Graphite::TextSide::WorkingTree);
}

TEST_CASE("A target that names no working tree has no side to read")
{
	const Graphite::WorkingTreeSides commit = Graphite::SidesFor(Core::DiffTarget::CommitToParent);

	CHECK(commit.Old == Graphite::TextSide::Absent);
	CHECK(commit.New == Graphite::TextSide::Absent);

	const Graphite::WorkingTreeSides range = Graphite::SidesFor(Core::DiffTarget::RevisionToRevision);

	CHECK(range.Old == Graphite::TextSide::Absent);
	CHECK(range.New == Graphite::TextSide::Absent);
}

TEST_CASE("Tokens are stale when nothing has been lexed yet")
{
	const Graphite::FileTexts fresh{.Old = "a", .New = "b", .HasOld = true, .HasNew = true};

	CHECK_FALSE(Graphite::TokensStillGood(nullptr, fresh));
}

TEST_CASE("Tokens survive a refresh that brought the same text back")
{
	const Graphite::FileTexts held{.Old = "a", .New = "b", .HasOld = true, .HasNew = true};
	const Graphite::FileTexts fresh{.Old = "a", .New = "b", .HasOld = true, .HasNew = true};

	CHECK(Graphite::TokensStillGood(&held, fresh));
}

TEST_CASE("Tokens are stale once either side's text differs")
{
	const Graphite::FileTexts held{.Old = "a", .New = "b", .HasOld = true, .HasNew = true};

	const Graphite::FileTexts newerChanged{.Old = "a", .New = "typed", .HasOld = true, .HasNew = true};
	CHECK_FALSE(Graphite::TokensStillGood(&held, newerChanged));

	const Graphite::FileTexts olderChanged{.Old = "staged", .New = "b", .HasOld = true, .HasNew = true};
	CHECK_FALSE(Graphite::TokensStillGood(&held, olderChanged));
}

TEST_CASE("Tokens are stale when a side appears or disappears")
{
	const Graphite::FileTexts held{.Old = "", .New = "b", .HasOld = false, .HasNew = true};
	const Graphite::FileTexts fresh{.Old = "", .New = "b", .HasOld = true, .HasNew = true};

	CHECK_FALSE(Graphite::TokensStillGood(&held, fresh));
}

namespace {

	constexpr float kNoTimePasses = 0.0f;

	Core::FileDiff ChangedFile(const char* path)
	{
		Core::FileDiff file;
		file.Change = Core::FileChange::Modified;
		file.OldPath = path;
		file.NewPath = path;
		return file;
	}
}

TEST_CASE("A commit's texts on their way are loading, and once held nothing is waited for")
{
	EmptyGitPort port;
	std::vector<std::function<void()>> held;
	Graphite::GitModel model(port, [&held](std::function<void()> job) { held.push_back(std::move(job)); });
	model.SetRoot("C:/repo");

	Graphite::FileTextsBinding binding;
	const Core::FileDiff file = ChangedFile("a.txt");

	binding.Update(model, OidWithFirstByte(1), file);

	CHECK(binding.Wait.Progress == Graphite::TextsProgress::Loading);

	for (const std::function<void()>& job : held)
	{
		job();
	}

	model.OnUpdate(Core::Timestep{kNoTimePasses});
	binding.Update(model, OidWithFirstByte(1), file);

	CHECK(binding.Texts != nullptr);
	CHECK(binding.Wait == Graphite::TextsWait{});
}

TEST_CASE("A commit's texts that could not be read say why")
{
	EmptyGitPort port;
	port.TextsFailure = Core::Error{Core::ErrorCode::IoFailure, "blob missing"};
	Graphite::GitModel model(port, RunJobsInline());
	model.SetRoot("C:/repo");

	Graphite::FileTextsBinding binding;
	const Core::FileDiff file = ChangedFile("a.txt");

	binding.Update(model, OidWithFirstByte(1), file);
	model.OnUpdate(Core::Timestep{kNoTimePasses});
	binding.Update(model, OidWithFirstByte(1), file);

	CHECK(binding.Wait == Graphite::TextsWait{.Progress = Graphite::TextsProgress::Failed, .Reason = "blob missing"});
}

TEST_CASE("A binary file's texts are never waited for")
{
	EmptyGitPort port;
	Graphite::GitModel model(port, RunJobsInline());
	model.SetRoot("C:/repo");

	Graphite::FileTextsBinding binding;
	Core::FileDiff file = ChangedFile("logo.png");
	file.IsBinary = true;

	binding.Update(model, OidWithFirstByte(1), file);

	CHECK(binding.Wait == Graphite::TextsWait{});
}

TEST_CASE("A working-tree diff's texts that could not be read say why")
{
	EmptyGitPort port;
	port.DiffAnswer.Files.push_back(ChangedFile("a.txt"));
	port.TextsFailure = Core::Error{Core::ErrorCode::IoFailure, "sharing violation"};
	Graphite::GitModel model(port, RunJobsInline());
	model.SetRoot("C:/repo");

	model.RequestWorkingTreeDiff({.Target = Core::DiffTarget::WorkTreeToIndex, .File = "a.txt"});
	model.OnUpdate(Core::Timestep{kNoTimePasses});

	Graphite::FileTextsBinding binding;
	binding.UpdateFromWorkingTree(model, model.GetWorkingTreeDiff()->Files.front());

	CHECK(binding.Wait == Graphite::TextsWait{.Progress = Graphite::TextsProgress::Failed, .Reason = "sharing violation"});
}