#include <doctest/doctest.h>

#include "Language/Analysis/AnalysisRecord.h"

#include "AnalysisFixtures.h"

#include <algorithm>
#include <string>

using namespace Graphite;

namespace {

	constexpr std::uint32_t kOnePastLastDeclarationKind = 3;

	CachedAnalysis Sample()
	{
		CachedAnalysis analysis;

		analysis.Semantic.SetSpansOn(3, {SemanticSpan{0, 4, TokenKind::SemanticType}});
		analysis.Semantic.SetSpansOn(7, {SemanticSpan{2, 5, TokenKind::SemanticType},
										 SemanticSpan{9, 12, TokenKind::SemanticType}});

		Declaration type;
		type.Name = "Renderer";
		type.Kind = DeclarationKind::Type;
		type.Line = 10;
		type.NameLine = 10;
		type.EndLine = 30;

		Declaration declaration;
		declaration.Name = "Draw";
		declaration.Kind = DeclarationKind::Function;
		declaration.Line = 12;
		declaration.Column = Utf16Column(4);
		declaration.NameLine = 15;
		declaration.NameColumn = Utf16Column(9);
		declaration.EndLine = 20;
		declaration.Parent = 0;

		analysis.Declarations.push_back(std::move(type));
		analysis.Declarations.push_back(std::move(declaration));
		analysis.Counts.push_back({"Draw", DeclarationKind::Type, 7});
		analysis.ModifiedAt = FileTimeAt(100);

		return analysis;
	}

	std::string RecordInThePreviousLayout(std::uint32_t version)
	{
		std::string bytes;

		bytes.append(kAnalysisMagic);
		AppendUint32(bytes, version);
		AppendString(bytes, "a.cpp");
		AppendUint64(bytes, FileTimeToTicks(FileTimeAt(100)));

		constexpr std::uint32_t kNoSemanticLines = 0;
		AppendUint32(bytes, kNoSemanticLines);

		constexpr std::uint32_t kOneDeclaration = 1;
		AppendUint32(bytes, kOneDeclaration);
		AppendString(bytes, "Draw");
		AppendUint32(bytes, static_cast<std::uint32_t>(DeclarationKind::Function));
		AppendUint64(bytes, 12);
		AppendUint64(bytes, 4);
		AppendUint64(bytes, 15);
		AppendUint64(bytes, 9);

		constexpr std::uint32_t kNoCounts = 0;
		AppendUint32(bytes, kNoCounts);

		return bytes;
	}

	std::size_t FirstDifference(const std::string& left, const std::string& right)
	{
		const std::size_t shortest = std::min(left.size(), right.size());

		for (std::size_t byteIndex = 0; byteIndex < shortest; ++byteIndex)
		{
			if (left[byteIndex] != right[byteIndex])
			{
				return byteIndex;
			}
		}

		return shortest;
	}

	void OverwriteKind(std::string& bytes, std::size_t kindAt, std::uint32_t kind)
	{
		bytes[kindAt] = static_cast<char>(kind);
		bytes[kindAt + 1] = 0;
		bytes[kindAt + 2] = 0;
		bytes[kindAt + 3] = 0;
	}

	CachedAnalysis AnalysisWithDeclarationKind(DeclarationKind kind)
	{
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
	}

	CachedAnalysis AnalysisWithCountKind(DeclarationKind kind)
	{
		CachedAnalysis analysis;
		analysis.Counts.push_back({"Draw", kind, 7});

		return analysis;
	}
}

TEST_CASE("An analysis survives a round trip through bytes")
{
	const std::string bytes = EncodeAnalysis("a.cpp", Sample());

	const std::optional<CachedAnalysis> back = DecodeAnalysis(bytes, "a.cpp");

	REQUIRE(back.has_value());
	CHECK(back->ModifiedAt == FileTimeAt(100));

	REQUIRE(back->Declarations.size() == 2);
	CHECK(back->Declarations[0].Name == "Renderer");
	CHECK(back->Declarations[0].EndLine == 30);
	CHECK(back->Declarations[0].Parent == kNoParentDeclaration);

	CHECK(back->Declarations[1].Name == "Draw");
	CHECK(back->Declarations[1].Kind == DeclarationKind::Function);
	CHECK(back->Declarations[1].Line == 12);
	CHECK(back->Declarations[1].Column == Utf16Column(4));
	CHECK(back->Declarations[1].NameLine == 15);
	CHECK(back->Declarations[1].NameColumn == Utf16Column(9));
	CHECK(back->Declarations[1].EndLine == 20);
	CHECK(back->Declarations[1].Parent == 0);

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
	empty.ModifiedAt = FileTimeAt(50);

	const std::optional<CachedAnalysis> back = DecodeAnalysis(EncodeAnalysis("a.cpp", empty), "a.cpp");

	REQUIRE(back.has_value());
	CHECK(back->Semantic.IsEmpty());
	CHECK(back->Declarations.empty());
	CHECK(back->ModifiedAt == FileTimeAt(50));
}

