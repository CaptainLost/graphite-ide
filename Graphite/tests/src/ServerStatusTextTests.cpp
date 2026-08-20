#include <doctest/doctest.h>

#include "Editor/ServerStatusText.h"

TEST_CASE("A language with no server contributes nothing to the bar")
{
	Graphite::ServerStatus status;
	status.State = Graphite::ServerState::NotConfigured;

	CHECK(Graphite::ServerStatusText(status).empty());
}

TEST_CASE("Each running state names the server and what it is doing")
{
	Graphite::ServerStatus status;
	status.Name = "clangd";

	status.State = Graphite::ServerState::Starting;
	CHECK(Graphite::ServerStatusText(status) == "clangd: starting");

	status.State = Graphite::ServerState::Indexing;
	status.Percent = 42;
	CHECK(Graphite::ServerStatusText(status) == "clangd: indexing 42%");

	status.State = Graphite::ServerState::Ready;
	CHECK(Graphite::ServerStatusText(status) == "clangd: ready");

	status.State = Graphite::ServerState::NotFound;
	CHECK(Graphite::ServerStatusText(status) == "clangd: not found");

	status.State = Graphite::ServerState::NoCompileDatabase;
	CHECK(Graphite::ServerStatusText(status) == "clangd: no compile database");

	status.State = Graphite::ServerState::Failed;
	CHECK(Graphite::ServerStatusText(status) == "clangd: failed");
}

TEST_CASE("Indexing without a reported percentage omits the number")
{
	Graphite::ServerStatus status;
	status.Name = "clangd";
	status.State = Graphite::ServerState::Indexing;
	status.Percent = -1;

	CHECK(Graphite::ServerStatusText(status) == "clangd: indexing");
}

TEST_CASE("The detail explains what is missing rather than repeating the state")
{
	Graphite::ServerStatus status;
	status.Name = "clangd";
	status.State = Graphite::ServerState::NoCompileDatabase;

	const std::string detail = Graphite::ServerStatusDetail(status);

	CHECK(detail.find("compile_commands.json") != std::string::npos);
	CHECK(detail.find("compile_flags.txt") != std::string::npos);
}

TEST_CASE("The detail reports restarts once there have been any")
{
	Graphite::ServerStatus status;
	status.Name = "clangd";
	status.State = Graphite::ServerState::Ready;
	status.Detail = "clangd version 19.1.0";
	status.Restarts = 2;

	CHECK(Graphite::ServerStatusDetail(status).find("restarted 2") != std::string::npos);
}

// The gap the status bar had nothing to say about: the server is up and answering, but the file
// on screen has questions outstanding, so its colours are still the lexer's own and its reference
// counts are still blank. "ready" is true and unhelpful there - it answers about the server when
// the question is about the file.
TEST_CASE("A file still being analysed is named, so the wait has a subject")
{
	Graphite::ServerStatus status;
	status.Name = "clangd";
	status.State = Graphite::ServerState::Ready;

	CHECK(Graphite::AnalysingText(status, "Application.cpp") == "clangd: analysing Application.cpp");
}

TEST_CASE("A server that is busy or broken outranks a file waiting on it")
{
	Graphite::ServerStatus status;
	status.Name = "clangd";

	// Indexing the whole project is the larger truth: one file's questions are outstanding
	// *because* of it, and reporting the file would bury the reason.
	status.State = Graphite::ServerState::Indexing;
	status.Percent = 42;
	CHECK(Graphite::AnalysingText(status, "Application.cpp") == "clangd: indexing 42%");

	status.State = Graphite::ServerState::Failed;
	CHECK(Graphite::AnalysingText(status, "Application.cpp") == "clangd: failed");

	status.State = Graphite::ServerState::NotFound;
	CHECK(Graphite::AnalysingText(status, "Application.cpp") == "clangd: not found");
}

TEST_CASE("A language with no server says nothing, analysed or not")
{
	Graphite::ServerStatus status;
	status.State = Graphite::ServerState::NotConfigured;

	CHECK(Graphite::AnalysingText(status, "notes.txt").empty());
}
