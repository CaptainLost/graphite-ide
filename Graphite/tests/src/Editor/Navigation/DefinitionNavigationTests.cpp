#include <doctest/doctest.h>

#include "Editor/Navigation/DefinitionNavigation.h"

#include "Core/Text/DocumentSet.h"

#include "TempDirectory.h"

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

TEST_CASE("A caret inside the name a location covers offers no jump")
{
	// clangd answers "Draw" at columns [8, 12); F12 pressed with the caret on the "a" is a
	// caret already inside the definition, not one somewhere else that should be moved.
	Graphite::Location draw{"D:/a.cpp", 4, Graphite::Utf16Column(8)};
	draw.EndLine = 4;
	draw.EndColumn = Graphite::Utf16Column(12);

	const Graphite::FilePosition inside{"D:/a.cpp", 4, Graphite::Utf16Column(10)};
	std::vector<Graphite::Location> candidates;

	CHECK(Graphite::ResolveDefinition({draw}, inside, candidates) ==
		  Graphite::DefinitionOutcome::None);
}

TEST_CASE("A caret past the end of the name a location covers still jumps")
{
	Graphite::Location draw{"D:/a.cpp", 4, Graphite::Utf16Column(8)};
	draw.EndLine = 4;
	draw.EndColumn = Graphite::Utf16Column(12);

	// The exclusive end, and everything after it on the line, is outside the name.
	const Graphite::FilePosition after{"D:/a.cpp", 4, Graphite::Utf16Column(12)};
	std::vector<Graphite::Location> candidates;

	CHECK(Graphite::ResolveDefinition({draw}, after, candidates) ==
		  Graphite::DefinitionOutcome::Jumped);
}

TEST_CASE("A location carrying no range is still compared against its start")
{
	// A reply with no range leaves the end at (0, 0), which must not read as a span covering
	// the whole file - only the start itself counts as "already here".
	const Graphite::FilePosition here{"D:/a.cpp", 4, Graphite::Utf16Column(0)};
	const Graphite::FilePosition elsewhere{"D:/a.cpp", 9, Graphite::Utf16Column(0)};
	std::vector<Graphite::Location> candidates;

	CHECK(Graphite::ResolveDefinition({At("D:/a.cpp", 4)}, here, candidates) ==
		  Graphite::DefinitionOutcome::None);
	CHECK(Graphite::ResolveDefinition({At("D:/a.cpp", 4)}, elsewhere, candidates) ==
		  Graphite::DefinitionOutcome::Jumped);
}

TEST_CASE("A caret inside a range in another file is not the place that range names")
{
	Graphite::Location draw{"D:/a.cpp", 4, Graphite::Utf16Column(8)};
	draw.EndLine = 4;
	draw.EndColumn = Graphite::Utf16Column(12);

	const Graphite::FilePosition other{"D:/b.cpp", 4, Graphite::Utf16Column(10)};
	std::vector<Graphite::Location> candidates;

	CHECK(Graphite::ResolveDefinition({draw}, other, candidates) ==
		  Graphite::DefinitionOutcome::Jumped);
}

TEST_CASE("Duplicate locations collapse before they become a choice")
{
	std::vector<Graphite::Location> candidates;

	CHECK(Graphite::ResolveDefinition({At("D:/a.cpp", 4), At("D:/a.cpp", 4)}, {}, candidates) ==
		  Graphite::DefinitionOutcome::Jumped);
}

TEST_CASE("Jumping into a file already open asks for that file's window")
{
	TempDirectory directory;
	const auto header = directory.MakeFile("header.h", "void Draw();\n");
	const auto source = directory.MakeFile("source.cpp", "void Draw() {}\n");

	Core::DocumentSet documents;
	documents.Open(header, true);
	documents.Open(source, true);

	// The state a second Ctrl+click starts from: both files open, the caller reading the one
	// holding the reference. Nothing about this jump adds a tab, so nothing about it reaches
	// the tab bar either unless the jump itself says so.
	documents.At(0).PendingFocus = false;
	documents.At(1).PendingFocus = false;

	Graphite::Jump(documents, Graphite::Location{header, 0, Graphite::Utf16Column(0)});

	const auto index = documents.Find(header);
	REQUIRE(index.has_value());
	CHECK(documents.At(*index).PendingFocus);
	CHECK(documents.At(*index).PendingScroll);
}
