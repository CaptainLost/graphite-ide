#include <doctest/doctest.h>

#include "Editor/DefinitionNavigation.h"

namespace {

	// The brief this test was drafted from predates Utf16Column: a bare 0 for the column no
	// longer compiles, since the constructor that would have accepted it is explicit precisely so
	// a byte column cannot be mistaken for one. Utf16Column(0) is what it means here.
	Graphite::Location At(const char* path, std::size_t line)
	{
		return Graphite::Location{path, line, Graphite::Utf16Column(0)};
	}
}

TEST_CASE("No locations is not a jump and offers no choice")
{
	std::vector<Graphite::Location> candidates;

	CHECK(Graphite::ResolveDefinition({}, {}, candidates) == Graphite::DefinitionOutcome::None);
	CHECK(candidates.empty());
}

TEST_CASE("One location jumps")
{
	std::vector<Graphite::Location> candidates;

	CHECK(Graphite::ResolveDefinition({At("D:/a.cpp", 4)}, {}, candidates) ==
		  Graphite::DefinitionOutcome::Jumped);
}

TEST_CASE("Several locations ask rather than guess")
{
	std::vector<Graphite::Location> candidates;

	CHECK(Graphite::ResolveDefinition({At("D:/a.h", 4), At("D:/a.cpp", 40)}, {}, candidates) ==
		  Graphite::DefinitionOutcome::NeedsChoice);
	CHECK(candidates.size() == 2);
}

TEST_CASE("A location identical to where the caret already is offers no jump")
{
	const Graphite::FilePosition here{"D:/a.cpp", 4, Graphite::Utf16Column(0)};
	std::vector<Graphite::Location> candidates;

	CHECK(Graphite::ResolveDefinition({At("D:/a.cpp", 4)}, here, candidates) ==
		  Graphite::DefinitionOutcome::None);
}

TEST_CASE("Duplicate locations collapse before they become a choice")
{
	std::vector<Graphite::Location> candidates;

	CHECK(Graphite::ResolveDefinition({At("D:/a.cpp", 4), At("D:/a.cpp", 4)}, {}, candidates) ==
		  Graphite::DefinitionOutcome::Jumped);
}
