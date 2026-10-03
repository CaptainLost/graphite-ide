#include <doctest/doctest.h>

#include "Lang/Analysis/AnalysisRecord.h"

#include <algorithm>
#include <chrono>
#include <string>

using namespace Graphite;

namespace {

	std::filesystem::file_time_type At(int seconds)
	{
		return std::filesystem::file_time_type{} + std::chrono::seconds(seconds);
	}

	CachedAnalysis Sample()
	{
		CachedAnalysis analysis;

		analysis.Semantic.SetSpansOn(3, {SemanticSpan{0, 4, TokenKind::SemanticType}});
		analysis.Semantic.SetSpansOn(7, {SemanticSpan{2, 5, TokenKind::SemanticType},
										 SemanticSpan{9, 12, TokenKind::SemanticType}});

		// Every field below is a distinct value from every other field of the same type, so a
		// decoder that swapped Line and NameLine, or dropped one of them, could not pass by
		// coincidence.
		Declaration declaration;
		declaration.Name = "Draw";
		declaration.Kind = DeclarationKind::Function;
		declaration.Line = 12;
		declaration.Column = Utf16Column(4);
		declaration.NameLine = 15;
		declaration.NameColumn = Utf16Column(9);

		analysis.Declarations.push_back(std::move(declaration));
		analysis.Counts.push_back({"Draw", DeclarationKind::Type, 7});
		analysis.ModifiedAt = At(100);

		return analysis;
	}
}

TEST_CASE("An analysis survives a round trip through bytes")
{
	const std::string bytes = EncodeAnalysis("a.cpp", Sample());

	const std::optional<CachedAnalysis> back = DecodeAnalysis(bytes, "a.cpp");

	REQUIRE(back.has_value());
	CHECK(back->ModifiedAt == At(100));

	REQUIRE(back->Declarations.size() == 1);
	CHECK(back->Declarations[0].Name == "Draw");
	CHECK(back->Declarations[0].Kind == DeclarationKind::Function);
	CHECK(back->Declarations[0].Line == 12);
	CHECK(back->Declarations[0].Column == Utf16Column(4));
	CHECK(back->Declarations[0].NameLine == 15);
	CHECK(back->Declarations[0].NameColumn == Utf16Column(9));

	REQUIRE(back->Counts.size() == 1);
	CHECK(back->Counts[0].Name == "Draw");
	CHECK(back->Counts[0].Kind == DeclarationKind::Type);
	CHECK(back->Counts[0].Count == 7);

	REQUIRE(back->Semantic.SpansOn(7).size() == 2);
	CHECK(back->Semantic.SpansOn(7)[1].From == 9);
	CHECK(back->Semantic.SpansOn(7)[1].To == 12);
	CHECK(back->Semantic.SpansOn(3).size() == 1);
	CHECK(back->Semantic.SpansOn(4).empty());
}

TEST_CASE("An empty analysis round trips as an empty analysis")
{
	CachedAnalysis empty;
	empty.ModifiedAt = At(50);

	const std::optional<CachedAnalysis> back = DecodeAnalysis(EncodeAnalysis("a.cpp", empty), "a.cpp");

	REQUIRE(back.has_value());
	CHECK(back->Semantic.IsEmpty());
	CHECK(back->Declarations.empty());
	CHECK(back->ModifiedAt == At(50));
}

TEST_CASE("A record written for one file is refused for another")
{
	const std::string bytes = EncodeAnalysis("a.cpp", Sample());

	// Two paths can hash to one record name; the path inside is what settles it.
	CHECK_FALSE(DecodeAnalysis(bytes, "b.cpp").has_value());
}

TEST_CASE("A truncated record is refused at every length rather than trusted")
{
	const std::string bytes = EncodeAnalysis("a.cpp", Sample());

	REQUIRE(bytes.size() > 8);

	// Every prefix short of the whole thing. A decoder that reads past its buffer fails here
	// rather than in a user's session, and one that trusts a length field fails here too.
	for (std::size_t length = 0; length < bytes.size(); ++length)
	{
		CHECK_FALSE(DecodeAnalysis(std::string_view{bytes}.substr(0, length), "a.cpp").has_value());
	}
}

TEST_CASE("A record with the wrong magic number is refused")
{
	std::string bytes = EncodeAnalysis("a.cpp", Sample());
	bytes[0] = 'X';

	CHECK_FALSE(DecodeAnalysis(bytes, "a.cpp").has_value());
}

TEST_CASE("A record from another version is refused")
{
	std::string bytes = EncodeAnalysis("a.cpp", Sample());

	// The version sits immediately after the four magic bytes, little endian.
	bytes[4] = static_cast<char>(kAnalysisRecordVersion + 1);

	CHECK_FALSE(DecodeAnalysis(bytes, "a.cpp").has_value());
}

namespace {

