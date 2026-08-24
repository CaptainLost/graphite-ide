#include <doctest/doctest.h>

#include "Lang/Diagnostics/DiagnosticsModel.h"

#include <filesystem>
#include <string>
#include <vector>

namespace {

	const std::filesystem::path kMain{"C:/src/Main.cpp"};

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
	// A reference to an empty vector, not a pointer: every caller loops, and none of them should
	// have to branch on whether a language server exists.
	const Graphite::DiagnosticsModel model;

	CHECK(model.For(kMain).empty());
	CHECK(!model.IsStale(kMain));
}

TEST_CASE("A publication replaces a document's set wholesale")
{
	// A server's publication is about the whole document: what it does not mention is fixed.
	Graphite::DiagnosticsModel model;

	Graphite::DiagnosticsUpdate first;
	first.Published.push_back(Published(1, {Error(3, "one"), Error(9, "two")}));
	model.Apply(first);

	Graphite::DiagnosticsUpdate second;
	second.Published.push_back(Published(2, {Error(3, "one")}));
	model.Apply(second);

	REQUIRE(model.For(kMain).size() == 1);
	CHECK(model.For(kMain)[0].Message == "one");
}

TEST_CASE("An empty publication is an answer, not a silence")
{
	Graphite::DiagnosticsModel model;

	Graphite::DiagnosticsUpdate first;
	first.Published.push_back(Published(1, {Error(3, "one")}));
	model.Apply(first);

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
	CHECK(model.For(kMain).size() == 1);   // dimmed, not dropped
	CHECK(model.Total().Errors == 1);      // Total counts a stale entry's items too - dimmed, not dropped
}

TEST_CASE("A publication naming no version reads as stale once anything has been sent")
{
	// The protocol allows a publication that names no version at all - ParseDiagnostics treats
	// version as optional, matching the LSP spec. DocumentVersion{} is both "never sent" and
	// what such a reply reads as, so this must not be mistaken for "never answered": the safe
	// reading is stale as soon as anything has been sent, per DocumentVersion's own comment.
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
	CHECK(model.Documents().empty());
}

TEST_CASE("A document closed and reopened within one update describes its new life")
{
	// This is a restart: the same update carries the close, the version that replaced it, and
	// the first diagnostics against the new session - the real shape of a re-diagnosed reopen.
	// Applying the close last would let it erase the very publication this same update just
	// wrote, since versions and publications write disjoint fields and nothing else distinguishes
	// the two orderings: this is the case that actually pins closes running first.
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

	// The old set is gone, and the new one - published within the same update that reopened the
	// document - survives the close rather than being wiped out by it.
	REQUIRE(model.For(kMain).size() == 1);
	CHECK(model.For(kMain)[0].Message == "fresh");
	CHECK(!model.IsStale(kMain));
}

TEST_CASE("Errors and warnings are counted separately across every document")
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

	CHECK(model.Total().Errors == 2);
	CHECK(model.Total().Warnings == 1);
}
