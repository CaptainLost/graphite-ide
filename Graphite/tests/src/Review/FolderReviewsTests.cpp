#include <doctest/doctest.h>

#include "Review/FolderReviews.h"

#include "DeferredJobs.h"
#include "GitFixtures.h"
#include "Review/FakeReviewPort.h"
#include "TempDirectory.h"

#include "Git/GitModel.h"
#include "Git/Lines/LinesModel.h"
#include "Git/Lines/LinesPort.h"
#include "Review/ReviewModel.h"
#include "UI/Dialogs/ModalGate.h"

#include <cstdint>
#include <string>
#include <vector>

using Graphite::FolderReviews;
using Graphite::ReviewAction;
using Graphite::ReviewMenuItem;
using Graphite::ReviewTabState;

namespace {

	const std::string kMain = "refs/heads/main";
	const std::string kSide = "refs/heads/side";

	const Core::Timestep kFrame{0.016f};

	// A lines port with no read to give: the reviews follow no line read.
	class NoLinesPort final : public Graphite::LinesPort
	{
	public:
		std::uint64_t RepositoryNow() override { return 1; }

		Core::Result<Graphite::LineRead> ReadLines(std::uint64_t, const Graphite::LineReadInput&) override { return Unread(); }

		Core::Result<Graphite::LineRead> RecomputeLines(const Graphite::LineRead&, const Graphite::LineAnswerChange&, std::int64_t) override
		{
			return Unread();
		}

	private:
		static Core::Error Unread() { return Core::Error{Core::ErrorCode::IoFailure, "not read in this test"}; }
	};

	// The reviews of a folder whose git model has listed nothing.
	struct Folder
	{
		EmptyGitPort GitPort;
		Graphite::GitModel Git{GitPort, RunJobsInline()};
		NoLinesPort LinesPort;
		Graphite::LinesModel Lines{LinesPort, RunJobsInline(), [] { return std::int64_t{0}; }};
		FakeReviewPort ReviewPort;
		DeferredJobs Jobs;
		Graphite::ReviewModel Model{ReviewPort, Jobs.Submitter(), NoWatches()};
		Graphite::ModalGate Modals;
		FolderReviews Reviews{Git, Lines, Model, Modals};
	};
}

TEST_CASE("Branch reviews: no branch has a review menu while the branches are not listed")
{
	Folder folder;
	Core::Branch side;
	side.FullName = kSide;

	const Graphite::ReviewMenu menu = folder.Reviews.MenuOf(side);

	CHECK(menu.Items.empty());
	CHECK(menu.SubmenuItems.empty());
}

TEST_CASE("Branch reviews: starting a review keeps its session and opens its tab; opening it again adds no tab")
{
	Folder folder;

	folder.Reviews.Apply(ReviewMenuItem{.Action = ReviewAction::Start, .Branch = kSide, .Base = kMain});
	folder.Reviews.Apply(ReviewMenuItem{.Action = ReviewAction::Open, .Branch = kSide});

	CHECK(folder.Reviews.Capture() == std::vector<ReviewTabState>{ReviewTabState{.Branch = kSide}});
}

TEST_CASE("Branch reviews: a branch with no session opens no tab while its base cannot be told")
{
	Folder folder;

	folder.Reviews.Open(kSide);
	folder.Reviews.Update(kFrame);

	CHECK(folder.Reviews.Capture().empty());
}

TEST_CASE("Branch reviews: the tabs and sessions go with the folder and come back with it")
{
	const TempDirectory root;
	Folder folder;
	folder.Reviews.Arrive(root.GetPath(), {});

	folder.Reviews.Apply(ReviewMenuItem{.Action = ReviewAction::Start, .Branch = kSide, .Base = kMain});
	const std::vector<ReviewTabState> captured = folder.Reviews.Capture();
	REQUIRE(captured.size() == 1);

	folder.Reviews.LetGo();
	CHECK(folder.Reviews.Capture().empty());

	folder.Reviews.Arrive(root.GetPath(), captured);
	CHECK(folder.Reviews.Capture() == captured);
}
