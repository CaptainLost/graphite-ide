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
