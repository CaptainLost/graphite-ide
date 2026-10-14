#include <doctest/doctest.h>

#include "AnalysisFixtures.h"
#include "Editor/Overlays/AnalysisCache.h"

namespace {

	Graphite::CachedAnalysis WithCount(std::string name, std::size_t count, int modifiedAt)
	{
		Graphite::CachedAnalysis analysis;
		analysis.Counts.push_back({std::move(name), Graphite::DeclarationKind::Function, count});
		analysis.ModifiedAt = FileTimeAt(modifiedAt);

		return analysis;
	}

	Graphite::CachedAnalysis OfDeclarations(std::size_t count, int modifiedAt)
	{
		Graphite::CachedAnalysis analysis;

		for (std::size_t index = 0; index < count; ++index)
		{
			Graphite::Declaration declaration;
			declaration.Name = std::string(64, 'x');
			declaration.Kind = Graphite::DeclarationKind::Function;
			declaration.Line = index;
			declaration.NameLine = index;

			analysis.Declarations.push_back(std::move(declaration));
		}

		analysis.ModifiedAt = FileTimeAt(modifiedAt);

		return analysis;
	}
}

TEST_CASE("A document's analysis comes back exactly as it was left")
{
	Graphite::AnalysisCache cache{1024 * 1024};
	cache.Put("a.cpp", WithCount("Draw", 7, 100));

	const std::optional<Graphite::CachedAnalysis> restored = cache.Take("a.cpp", FileTimeAt(100));

	REQUIRE(restored.has_value());
	REQUIRE(restored->Counts.size() == 1);
	CHECK(restored->Counts[0].Name == "Draw");
	CHECK(restored->Counts[0].Count == 7);
}

TEST_CASE("A document nothing was ever kept for restores nothing")
{
	Graphite::AnalysisCache cache{1024 * 1024};

	CHECK_FALSE(cache.Take("never-seen.cpp", FileTimeAt(100)).has_value());
}

TEST_CASE("Analysis of a file that has changed since is not restored")
{
	Graphite::AnalysisCache cache{1024 * 1024};
	cache.Put("a.cpp", WithCount("Draw", 7, 100));

	CHECK_FALSE(cache.Take("a.cpp", FileTimeAt(200)).has_value());
}

TEST_CASE("Analysis rejected as out of date is discarded rather than kept")
{
	Graphite::AnalysisCache cache{1024 * 1024};
	cache.Put("a.cpp", WithCount("Draw", 7, 100));

	cache.Take("a.cpp", FileTimeAt(200));

	CHECK_FALSE(cache.Take("a.cpp", FileTimeAt(100)).has_value());
}

TEST_CASE("Restoring a document consumes what was kept for it")
{
	Graphite::AnalysisCache cache{1024 * 1024};
	cache.Put("a.cpp", WithCount("Draw", 7, 100));

	REQUIRE(cache.Take("a.cpp", FileTimeAt(100)).has_value());

	CHECK_FALSE(cache.Take("a.cpp", FileTimeAt(100)).has_value());
}

TEST_CASE("Keeping a document again moves it out of the way of the next eviction")
{
	Graphite::AnalysisCache cache{Graphite::BytesOf(WithCount("A", 1, 100)) * 2};
	cache.Put("first.cpp", WithCount("A", 1, 100));
	cache.Put("second.cpp", WithCount("B", 2, 100));
	cache.Put("first.cpp", WithCount("A", 9, 100));
	cache.Put("third.cpp", WithCount("C", 3, 100));

	CHECK_FALSE(cache.Take("second.cpp", FileTimeAt(100)).has_value());

	const std::optional<Graphite::CachedAnalysis> restored = cache.Take("first.cpp", FileTimeAt(100));

	REQUIRE(restored.has_value());
	CHECK(restored->Counts[0].Count == 9);
}

TEST_CASE("Where a document's declarations were is kept alongside their counts")
{
	Graphite::AnalysisCache cache{1024 * 1024};

	Graphite::CachedAnalysis analysis = WithCount("Draw", 7, 100);
	analysis.Declarations.push_back(
		{"Draw", Graphite::DeclarationKind::Function, 12, Graphite::Utf16Column{4}, 12,
		 Graphite::Utf16Column{9}});

	cache.Put("a.cpp", std::move(analysis));

	const std::optional<Graphite::CachedAnalysis> restored = cache.Take("a.cpp", FileTimeAt(100));

	REQUIRE(restored.has_value());
	REQUIRE(restored->Declarations.size() == 1);
	CHECK(restored->Declarations[0].Name == "Draw");
	CHECK(restored->Declarations[0].Line == 12);
	CHECK(restored->Declarations[0].NameColumn.Value == 9);
}

