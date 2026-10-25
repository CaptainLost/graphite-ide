#include <doctest/doctest.h>

#include "Language/Diagnostics/DiagnosticsModel.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

	const std::filesystem::path kMain{"C:/src/Main.cpp"};

	bool Everything(const std::filesystem::path&)
	{
		return true;
	}

	Graphite::Diagnostic Error(std::size_t line, std::string message)
	{
		Graphite::Diagnostic diagnostic;
		diagnostic.Line = line;
		diagnostic.Severity = Graphite::DiagnosticSeverity::Error;
		diagnostic.Message = std::move(message);

		return diagnostic;
	}

	Graphite::PublishedDiagnostics Published(int version, std::vector<Graphite::Diagnostic> items)
	{
		return Graphite::PublishedDiagnostics{kMain, Graphite::DocumentVersion{version},
											  std::move(items)};
	}
}

TEST_CASE("A document with nothing said about it reads as empty rather than as absent")
{
	const Graphite::DiagnosticsModel model;

	CHECK(model.For(kMain).empty());
	CHECK(!model.IsStale(kMain));
}

TEST_CASE("A publication replaces a document's set wholesale")
{
	Graphite::DiagnosticsModel model;

	Graphite::DiagnosticsUpdate firstUpdate;
	firstUpdate.Published.push_back(Published(1, {Error(3, "one"), Error(9, "two")}));
	model.Apply(firstUpdate);

	Graphite::DiagnosticsUpdate secondUpdate;
	secondUpdate.Published.push_back(Published(2, {Error(3, "one")}));
	model.Apply(secondUpdate);

	REQUIRE(model.For(kMain).size() == 1);
	CHECK(model.For(kMain)[0].Message == "one");
}

TEST_CASE("An empty publication is an answer, not a silence")
{
	Graphite::DiagnosticsModel model;

	Graphite::DiagnosticsUpdate firstUpdate;
	firstUpdate.Published.push_back(Published(1, {Error(3, "one")}));
	model.Apply(firstUpdate);

	Graphite::DiagnosticsUpdate cleared;
	cleared.Published.push_back(Published(2, {}));
	model.Apply(cleared);

	CHECK(model.For(kMain).empty());
}

TEST_CASE("A set the buffer has outrun is stale")
{
	Graphite::DiagnosticsModel model;

	Graphite::DiagnosticsUpdate answered;
	answered.Versions.push_back({kMain, Graphite::DocumentVersion{4}});
	answered.Published.push_back(Published(4, {Error(3, "one")}));
	model.Apply(answered);

	CHECK(!model.IsStale(kMain));

	Graphite::DiagnosticsUpdate typed;
	typed.Versions.push_back({kMain, Graphite::DocumentVersion{5}});
	model.Apply(typed);

	CHECK(model.IsStale(kMain));
	CHECK(model.For(kMain).size() == 1);
	CHECK(model.Total(Everything).Errors == 1);
}

TEST_CASE("A publication naming no version reads as stale once anything has been sent")
{
	Graphite::DiagnosticsModel model;

	Graphite::DiagnosticsUpdate versionless;
	versionless.Published.push_back(
		Graphite::PublishedDiagnostics{kMain, Graphite::DocumentVersion{}, {Error(3, "one")}});
	model.Apply(versionless);

	Graphite::DiagnosticsUpdate typed;
	typed.Versions.push_back({kMain, Graphite::DocumentVersion{1}});
	model.Apply(typed);

	CHECK(model.IsStale(kMain));
}

TEST_CASE("A version the server gave up answering is remembered, and only for a document it was sent")
{
	Graphite::DiagnosticsModel model;

	Graphite::DiagnosticsUpdate sent;
	sent.Versions.push_back({kMain, Graphite::DocumentVersion{3}});
	model.Apply(sent);

	Graphite::DiagnosticsUpdate givenUp;
	givenUp.Unanswered.push_back({kMain, Graphite::DocumentVersion{3}});
	givenUp.Unanswered.push_back({"C:/src/NeverSent.cpp", Graphite::DocumentVersion{1}});
	model.Apply(givenUp);

	REQUIRE(model.GetDocuments().size() == 1);
	CHECK(model.GetDocuments().at(kMain).GaveUpOn == Graphite::DocumentVersion{3});
	CHECK_FALSE(model.GetDocuments().at(kMain).HasAnswer);
}

