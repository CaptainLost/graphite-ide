#include <doctest/doctest.h>

#include "Search/HitGroups.h"

namespace {

	Graphite::SearchHit HitIn(std::string file, std::size_t line)
	{
		Graphite::SearchHit hit;
		hit.File = std::move(file);
		hit.Line = line;
		hit.Text = "text";

		return hit;
	}
}

TEST_CASE("HitGroups puts hits from one file in one group")
{
	Graphite::HitGroups groups;

	groups.Add(HitIn("a.cpp", 0));
	groups.Add(HitIn("a.cpp", 4));

	REQUIRE(groups.Get().size() == 1);
	CHECK(groups.Get()[0].Hits.size() == 2);
	CHECK(groups.GetCount() == 2);
}

TEST_CASE("HitGroups gathers a file's hits even when they arrive apart")
{
	Graphite::HitGroups groups;

	groups.Add(HitIn("a.cpp", 0));
	groups.Add(HitIn("b.cpp", 1));
	groups.Add(HitIn("a.cpp", 2));
	groups.Add(HitIn("c.cpp", 3));
	groups.Add(HitIn("b.cpp", 4));

	REQUIRE(groups.Get().size() == 3);

	CHECK(groups.Get()[0].Name == "a.cpp");
	CHECK(groups.Get()[0].Hits.size() == 2);

	CHECK(groups.Get()[1].Name == "b.cpp");
	CHECK(groups.Get()[1].Hits.size() == 2);

	CHECK(groups.Get()[2].Name == "c.cpp");
	CHECK(groups.Get()[2].Hits.size() == 1);

	CHECK(groups.GetCount() == 5);
}

TEST_CASE("HitGroups keeps groups in the order their first hit arrived")
{
	Graphite::HitGroups groups;

	groups.Add(HitIn("zebra.cpp", 0));
	groups.Add(HitIn("apple.cpp", 0));

	REQUIRE(groups.Get().size() == 2);
	CHECK(groups.Get()[0].Name == "zebra.cpp");
	CHECK(groups.Get()[1].Name == "apple.cpp");
}

TEST_CASE("HitGroups keeps a hit's own line and text")
{
	Graphite::HitGroups groups;

	Graphite::SearchHit hit = HitIn("a.cpp", 7);
	hit.Text = "int value = 1;";
	hit.From = 4;
	hit.To = 9;

	groups.Add(std::move(hit));

	REQUIRE(groups.Get().size() == 1);
	REQUIRE(groups.Get()[0].Hits.size() == 1);

	const Graphite::SearchHit& stored = groups.Get()[0].Hits[0];

	CHECK(stored.Line == 7);
	CHECK(stored.Text == "int value = 1;");
	CHECK(stored.From == 4);
	CHECK(stored.To == 9);
}

TEST_CASE("HitGroups keeps the path a jump needs")
{
	Graphite::HitGroups groups;

	groups.Add(HitIn("D:/Repos/IDE/Graphite/src/a.cpp", 0));

	REQUIRE(groups.Get().size() == 1);
	CHECK(groups.Get()[0].File == std::filesystem::path("D:/Repos/IDE/Graphite/src/a.cpp"));
}

TEST_CASE("HitGroups forgets everything when cleared")
{
	Graphite::HitGroups groups;

	groups.Add(HitIn("a.cpp", 0));
	groups.Clear();

	CHECK(groups.Get().empty());
	CHECK(groups.GetCount() == 0);

	groups.Add(HitIn("a.cpp", 1));

	REQUIRE(groups.Get().size() == 1);
	REQUIRE(groups.Get()[0].Hits.size() == 1);
	CHECK(groups.Get()[0].Hits[0].Line == 1);
}
