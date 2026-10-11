#include <doctest/doctest.h>

#include "Editor/Navigation/CandidateLine.h"
#include "TempDirectory.h"

#include "Core/Text/DocumentSet.h"

#include <ostream>
#include <string>

TEST_CASE("A candidate's line is read with its tabs expanded and coloured in its file's language")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("Widget.cs", "namespace Demo;\n\nclass Widget\n{\n\tpublic int Size;\n}\n");
	const Core::DocumentSet documents;

	const Graphite::CandidateLine line = Graphite::LoadCandidateLine(documents, Graphite::Location{.Path = file, .Line = 4}, 4);

	CHECK(line.Text == "    public int Size;");
	REQUIRE_FALSE(line.Tokens.empty());
	CHECK(line.Tokens.front().Kind == Graphite::TokenKind::Keyword);
}

TEST_CASE("A candidate in a file that cannot be read has no line")
{
	TempDirectory temp;
	const Core::DocumentSet documents;

	const Graphite::CandidateLine line = Graphite::LoadCandidateLine(documents, Graphite::Location{.Path = temp.GetPath() / "Gone.cs", .Line = 0}, 4);

	CHECK(line.Text.empty());
	CHECK(line.Tokens.empty());
}