namespace {

	Graphite::CachedAnalysis WithOneDeclaration()
	{
		Graphite::CachedAnalysis analysis;
		analysis.Declarations.push_back(
			{"Draw", Graphite::DeclarationKind::Function, 12, Graphite::Utf16Column{4}, 12,
			 Graphite::Utf16Column{9}});

		return analysis;
	}
}

TEST_CASE("Analysis of a document with unsaved edits is not worth keeping")
{
	const bool documentModified = true;

	CHECK_FALSE(Graphite::AnalysisWorthKeeping(WithOneDeclaration(), documentModified, FileTimeAt(100))
					.has_value());
}

TEST_CASE("Analysis is not worth keeping when the file's own time cannot be read")
{
	const bool documentModified = false;

	CHECK_FALSE(Graphite::AnalysisWorthKeeping(WithOneDeclaration(), documentModified, std::nullopt)
					.has_value());
}

TEST_CASE("Analysis that says nothing about the document is not worth keeping")
{
	const bool documentModified = false;

	CHECK_FALSE(Graphite::AnalysisWorthKeeping(Graphite::CachedAnalysis{}, documentModified, FileTimeAt(100))
					.has_value());
}

TEST_CASE("Kept analysis carries the file time it was checked against")
{
	const bool documentModified = false;

	const std::optional<Graphite::CachedAnalysis> kept =
		Graphite::AnalysisWorthKeeping(WithOneDeclaration(), documentModified, FileTimeAt(100));

	REQUIRE(kept.has_value());
	CHECK(kept->ModifiedAt == FileTimeAt(100));
	CHECK(kept->Declarations.size() == 1);
}

TEST_CASE("An entry larger than the whole budget is not stored")
{
	const Graphite::CachedAnalysis large = OfDeclarations(32, 100);

	Graphite::AnalysisCache cache{Graphite::BytesOf(large) / 2};
	cache.Put("big.cpp", large);

	CHECK(cache.GetBytes() == 0);
	CHECK_FALSE(cache.Take("big.cpp", FileTimeAt(100)).has_value());
}

TEST_CASE("A large entry evicts as many small ones as it needs to")
{
	const std::size_t small = Graphite::BytesOf(OfDeclarations(1, 100));
	const std::size_t large = Graphite::BytesOf(OfDeclarations(16, 100));

	Graphite::AnalysisCache cache{large + small};

	cache.Put("a.cpp", OfDeclarations(1, 100));
	cache.Put("b.cpp", OfDeclarations(1, 100));
	cache.Put("c.cpp", OfDeclarations(1, 100));
	cache.Put("big.cpp", OfDeclarations(16, 100));

	CHECK_FALSE(cache.Take("a.cpp", FileTimeAt(100)).has_value());
	CHECK_FALSE(cache.Take("b.cpp", FileTimeAt(100)).has_value());
	CHECK(cache.Take("c.cpp", FileTimeAt(100)).has_value());
	CHECK(cache.Take("big.cpp", FileTimeAt(100)).has_value());
}

TEST_CASE("Taking an entry gives its bytes back")
{
	Graphite::AnalysisCache cache{1024 * 1024};
	cache.Put("a.cpp", OfDeclarations(4, 100));

	REQUIRE(cache.GetBytes() > 0);

	cache.Take("a.cpp", FileTimeAt(100));

	CHECK(cache.GetBytes() == 0);
}

TEST_CASE("A budget lowered below what is held evicts down to it at once")
{
	Graphite::AnalysisCache cache{1024 * 1024};

	cache.Put("a.cpp", OfDeclarations(4, 100));
	cache.Put("b.cpp", OfDeclarations(4, 100));

	cache.SetBudget(Graphite::BytesOf(OfDeclarations(4, 100)));

	CHECK_FALSE(cache.Take("a.cpp", FileTimeAt(100)).has_value());
	CHECK(cache.Take("b.cpp", FileTimeAt(100)).has_value());
}
