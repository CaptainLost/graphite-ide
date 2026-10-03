#include <doctest/doctest.h>

#include "Editor/ActiveTextDocument.h"

#include "Core/Text/DocumentSet.h"

#include "TempDirectory.h"

TEST_CASE("With nothing open there is no active text document")
{
	const Core::DocumentSet documents;

	CHECK_FALSE(Graphite::ActiveTextDocument(documents).has_value());
}

TEST_CASE("The active tab's path is the active text document once its text is loaded")
{
	TempDirectory temp;
	const std::filesystem::path first = temp.MakeFile("first.cpp", "int x;");
	const std::filesystem::path second = temp.MakeFile("second.cpp", "int y;");

	Core::DocumentSet documents;
	REQUIRE(documents.Open(first, true) == Core::OpenOutcome::Opened);
	REQUIRE(documents.Open(second, true) == Core::OpenOutcome::Opened);

	CHECK(Graphite::ActiveTextDocument(documents) == second);

	documents.Activate(0);

	CHECK(Graphite::ActiveTextDocument(documents) == first);
}

TEST_CASE("An active tab whose file could not be read is not a text document")
{
	TempDirectory temp;

	// A tab that failed to load is still a tab - it stays open to say why - but it holds no
	// text, so nothing that asks a language server about the active document can use it.
	Core::DocumentSet documents;
	REQUIRE(documents.Open(temp.GetPath() / "missing.cpp", true) == Core::OpenOutcome::Failed);
	REQUIRE(documents.GetActive().has_value());

	CHECK_FALSE(Graphite::ActiveTextDocument(documents).has_value());
}
