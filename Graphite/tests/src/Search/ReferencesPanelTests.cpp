#include <doctest/doctest.h>

#include "Search/ReferencesPanel.h"

namespace {
	std::string LineOf(const std::filesystem::path& path, std::size_t line)
	{
		return path.filename().string() + " line " + std::to_string(line);
	}
}

TEST_CASE("References become hits grouped by file and ordered by line")
{
	const std::vector<Graphite::Location> locations{{"D:/b.cpp", 9, Graphite::Utf16Column(2)},
													{"D:/a.cpp", 40, Graphite::Utf16Column(4)},
													{"D:/a.cpp", 4, Graphite::Utf16Column(8)}};

	const std::vector<Graphite::SearchHit> hits = Graphite::AsHits(locations, "Draw", LineOf);

	REQUIRE(hits.size() == 3);
	CHECK(hits[0].File == std::filesystem::path("D:/a.cpp"));
	CHECK(hits[0].Line == 4);
	CHECK(hits[1].Line == 40);
	CHECK(hits[2].File == std::filesystem::path("D:/b.cpp"));
	CHECK(hits[0].Text == "a.cpp line 4");
}

TEST_CASE("References waiting on a server say what the server is doing, and wait with a loader")
{
	const Graphite::ReferencesSummary summary = Graphite::SummariseReferences(
		Graphite::ReferencesPhase::WaitingForServer, "Foo", 0, "waiting for Roslyn to load Graphite.csproj");

	CHECK(summary.Text == "References to Foo \xC2\xB7 waiting for Roslyn to load Graphite.csproj");
	CHECK(summary.Waiting);
}

TEST_CASE("References being found say so with a loader rather than a count")
{
	const Graphite::ReferencesSummary summary =
		Graphite::SummariseReferences(Graphite::ReferencesPhase::Finding, "Foo", 0, "");

	CHECK(summary.Text == "Finding references to Foo");
	CHECK(summary.Waiting);
}

TEST_CASE("Found references give their count, and say when the server had not finished indexing")
{
	CHECK(Graphite::SummariseReferences(Graphite::ReferencesPhase::Found, "Foo", 12, "").Text == "References to Foo: 12 hits");
	CHECK(Graphite::SummariseReferences(Graphite::ReferencesPhase::Found, "Foo", 1, "").Text == "References to Foo: 1 hit");
	CHECK(Graphite::SummariseReferences(Graphite::ReferencesPhase::Found, "Foo", 0, "").Text == "References to Foo: 0 hits");

	const Graphite::ReferencesSummary incomplete = Graphite::SummariseReferences(
		Graphite::ReferencesPhase::Found, "Foo", 12, "clangd is still indexing (40%), the list may be incomplete");

	CHECK(incomplete.Text == "References to Foo: 12 hits \xC2\xB7 clangd is still indexing (40%), the list may be incomplete");
	CHECK_FALSE(incomplete.Waiting);
}

TEST_CASE("References that failed or could not be asked give no count")
{
	CHECK(Graphite::SummariseReferences(Graphite::ReferencesPhase::Failed, "Foo", 0, "clangd did not answer").Text ==
		  "References to Foo");
	CHECK(Graphite::SummariseReferences(Graphite::ReferencesPhase::Unavailable, "Foo", 0, "clangd not installed").Text ==
		  "References to Foo");
	CHECK(Graphite::ReferencesFailureText("clangd returned an error") == "Couldn't get references: clangd returned an error");
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
	const auto unread = [](const std::filesystem::path&, std::size_t) -> std::optional<std::string> { return std::nullopt; };

	const std::vector<Graphite::Location> locations{{"D:/a.cpp", 4, Graphite::Utf16Column(8)}};
	const std::vector<Graphite::SearchHit> hits = Graphite::AsHits(locations, "Draw", unread);

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].Text.empty());
	CHECK(hits[0].From == 0);
	CHECK(hits[0].To == 0);
	REQUIRE(hits[0].Unread.has_value());
	CHECK(hits[0].Unread->Column == 8);
}

TEST_CASE("A line that is available is turned into a byte range there and then")
{
	const auto available = [](const std::filesystem::path&, std::size_t) { return std::optional<std::string>{"\tvoid Draw();"}; };

	const std::vector<Graphite::Location> locations{{"D:/a.cpp", 4, Graphite::Utf16Column(6)}};
	const std::vector<Graphite::SearchHit> hits = Graphite::AsHits(locations, "Draw", available);

	REQUIRE(hits.size() == 1);
	CHECK_FALSE(hits[0].Unread.has_value());
	CHECK(hits[0].From == 6);
	CHECK(hits[0].To == 10);
}

TEST_CASE("A highlight covers the name as written, not the length of its qualified name")
{
	const auto available = [](const std::filesystem::path&, std::size_t) { return std::optional<std::string>{"	Core::Application::Get().GetWindow();"}; };

	std::vector<Graphite::Location> locations{{"D:/a.cpp", 4, Graphite::Utf16Column(20)}};
	locations[0].EndLine = 4;
	locations[0].EndColumn = Graphite::Utf16Column(23);

	const std::vector<Graphite::SearchHit> hits = Graphite::AsHits(locations, "Application::Get", available);

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].From == 20);
	CHECK(hits[0].To == 23);
}

TEST_CASE("A span that misses the name is corrected to the name itself")
{
	const auto available = [](const std::filesystem::path&, std::size_t) { return std::optional<std::string>{"			: Core::Application(Core::ApplicationSpec{}),"}; };

	std::vector<Graphite::Location> locations{{"D:/Main.cpp", 54, Graphite::Utf16Column(5)}};
	locations[0].EndLine = 54;
	locations[0].EndColumn = Graphite::Utf16Column(9);

	const std::vector<Graphite::SearchHit> hits = Graphite::AsHits(locations, "Application::Application", available);

	REQUIRE(hits.size() == 1);

	CHECK(hits[0].From == 11);
	CHECK(hits[0].To == 22);
}

TEST_CASE("A span the server named is kept when it does cover the name")
{
	const auto available = [](const std::filesystem::path&, std::size_t) { return std::optional<std::string>{"	Core::Application::Get().GetWindow();"}; };

	std::vector<Graphite::Location> locations{{"D:/a.cpp", 4, Graphite::Utf16Column(20)}};
	locations[0].EndLine = 4;
	locations[0].EndColumn = Graphite::Utf16Column(23);

	const std::vector<Graphite::SearchHit> hits = Graphite::AsHits(locations, "Application::Get", available);

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].From == 20);
	CHECK(hits[0].To == 23);
}

TEST_CASE("A reply carrying no end falls back to the unqualified name, not the whole symbol")
{
	const auto available = [](const std::filesystem::path&, std::size_t) { return std::optional<std::string>{"	Core::Application::Get().GetWindow();"}; };

	const std::vector<Graphite::Location> locations{{"D:/a.cpp", 4, Graphite::Utf16Column(20)}};

	const std::vector<Graphite::SearchHit> hits = Graphite::AsHits(locations, "Application::Get", available);

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].From == 20);
	CHECK(hits[0].To == 23);
}
