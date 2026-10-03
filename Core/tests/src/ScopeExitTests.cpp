#include <doctest/doctest.h>

#include "Core/Base/ScopeExit.h"

#include <type_traits>

TEST_CASE("ScopeExit runs its action once, when its scope ends")
{
	int runs = 0;

	{
		const Core::ScopeExit atExit{[&runs] { ++runs; }};
		CHECK(runs == 0);
	}

	CHECK(runs == 1);
}

TEST_CASE("ScopeExit runs its action when its scope is left early")
{
	int runs = 0;

	const auto leaveEarly = [&runs](bool early) {
		const Core::ScopeExit atExit{[&runs] { ++runs; }};

		if (early)
		{
			return;
		}

		runs += 10;
	};

	leaveEarly(true);

	CHECK(runs == 1);
}

// A copy would run the action a second time: a promise answered twice throws.
TEST_CASE("ScopeExit can be neither copied nor moved")
{
	using Guard = Core::ScopeExit<void (*)()>;

	CHECK_FALSE(std::is_copy_constructible_v<Guard>);
	CHECK_FALSE(std::is_copy_assignable_v<Guard>);
	CHECK_FALSE(std::is_move_constructible_v<Guard>);
	CHECK_FALSE(std::is_move_assignable_v<Guard>);
}