TEST_CASE("A close forgets a document entirely")
{
	Graphite::DiagnosticsModel model;

	Graphite::DiagnosticsUpdate opened;
	opened.Versions.push_back({kMain, Graphite::DocumentVersion{1}});
	opened.Published.push_back(Published(1, {Error(3, "one")}));
	model.Apply(opened);

	Graphite::DiagnosticsUpdate closed;
	closed.Closed.push_back(kMain);
	model.Apply(closed);

	CHECK(model.For(kMain).empty());
	CHECK(model.GetDocuments().empty());
}

TEST_CASE("A document closed and reopened within one update describes its new life")
{
	Graphite::DiagnosticsModel model;

	Graphite::DiagnosticsUpdate before;
	before.Versions.push_back({kMain, Graphite::DocumentVersion{47}});
	before.Published.push_back(Published(47, {Error(3, "stale")}));
	model.Apply(before);

	Graphite::DiagnosticsUpdate restart;
	restart.Closed.push_back(kMain);
	restart.Versions.push_back({kMain, Graphite::DocumentVersion{1}});
	restart.Published.push_back(Published(1, {Error(5, "fresh")}));
	model.Apply(restart);

	REQUIRE(model.For(kMain).size() == 1);
	CHECK(model.For(kMain)[0].Message == "fresh");
	CHECK(!model.IsStale(kMain));
}

TEST_CASE("Two publications at the same version still move the generation")
{
	Graphite::DiagnosticsModel model;

	Graphite::DiagnosticsUpdate firstUpdate;
	firstUpdate.Published.push_back(Published(1, {Error(3, "one")}));
	model.Apply(firstUpdate);

	const std::uint64_t firstGeneration = model.GetDocuments().at(kMain).Generation;

	Graphite::DiagnosticsUpdate secondUpdate;
	secondUpdate.Published.push_back(Published(1, {Error(3, "one"), Error(9, "two")}));
	model.Apply(secondUpdate);

	CHECK(model.GetDocuments().at(kMain).Generation != firstGeneration);
	CHECK(model.For(kMain).size() == 2);
}

TEST_CASE("Applying nothing about a document leaves its generation alone")
{
	Graphite::DiagnosticsModel model;

	Graphite::DiagnosticsUpdate firstUpdate;
	firstUpdate.Published.push_back(Published(1, {Error(3, "one")}));
	model.Apply(firstUpdate);

	const std::uint64_t generation = model.GetDocuments().at(kMain).Generation;

	Graphite::DiagnosticsUpdate other;
	other.Published.push_back(
		Graphite::PublishedDiagnostics{std::filesystem::path{"C:/src/Other.cpp"},
									   Graphite::DocumentVersion{1},
									   {Error(0, "x")}});
	model.Apply(other);

	CHECK(model.GetDocuments().at(kMain).Generation == generation);
}

TEST_CASE("Errors and warnings are counted separately across every document a predicate admits")
{
	Graphite::DiagnosticsModel model;

	Graphite::Diagnostic warning;
	warning.Line = 1;
	warning.Severity = Graphite::DiagnosticSeverity::Warning;

	Graphite::DiagnosticsUpdate update;
	update.Published.push_back(Published(1, {Error(3, "one"), warning}));
	update.Published.push_back(Graphite::PublishedDiagnostics{
		std::filesystem::path{"C:/src/Other.cpp"}, Graphite::DocumentVersion{1}, {Error(0, "x")}});
	model.Apply(update);

	CHECK(model.Total(Everything).Errors == 2);
	CHECK(model.Total(Everything).Warnings == 1);
}

TEST_CASE("Total counts only the documents its predicate admits")
{
	Graphite::DiagnosticsModel model;

	const std::filesystem::path other{"C:/src/Other.cpp"};

	Graphite::Diagnostic warning;
	warning.Line = 1;
	warning.Severity = Graphite::DiagnosticSeverity::Warning;

	Graphite::DiagnosticsUpdate update;
	update.Published.push_back(Published(1, {Error(3, "one")}));
	update.Published.push_back(
		Graphite::PublishedDiagnostics{other, Graphite::DocumentVersion{1}, {warning}});
	model.Apply(update);

	const Graphite::DiagnosticsModel::Counts counts =
		model.Total([](const std::filesystem::path& path) { return path == kMain; });

	CHECK(counts.Errors == 1);
	CHECK(counts.Warnings == 0);
}
