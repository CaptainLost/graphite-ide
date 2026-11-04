#include <doctest/doctest.h>

#include "Git/Branches/BranchLookup.h"
#include "Git/Lines/LineReadFixtures.h"
#include "Review/ReviewMenu.h"

#include <optional>
#include <string>
#include <vector>

using Graphite::ReviewAction;
using Graphite::ReviewMenu;
using Graphite::ReviewMenuItem;
using Graphite::ReviewSessions;

namespace {

	const std::string kMain = "refs/heads/main";
	const std::string kSide = "refs/heads/side";

	// main is a base tracking origin/main; side is compared with main; lone is a base with no upstream; orphan is
	// compared with nothing.
	struct Repository
	{
		Repository()
		{
			Graphite::Fixtures::LineReadBuilder builder;
			builder.AddRemote("origin");
			const std::size_t main = builder.AddLocal("main");
			const std::size_t side = builder.AddLocal("side");
			const std::size_t lone = builder.AddLocal("lone");
			builder.AddLocal("orphan");
			const std::size_t originMain = builder.AddRemoteBranch("origin", "main");
			builder.MakeBase(main);
			builder.MakeBase(lone);
			builder.Track(main, originMain);
			builder.Compare(side, main);
			Read = builder.Build();
			Lines.emplace(Read);
		}

		Graphite::LineRead Read;
		std::optional<Graphite::BranchLines> Lines;

		const std::vector<Core::Branch>& Branches() const { return *Read.Branches; }
		const Core::Branch& Named(const std::string& name) const { return *Graphite::BranchWithFullName(Branches(), name); }

		ReviewMenu MenuOf(const std::string& name, const ReviewSessions& sessions, bool withLines = true) const
		{
			return Graphite::ReviewMenuFor(Named(name), Branches(), sessions, withLines ? &*Lines : nullptr);
		}
	};
}

TEST_CASE("Git window: a branch without a kept session offers a review against its line and a submenu of other bases")
{
	const Repository repository;
	const ReviewSessions sessions;

	const ReviewMenu menu = repository.MenuOf(kSide, sessions);

	REQUIRE(menu.Items.size() == 1);
	CHECK(menu.Items[0].Label == "Review against main");
	CHECK(menu.Items[0].Action == ReviewAction::Open);
	CHECK(menu.Items[0].Branch == kSide);
	CHECK(menu.Items[0].Base.empty());
	CHECK(menu.Submenu == "Review against");
	REQUIRE(menu.SubmenuItems.size() == 3);
	CHECK(menu.SubmenuItems[0].Label == "main");
	CHECK(menu.SubmenuItems[0].Action == ReviewAction::Start);
	CHECK(menu.SubmenuItems[0].Branch == kSide);
	CHECK(menu.SubmenuItems[0].Base == kMain);
}

TEST_CASE("Git window: a draft counts as no session in the review menu")
{
	const Repository repository;
	ReviewSessions sessions;
	sessions.OpenDraft(kSide, kMain);

	const ReviewMenu menu = repository.MenuOf(kSide, sessions);

	REQUIRE(menu.Items.size() == 1);
	CHECK(menu.Items[0].Label == "Review against main");
	CHECK(menu.Submenu == "Review against");
}

TEST_CASE("Git window: a base is offered a review against its upstream and nothing without one")
{
	const Repository repository;
	const ReviewSessions sessions;

	const ReviewMenu menu = repository.MenuOf(kMain, sessions);

	REQUIRE(menu.Items.size() == 1);
	CHECK(menu.Items[0].Label == "Review against origin/main");

	CHECK(repository.MenuOf("refs/heads/lone", sessions).Items.empty());
}

TEST_CASE("Git window: a branch compared with nothing is offered only a base to pick")
{
	const Repository repository;
	const ReviewSessions sessions;

	const ReviewMenu menu = repository.MenuOf("refs/heads/orphan", sessions);

	CHECK(menu.Items.empty());
	CHECK(menu.Submenu == "Review against");
	CHECK_FALSE(menu.SubmenuItems.empty());
}

TEST_CASE("Git window: a kept session offers Open review and End review")
{
	const Repository repository;
	ReviewSessions sessions;
	sessions.Start(kSide, kMain);

	const ReviewMenu menu = repository.MenuOf(kSide, sessions);

	CHECK(menu.Submenu.empty());
	CHECK(menu.SubmenuItems.empty());
	REQUIRE(menu.Items.size() == 2);
	CHECK(menu.Items[0].Label == "Open review");
	CHECK(menu.Items[0].Action == ReviewAction::Open);
	CHECK(menu.Items[1].Label == "End review\xE2\x80\xA6");
	CHECK(menu.Items[1].Action == ReviewAction::End);
	CHECK(menu.Items[1].Branch == kSide);
}

TEST_CASE("Git window: without a line read only review against is offered, listing every other local branch")
{
	const Repository repository;
	const ReviewSessions sessions;

	const ReviewMenu menu = repository.MenuOf(kSide, sessions, false);

	CHECK(menu.Items.empty());
	CHECK(menu.Submenu == "Review against");
	REQUIRE(menu.SubmenuItems.size() == 3);
	CHECK(menu.SubmenuItems[0].Label == "main");
	CHECK(menu.SubmenuItems[0].Base == kMain);
	CHECK(menu.SubmenuItems[1].Branch == kSide);
	CHECK(menu.SubmenuItems[1].Action == ReviewAction::Start);
}
