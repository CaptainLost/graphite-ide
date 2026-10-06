#include <doctest/doctest.h>

#include "Core/Base/ScopeExit.h"

#include <type_traits>

namespace {
	enum class ScopeLeft
	{
		ByEarlyReturn,
		AtItsEnd
	};

	constexpr int kAddedOnlyAtTheEndOfTheScope = 10;

	void LeaveScopeHoldingExitCounter(int& runs, ScopeLeft scopeLeft)
	{
		const Core::ScopeExit atExit{[&runs] { ++runs; }};

		if (scopeLeft == ScopeLeft::ByEarlyReturn)
		{
			return;
		}

		runs += kAddedOnlyAtTheEndOfTheScope;
	}
}

TEST_CASE("ScopeExit runs its action once, when its scope ends")
{
	int runs = 0;

	{
		const Core::ScopeExit atExit{[&runs] { ++runs; }};
		CHECK(runs == 0);
	}

	CHECK(runs == 1);
}

TEST_CASE("ScopeExit runs its action when its scope is left early by a return")
{
	int runs = 0;

	LeaveScopeHoldingExitCounter(runs, ScopeLeft::ByEarlyReturn);

	CHECK(runs == 1);
}

TEST_CASE("ScopeExit can be neither copied nor moved because a copy would run its action a second time")
{
	using Guard = Core::ScopeExit<void (*)()>;

	CHECK_FALSE(std::is_copy_constructible_v<Guard>);
	CHECK_FALSE(std::is_copy_assignable_v<Guard>);
	CHECK_FALSE(std::is_move_constructible_v<Guard>);
	CHECK_FALSE(std::is_move_assignable_v<Guard>);
}