	// Encodes two records that differ only in one field and returns where in the bytes they
	// first diverge. Locating a field this way, rather than by hand-computing its offset from
	// the format's layout, is what keeps these two tests honest about which field they corrupt
	// without re-encoding the encoder's own layout decisions into the test.
	std::size_t FirstDifference(const std::string& a, const std::string& b)
	{
		const std::size_t shortest = std::min(a.size(), b.size());

		for (std::size_t i = 0; i < shortest; ++i)
		{
			if (a[i] != b[i])
			{
				return i;
			}
		}

		return shortest;
	}
}

TEST_CASE("A semantic span whose kind is at or past TokenKind::Count is refused")
{
	CachedAnalysis withType;
	withType.Semantic.SetSpansOn(3, {SemanticSpan{0, 4, TokenKind::SemanticType}});

	CachedAnalysis withFunction;
	withFunction.Semantic.SetSpansOn(3, {SemanticSpan{0, 4, TokenKind::SemanticFunction}});

	const std::string typeBytes = EncodeAnalysis("a.cpp", withType);
	const std::string functionBytes = EncodeAnalysis("a.cpp", withFunction);

	REQUIRE(typeBytes.size() == functionBytes.size());

	const std::size_t kindAt = FirstDifference(typeBytes, functionBytes);
	REQUIRE(kindAt < typeBytes.size());

	std::string corrupted = typeBytes;
	corrupted[kindAt] = static_cast<char>(TokenKind::Count);
	corrupted[kindAt + 1] = 0;
	corrupted[kindAt + 2] = 0;
	corrupted[kindAt + 3] = 0;

	// A kind is an index into SyntaxPalette::Colours, sized by TokenKind::Count: a record
	// claiming one at or past it would read past the end of that array while drawing.
	CHECK_FALSE(DecodeAnalysis(corrupted, "a.cpp").has_value());
}

TEST_CASE("A declaration whose kind is past DeclarationKind's known values is refused")
{
	auto withKind = [](DeclarationKind kind) {
		CachedAnalysis analysis;

		Declaration declaration;
		declaration.Name = "Draw";
		declaration.Kind = kind;
		declaration.Line = 12;
		declaration.Column = Utf16Column(4);
		declaration.NameLine = 12;
		declaration.NameColumn = Utf16Column(9);

		analysis.Declarations.push_back(std::move(declaration));

		return analysis;
	};

	const std::string functionBytes = EncodeAnalysis("a.cpp", withKind(DeclarationKind::Function));
	const std::string typeBytes = EncodeAnalysis("a.cpp", withKind(DeclarationKind::Type));

	REQUIRE(functionBytes.size() == typeBytes.size());

	const std::size_t kindAt = FirstDifference(functionBytes, typeBytes);
	REQUIRE(kindAt < functionBytes.size());

	std::string corrupted = functionBytes;
	corrupted[kindAt] = 3; // one past Other, the last valid DeclarationKind
	corrupted[kindAt + 1] = 0;
	corrupted[kindAt + 2] = 0;
	corrupted[kindAt + 3] = 0;

	CHECK_FALSE(DecodeAnalysis(corrupted, "a.cpp").has_value());
}

TEST_CASE("A known count whose kind is past DeclarationKind's known values is refused")
{
	// The counts loop validates KnownCount::Kind with its own if-statement, separate from the
	// one guarding Declaration::Kind - nothing shares the check between them, so each needs its
	// own case pinning it or a later simplification could delete either one unnoticed.
	auto withKind = [](DeclarationKind kind) {
		CachedAnalysis analysis;
		analysis.Counts.push_back({"Draw", kind, 7});

		return analysis;
	};

	const std::string functionBytes = EncodeAnalysis("a.cpp", withKind(DeclarationKind::Function));
	const std::string typeBytes = EncodeAnalysis("a.cpp", withKind(DeclarationKind::Type));

	REQUIRE(functionBytes.size() == typeBytes.size());

	const std::size_t kindAt = FirstDifference(functionBytes, typeBytes);
	REQUIRE(kindAt < functionBytes.size());

	std::string corrupted = functionBytes;
	corrupted[kindAt] = 3; // one past Other, the last valid DeclarationKind
	corrupted[kindAt + 1] = 0;
	corrupted[kindAt + 2] = 0;
	corrupted[kindAt + 3] = 0;

	CHECK_FALSE(DecodeAnalysis(corrupted, "a.cpp").has_value());
}

TEST_CASE("A record with extra bytes appended past a well-formed one is refused")
{
	// The only case that actually exercises AtEnd() as the check that distinguishes a
	// well-formed record from a longer one - truncation trips the cursor's own bounds checks
	// first, so it never reaches AtEnd() at all.
	CHECK_FALSE(DecodeAnalysis(EncodeAnalysis("a.cpp", Sample()) + "x", "a.cpp").has_value());
}