TEST_CASE("A record written for one file is refused for another")
{
	const std::string bytes = EncodeAnalysis("a.cpp", Sample());

	CHECK_FALSE(DecodeAnalysis(bytes, "b.cpp").has_value());
}

TEST_CASE("A truncated record is refused at every length rather than trusted")
{
	const std::string bytes = EncodeAnalysis("a.cpp", Sample());

	REQUIRE(bytes.size() > 8);

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
	constexpr std::size_t kVersionAt = 4;

	bytes[kVersionAt] = static_cast<char>(kAnalysisRecordVersion + 1);

	CHECK_FALSE(DecodeAnalysis(bytes, "a.cpp").has_value());
}

TEST_CASE("A record of the previous version is discarded")
{
	constexpr std::uint32_t kVersionWithoutRangesOrNesting = 1;

	CHECK_FALSE(DecodeAnalysis(RecordInThePreviousLayout(kVersionWithoutRangesOrNesting), "a.cpp").has_value());
}

TEST_CASE("A declaration whose parent is not an earlier declaration is refused")
{
	CachedAnalysis ownParent = Sample();
	ownParent.Declarations[1].Parent = 1;

	CachedAnalysis laterParent = Sample();
	laterParent.Declarations[0].Parent = 1;

	CachedAnalysis negativeParent = Sample();
	negativeParent.Declarations[1].Parent = kNoParentDeclaration - 1;

	CHECK_FALSE(DecodeAnalysis(EncodeAnalysis("a.cpp", ownParent), "a.cpp").has_value());
	CHECK_FALSE(DecodeAnalysis(EncodeAnalysis("a.cpp", laterParent), "a.cpp").has_value());
	CHECK_FALSE(DecodeAnalysis(EncodeAnalysis("a.cpp", negativeParent), "a.cpp").has_value());
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
	OverwriteKind(corrupted, kindAt, static_cast<std::uint32_t>(TokenKind::Count));

	CHECK_FALSE(DecodeAnalysis(corrupted, "a.cpp").has_value());
}

TEST_CASE("A declaration whose kind is past DeclarationKind's known values is refused")
{
	const std::string functionBytes =
		EncodeAnalysis("a.cpp", AnalysisWithDeclarationKind(DeclarationKind::Function));
	const std::string typeBytes =
		EncodeAnalysis("a.cpp", AnalysisWithDeclarationKind(DeclarationKind::Type));

	REQUIRE(functionBytes.size() == typeBytes.size());

	const std::size_t kindAt = FirstDifference(functionBytes, typeBytes);
	REQUIRE(kindAt < functionBytes.size());

	std::string corrupted = functionBytes;
	OverwriteKind(corrupted, kindAt, kOnePastLastDeclarationKind);

	CHECK_FALSE(DecodeAnalysis(corrupted, "a.cpp").has_value());
}

TEST_CASE("A known count whose kind is past DeclarationKind's known values is refused")
{
	const std::string functionBytes =
		EncodeAnalysis("a.cpp", AnalysisWithCountKind(DeclarationKind::Function));
	const std::string typeBytes =
		EncodeAnalysis("a.cpp", AnalysisWithCountKind(DeclarationKind::Type));

	REQUIRE(functionBytes.size() == typeBytes.size());

	const std::size_t kindAt = FirstDifference(functionBytes, typeBytes);
	REQUIRE(kindAt < functionBytes.size());

	std::string corrupted = functionBytes;
	OverwriteKind(corrupted, kindAt, kOnePastLastDeclarationKind);

	CHECK_FALSE(DecodeAnalysis(corrupted, "a.cpp").has_value());
}

TEST_CASE("A record with extra bytes appended past a well-formed one is refused")
{
	CHECK_FALSE(DecodeAnalysis(EncodeAnalysis("a.cpp", Sample()) + "x", "a.cpp").has_value());
}
