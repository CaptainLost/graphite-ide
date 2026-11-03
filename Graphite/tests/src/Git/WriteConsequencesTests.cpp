#include <doctest/doctest.h>

#include "Git/Changes/ChangesShared.h"
#include "Git/GitModel.h"
#include "Git/WriteConsequences.h"

#include "Git/FakeEditorBuffers.h"
#include "GitFixtures.h"

#include "Core/Base/Timestep.h"

#include <filesystem>
#include <vector>

namespace {

	class LandingPort final : public EmptyGitPort
	{
	public:
		Core::CheckoutOutcome Checkout(const Core::CheckoutTarget&) override { return CheckoutResult; }

		Core::Result<void> DeleteUntracked(const Paths&) override
		{
			if (DeleteFails)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "the file is locked"};
			}

			return Core::Result<void>::Ok();
		}

		Core::CheckoutOutcome CheckoutResult;
		bool DeleteFails = false;
	};

	// A repository at C:/repo whose status has landed, with the consequences applied after every frame.
	struct Landing
	{
		Landing()
		{
			Model.SetRoot("C:/repo");
			Model.RequestStatus();
			Model.OnUpdate(Core::Timestep{0.0f});
		}

		void Frame()
		{
			Model.OnUpdate(Core::Timestep{0.0f});
			Consequences.Apply();
		}

		LandingPort Port;
		Graphite::GitModel Model{Port, RunJobsInline()};
		FakeEditorBuffers Editor;
		Graphite::ChangesShared Draft;
		std::vector<Graphite::GitModel::WriteFailure> Failures;
		int FilesWritten = 0;
		Graphite::WriteConsequences Consequences{
			Model, Editor, Draft, [this](const Graphite::GitModel::WriteFailure& failure) { Failures.push_back(failure); },
			[this] { ++FilesWritten; }};
	};

	std::filesystem::path InRepository(const std::filesystem::path& relative)
	{
		return std::filesystem::path("C:/repo") / relative;
	}
}

TEST_CASE("A landed discard tells the editor every file it rewrote, in the repository")
{
	Landing landing;

	landing.Model.RequestDiscard({.Restore = {"a.txt"}});
	landing.Frame();

	CHECK(landing.Editor.Expected == std::vector<std::filesystem::path>{InRepository("a.txt")});
}

TEST_CASE("A discarded folder is announced file by file, as it was listed when the discard was asked")
{
	Landing landing;

	landing.Model.RequestDiscard({.Delete = {"dir"}, .ListedFiles = {"dir/a.txt", "dir/b.txt"}});
	landing.Frame();

	CHECK(landing.Editor.Expected ==
		  std::vector<std::filesystem::path>{InRepository("dir/a.txt"), InRepository("dir/b.txt")});
}

TEST_CASE("A checkout tells the editor only of the documents that hold no unsaved edits")
{
	Landing landing;
	landing.Port.CheckoutResult.Changed = {"clean.txt", "edited.txt"};
	landing.Editor.Modified = {InRepository("edited.txt")};

	landing.Model.RequestCheckout({.Target = {.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature"}});
	landing.Frame();

	CHECK(landing.Editor.Expected == std::vector<std::filesystem::path>{InRepository("clean.txt")});
}

TEST_CASE("A landed commit clears the draft both hosts share, whichever host made it and whether one is drawn")
{
	Landing landing;
	landing.Draft.Message = "fix";
	landing.Draft.Amend = true;

	landing.Model.RequestCommit({.Message = "fix", .Amend = true});
	landing.Frame();

	CHECK(landing.Draft.Message.empty());
	CHECK_FALSE(landing.Draft.Amend);
}

TEST_CASE("A failed write is reported once")
{
	Landing landing;
	landing.Port.DeleteFails = true;

	landing.Model.RequestDiscard({.Delete = {"new.txt"}});
	landing.Frame();
	landing.Frame();

	REQUIRE(landing.Failures.size() == 1);
	CHECK(landing.Failures.front().Kind == Graphite::GitModel::WriteKind::Discard);
}

TEST_CASE("A write that changed files is reported once to whatever follows the files")
{
	Landing landing;

	landing.Model.RequestDiscard({.Restore = {"a.txt"}});
	landing.Frame();
	landing.Frame();

	CHECK(landing.FilesWritten == 1);
}

TEST_CASE("A frame with nothing landed does nothing")
{
	Landing landing;

	landing.Frame();

	CHECK(landing.Editor.Expected.empty());
	CHECK(landing.FilesWritten == 0);
	CHECK(landing.Failures.empty());
}
