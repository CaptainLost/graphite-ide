#include <doctest/doctest.h>

#include "Git/Lines/HeadInRead.h"

#include "Core/Git/Head.h"

#include <string>
#include <utility>

using Graphite::HeadInRead;
using Graphite::HeadInReadInput;
using Graphite::PlaceHeadInRead;

namespace {

	Core::HeadState OnBranch(std::string name)
	{
		Core::HeadState head;
		head.Kind = Core::HeadKind::Branch;
		head.BranchName = std::move(name);
		return head;
	}

	Core::HeadState Detached()
	{
		Core::HeadState head;
		head.Kind = Core::HeadKind::Detached;
		return head;
	}

	Core::HeadState Unborn(std::string name)
	{
		Core::HeadState head;
		head.Kind = Core::HeadKind::Unborn;
		head.BranchName = std::move(name);
		return head;
	}

	constexpr bool kReadComing = true;
	constexpr bool kNoReadComing = false;
}

TEST_CASE("Git window: HEAD's history takes the read's line while the read names HEAD's branch checked out")
{
	const Core::HeadState head = OnBranch("main");

	CHECK(PlaceHeadInRead(HeadInReadInput{.Head = &head, .ReadCheckedOut = "refs/heads/main", .ReadComing = kNoReadComing}) ==
		  HeadInRead::Agrees);
	CHECK(PlaceHeadInRead(HeadInReadInput{.Head = &head, .ReadCheckedOut = "refs/heads/main", .ReadComing = kReadComing}) ==
		  HeadInRead::Agrees);
}

TEST_CASE("Git window: HEAD's history waits for the read coming after a checkout")
{
	const Core::HeadState head = OnBranch("feature");

	CHECK(PlaceHeadInRead(HeadInReadInput{.Head = &head, .ReadCheckedOut = "refs/heads/main", .ReadComing = kReadComing}) ==
		  HeadInRead::WaitsForRead);
}

TEST_CASE("Git window: HEAD's history is shown alone when the read names another branch and none is coming")
{
	const Core::HeadState head = OnBranch("feature");

	CHECK(PlaceHeadInRead(HeadInReadInput{.Head = &head, .ReadCheckedOut = "refs/heads/main", .ReadComing = kNoReadComing}) ==
		  HeadInRead::Differs);
}

TEST_CASE("Git window: a detached HEAD agrees only with a read that names no branch checked out")
{
	const Core::HeadState head = Detached();

	CHECK(PlaceHeadInRead(HeadInReadInput{.Head = &head, .ReadCheckedOut = "", .ReadComing = kNoReadComing}) == HeadInRead::Agrees);
	CHECK(PlaceHeadInRead(HeadInReadInput{.Head = &head, .ReadCheckedOut = "refs/heads/main", .ReadComing = kReadComing}) ==
		  HeadInRead::WaitsForRead);
}

TEST_CASE("Git window: an unborn HEAD agrees with a read that lists no branch checked out")
{
	const Core::HeadState head = Unborn("main");

	CHECK(PlaceHeadInRead(HeadInReadInput{.Head = &head, .ReadCheckedOut = "", .ReadComing = kNoReadComing}) == HeadInRead::Agrees);
}

TEST_CASE("Git window: with HEAD unread the read's checked-out branch stands")
{
	CHECK(PlaceHeadInRead(HeadInReadInput{.Head = nullptr, .ReadCheckedOut = "refs/heads/main", .ReadComing = kReadComing}) ==
		  HeadInRead::Agrees);
}
