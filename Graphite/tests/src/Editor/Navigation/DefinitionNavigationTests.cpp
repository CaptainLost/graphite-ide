#include <doctest/doctest.h>

#include "Editor/Navigation/DefinitionNavigation.h"

#include "Core/Text/DocumentSet.h"

#include "TempDirectory.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace {
	Graphite::Location LocationAt(const char* path, std::size_t line)
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

	CHECK(Graphite::ResolveDefinition({LocationAt("D:/a.cpp", 4)}, {}, candidates) ==
		  Graphite::DefinitionOutcome::Jumped);
}

TEST_CASE("Several locations ask rather than guess")
{
	std::vector<Graphite::Location> candidates;

	CHECK(Graphite::ResolveDefinition({LocationAt("D:/a.h", 4), LocationAt("D:/a.cpp", 40)}, {}, candidates) ==
		  Graphite::DefinitionOutcome::NeedsChoice);
	CHECK(candidates.size() == 2);
}

TEST_CASE("A location identical to where the caret already is offers no jump")
{
	const Graphite::FilePosition here{"D:/a.cpp", 4, Graphite::Utf16Column(0)};
	std::vector<Graphite::Location> candidates;

	CHECK(Graphite::ResolveDefinition({LocationAt("D:/a.cpp", 4)}, here, candidates) ==
		  Graphite::DefinitionOutcome::None);
}

TEST_CASE("A caret inside the name a location covers offers no jump")
{
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

	const Graphite::FilePosition after{"D:/a.cpp", 4, Graphite::Utf16Column(12)};
	std::vector<Graphite::Location> candidates;

	CHECK(Graphite::ResolveDefinition({draw}, after, candidates) ==
		  Graphite::DefinitionOutcome::Jumped);
}

TEST_CASE("A location carrying no range is still compared against its start")
{
	const Graphite::FilePosition here{"D:/a.cpp", 4, Graphite::Utf16Column(0)};
	const Graphite::FilePosition elsewhere{"D:/a.cpp", 9, Graphite::Utf16Column(0)};
	std::vector<Graphite::Location> candidates;

	CHECK(Graphite::ResolveDefinition({LocationAt("D:/a.cpp", 4)}, here, candidates) ==
		  Graphite::DefinitionOutcome::None);
	CHECK(Graphite::ResolveDefinition({LocationAt("D:/a.cpp", 4)}, elsewhere, candidates) ==
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

	CHECK(Graphite::ResolveDefinition({LocationAt("D:/a.cpp", 4), LocationAt("D:/a.cpp", 4)}, {}, candidates) ==
		  Graphite::DefinitionOutcome::Jumped);
}

TEST_CASE("Jumping into a file already open asks for that file's window")
{
	TempDirectory directory;
	const std::filesystem::path header = directory.MakeFile("header.h", "void Draw();\n");
	const std::filesystem::path source = directory.MakeFile("source.cpp", "void Draw() {}\n");

	Core::DocumentSet documents;
	documents.Open(header, Core::TabKind::Permanent);
	documents.Open(source, Core::TabKind::Permanent);

	documents.At(0).PendingFocus = false;
	documents.At(1).PendingFocus = false;

	Graphite::Jump(documents, Graphite::Location{header, 0, Graphite::Utf16Column(0)},
				   [&documents](const std::filesystem::path& path) { documents.Open(path, Core::TabKind::Permanent); });

	const std::optional<std::size_t> index = documents.Find(header);
	REQUIRE(index.has_value());
	CHECK(documents.At(*index).PendingFocus);
	CHECK(documents.At(*index).PendingScroll);
}

TEST_CASE("Jumping opens the file through the opener it is given, so a large file is asked about first")
{
	TempDirectory directory;
	const std::filesystem::path header = directory.MakeFile("header.h", "void Draw();\n");

	Core::DocumentSet documents;
	std::vector<std::filesystem::path> opened;

	Graphite::Jump(documents, Graphite::Location{header, 0, Graphite::Utf16Column(5)},
				   [&documents, &opened](const std::filesystem::path& path) {
					   opened.push_back(path);
					   documents.Open(path, Core::TabKind::Permanent);
				   });

	REQUIRE(opened.size() == 1);
	CHECK(opened.front() == header);

	const std::optional<std::size_t> index = documents.Find(header);
	REQUIRE(index.has_value());
	CHECK(documents.At(*index).Caret.Offset == 5);
	CHECK(documents.At(*index).Caret.Anchor == 5);
}

TEST_CASE("Jumping to a file the opener did not open moves nothing")
{
	TempDirectory directory;
	const std::filesystem::path header = directory.MakeFile("header.h", "void Draw();\n");

	Core::DocumentSet documents;

	Graphite::Jump(documents, Graphite::Location{header, 0, Graphite::Utf16Column(5)},
				   [](const std::filesystem::path&) {});

	CHECK_FALSE(documents.Find(header).has_value());
}
