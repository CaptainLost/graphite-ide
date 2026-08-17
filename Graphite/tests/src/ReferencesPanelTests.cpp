#include <doctest/doctest.h>

#include "Panels/ReferencesPanel.h"

namespace {

	std::string LineOf(const std::filesystem::path& path, std::size_t line)
	{
		return path.filename().string() + " line " + std::to_string(line);
	}
}

TEST_CASE("References become hits grouped by file and ordered by line")
{
	// Utf16Column's constructor is explicit (ADR 0023), so a bare column number does not
	// aggregate-initialize a Location the way the other two fields do - it has to be spelled out.
	const std::vector<Graphite::Location> locations{{"D:/b.cpp", 9, Graphite::Utf16Column(2)},
													 {"D:/a.cpp", 40, Graphite::Utf16Column(4)},
													 {"D:/a.cpp", 4, Graphite::Utf16Column(8)}};

	const auto hits = Graphite::AsHits(locations, "Draw", LineOf);

	REQUIRE(hits.size() == 3);
	CHECK(hits[0].File == std::filesystem::path("D:/a.cpp"));
	CHECK(hits[0].Line == 4);
	CHECK(hits[1].Line == 40);
	CHECK(hits[2].File == std::filesystem::path("D:/b.cpp"));
	CHECK(hits[0].Text == "a.cpp line 4");
}

TEST_CASE("An empty reply becomes no hits rather than one empty group")
{
	CHECK(Graphite::AsHits({}, "Draw", LineOf).empty());
}

TEST_CASE("A duplicated location appears once")
{
	const std::vector<Graphite::Location> locations{{"D:/a.cpp", 4, Graphite::Utf16Column(8)},
													 {"D:/a.cpp", 4, Graphite::Utf16Column(8)}};

	CHECK(Graphite::AsHits(locations, "Draw", LineOf).size() == 1);
}

TEST_CASE("A line that cannot be read yet leaves the hit carrying the column instead")
{
	// The references panel answers only for files already open, so a reply naming hundreds of
	// files costs no reads in the frame it arrives. Everything else comes back here unresolved,
	// carrying the column the row needs once someone does read the line.
	const auto unread = [](const std::filesystem::path&, std::size_t) -> std::optional<std::string>
	{ return std::nullopt; };

	const std::vector<Graphite::Location> locations{{"D:/a.cpp", 4, Graphite::Utf16Column(8)}};
	const auto hits = Graphite::AsHits(locations, "Draw", unread);

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].Text.empty());
	CHECK(hits[0].From == 0);
	CHECK(hits[0].To == 0);
	REQUIRE(hits[0].Unread.has_value());
	CHECK(hits[0].Unread->Column == 8);
}

TEST_CASE("A line that is available is turned into a byte range there and then")
{
	const auto available = [](const std::filesystem::path&, std::size_t)
	{ return std::optional<std::string>{"\tvoid Draw();"}; };

	// Column 6 in UTF-16 units is byte 6 on this ASCII line, where "Draw" starts.
	const std::vector<Graphite::Location> locations{{"D:/a.cpp", 4, Graphite::Utf16Column(6)}};
	const auto hits = Graphite::AsHits(locations, "Draw", available);

	REQUIRE(hits.size() == 1);
	CHECK_FALSE(hits[0].Unread.has_value());
	CHECK(hits[0].From == 6);
	CHECK(hits[0].To == 10);
}

// The bug this rule exists for: a server names a symbol in full, a use site spells only its last
// component, and measuring a highlight from the full name runs it off the end of the name. For
// "Application::Get" that is sixteen characters, which covers "Get().GetWindow(" exactly.
TEST_CASE("A highlight covers the name as written, not the length of its qualified name")
{
	const auto available = [](const std::filesystem::path&, std::size_t)
	{ return std::optional<std::string>{"	Core::Application::Get().GetWindow();"}; };

	// The server names the span it matched: "Get" at bytes 19 to 22.
	std::vector<Graphite::Location> locations{{"D:/a.cpp", 4, Graphite::Utf16Column(19)}};
	locations[0].EndLine = 4;
	locations[0].EndColumn = Graphite::Utf16Column(22);

	const auto hits = Graphite::AsHits(locations, "Application::Get", available);

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].From == 19);
	CHECK(hits[0].To == 22);
}

TEST_CASE("A reply carrying no end falls back to the unqualified name, not the whole symbol")
{
	const auto available = [](const std::filesystem::path&, std::size_t)
	{ return std::optional<std::string>{"	Core::Application::Get().GetWindow();"}; };

	// No end named, so the length has to be guessed - and the guess is "Get", three characters,
	// rather than "Application::Get", sixteen.
	const std::vector<Graphite::Location> locations{{"D:/a.cpp", 4, Graphite::Utf16Column(19)}};

	const auto hits = Graphite::AsHits(locations, "Application::Get", available);

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].From == 19);
	CHECK(hits[0].To == 22);
}
