#include <doctest/doctest.h>

#include "Git/Window/GitWindowRestore.h"

#include <string>
#include <vector>

namespace {

	using namespace Graphite;

	Core::Branch Local(const std::string& name)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = "refs/heads/" + name;
		return branch;
	}

	Core::Branch Remote(const std::string& name)
	{
		Core::Branch branch;
		branch.Kind = Core::RefKind::Remote;
		branch.Name = name;
		branch.FullName = "refs/remotes/" + name;
		return branch;
	}

	const std::vector<Core::Branch> kBranches = {Local("main"), Local("topic"), Remote("origin/side")};
}

TEST_CASE("A restored thread of a branch that is gone opens Work")
{
	const GitWindowState state{.View = GitWindowView::Thread, .Branch = "refs/heads/gone"};

	CHECK(SelectionToRestore(state, kBranches) == GitWindowSelection{GitWindowView::Work, ""});
	CHECK(SelectionToRestore(GitWindowState{.View = GitWindowView::Thread}, kBranches) ==
		  GitWindowSelection{GitWindowView::Work, ""});
}

TEST_CASE("A restored history of a ref that is gone shows HEAD's history")
{
	const GitWindowState gone{.View = GitWindowView::History, .Branch = "refs/remotes/origin/gone"};
	const GitWindowState head{.View = GitWindowView::History};
	const GitWindowState remote{.View = GitWindowView::History, .Branch = "refs/remotes/origin/side"};

	CHECK(SelectionToRestore(gone, kBranches) == GitWindowSelection{GitWindowView::History, ""});
	CHECK(SelectionToRestore(head, kBranches) == GitWindowSelection{GitWindowView::History, ""});
	CHECK(SelectionToRestore(remote, kBranches) == GitWindowSelection{GitWindowView::History, "refs/remotes/origin/side"});
}

TEST_CASE("A restored thread of a branch still there opens it")
{
	const GitWindowState state{.View = GitWindowView::Thread, .Branch = "refs/heads/topic"};

	CHECK(SelectionToRestore(state, kBranches) == GitWindowSelection{GitWindowView::Thread, "refs/heads/topic"});

	// The views that name no branch come back as they were, and drop any branch kept with them.
	CHECK(SelectionToRestore(GitWindowState{.View = GitWindowView::Changes, .Branch = "refs/heads/topic"}, kBranches) ==
		  GitWindowSelection{GitWindowView::Changes, ""});
	CHECK(SelectionToRestore(GitWindowState{.View = GitWindowView::Branches}, kBranches) ==
		  GitWindowSelection{GitWindowView::Branches, ""});
	CHECK(SelectionToRestore(GitWindowState{.View = GitWindowView::Work}, {}) == GitWindowSelection{GitWindowView::Work, ""});
}
