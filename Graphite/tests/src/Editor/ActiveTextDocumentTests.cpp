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
	REQUIRE(documents.Open(first, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);
	REQUIRE(documents.Open(second, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);

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
	REQUIRE(documents.Open(temp.GetPath() / "missing.cpp", Core::TabKind::Permanent) == Core::OpenOutcome::Failed);
	REQUIRE(documents.GetActiveIndex().has_value());

	CHECK_FALSE(Graphite::ActiveTextDocument(documents).has_value());
}

TEST_CASE("The active text document is the shown one only while its tab is on screen")
{
	TempDirectory temp;
	const std::filesystem::path first = temp.MakeFile("first.cpp", "int x;");
	const std::filesystem::path second = temp.MakeFile("second.cpp", "int y;");

	Core::DocumentSet documents;
	REQUIRE(documents.Open(first, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);
	REQUIRE(documents.Open(second, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);

	// The second tab is active, but another tab of its node - a panel, the Git view - is in
	// front of it. It stays active, so commands still act on it, yet nothing on screen shows it.
	const auto onlyFirstShown = [&first](const std::filesystem::path& path) { return path == first; };
	CHECK_FALSE(Graphite::ShownActiveDocument(documents, onlyFirstShown).has_value());

	const auto bothShown = [](const std::filesystem::path&) { return true; };
	CHECK(Graphite::ShownActiveDocument(documents, bothShown) == std::optional<std::size_t>{1});
}

TEST_CASE("The active tab is the shown one only while it is on screen, whether or not it holds text")
{
	TempDirectory temp;
	const std::filesystem::path readable = temp.MakeFile("readable.cpp", "int x;");

	Core::DocumentSet documents;
	REQUIRE(documents.Open(readable, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);
	REQUIRE(documents.Open(temp.GetPath() / "missing.cpp", Core::TabKind::Permanent) == Core::OpenOutcome::Failed);

	const auto allShown = [](const std::filesystem::path&) { return true; };
	CHECK(Graphite::ShownActiveTab(documents, allShown) == std::optional<std::size_t>{1});

	const auto onlyReadableShown = [&readable](const std::filesystem::path& path) { return path == readable; };
	CHECK_FALSE(Graphite::ShownActiveTab(documents, onlyReadableShown).has_value());
}

TEST_CASE("A shown tab whose file could not be read is not a shown text document")
{
	TempDirectory temp;

	Core::DocumentSet documents;
	REQUIRE(documents.Open(temp.GetPath() / "missing.cpp", Core::TabKind::Permanent) == Core::OpenOutcome::Failed);

	const auto allShown = [](const std::filesystem::path&) { return true; };
	CHECK_FALSE(Graphite::ShownActiveDocument(documents, allShown).has_value());
}
