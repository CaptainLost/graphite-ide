#include <doctest/doctest.h>

#include "Core/Platform/ShellProfiles.h"

TEST_CASE("DiscoverShells finds at least one shell on this machine")
{
	const std::vector<Core::ShellProfile> shells = Core::DiscoverShells();

	REQUIRE_FALSE(shells.empty());
}

TEST_CASE("DiscoverShells returns paths that exist")
{
	for (const Core::ShellProfile& shell : Core::DiscoverShells())
	{
		CHECK(std::filesystem::exists(shell.Path));
	}
}

TEST_CASE("DiscoverShells names every shell it returns")
{
	for (const Core::ShellProfile& shell : Core::DiscoverShells())
	{
		CHECK_FALSE(shell.Name.empty());
	}
}

TEST_CASE("DiscoverShells returns each shell once")
{
	const std::vector<Core::ShellProfile> shells = Core::DiscoverShells();

	for (std::size_t outer = 0; outer < shells.size(); ++outer)
	{
		for (std::size_t inner = outer + 1; inner < shells.size(); ++inner)
		{
			CHECK(shells[outer].Path != shells[inner].Path);
		}
	}
}
