#include <doctest/doctest.h>

#include "Editor/ServerStatusText.h"
#include "Language/Analysis/WorkspaceAnalysis.h"

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

TEST_CASE("The bar names the background pass while it is finding files")
{
	Graphite::AnalysisProgress progress;
	progress.Walking = true;

	CHECK(Graphite::BackgroundAnalysisText(progress) == "analysing workspace: finding files");
}

TEST_CASE("The bar counts the background pass against what it found")
{
	Graphite::AnalysisProgress progress;
	progress.Done = 42;
	progress.Total = 385;

	CHECK(Graphite::BackgroundAnalysisText(progress) == "analysing workspace: 42 of 385");
}

TEST_CASE("A finished background pass says nothing at all")
{
	Graphite::AnalysisProgress progress;
	progress.Done = 385;
	progress.Total = 385;

	CHECK(Graphite::BackgroundAnalysisText(progress).empty());
}

TEST_CASE("A background pass with nothing queued says nothing at all")
{
	CHECK(Graphite::BackgroundAnalysisText(Graphite::AnalysisProgress{}).empty());
}

TEST_CASE("The background tooltip says what is kept against what may be")
{
	Graphite::AnalysisProgress progress;
	progress.Done = 42;
	progress.Total = 385;

	const std::string detail =
		Graphite::BackgroundAnalysisDetail(progress, 34ull * 1024 * 1024, 128ull * 1024 * 1024);

	CHECK(detail.find("42 of 385") != std::string::npos);
	CHECK(detail.find("34 MB") != std::string::npos);
	CHECK(detail.find("128 MB") != std::string::npos);
}

TEST_CASE("A restricted server says so and says why")
{
	Graphite::ServerStatus status;
	status.Name = "clangd";
	status.State = Graphite::ServerState::Restricted;

	CHECK(Graphite::ServerStatusText(status) == "clangd: restricted mode");
	CHECK(Graphite::ServerStatusDetail(status).find("not trusted") != std::string::npos);
}

// The root can lie above the open folder, or be a different folder altogether, so the detail
// has to say which one it means for the remedy to be something the user can act on.
TEST_CASE("A restricted server names the folder that is not trusted")
{
	Graphite::ServerStatus status;
	status.Name = "clangd";
	status.State = Graphite::ServerState::Restricted;
	status.Detail = "D:\\Elsewhere\\Project";

	const std::string detail = Graphite::ServerStatusDetail(status);

	CHECK(detail.find("D:\\Elsewhere\\Project") != std::string::npos);
	CHECK(detail.find("not trusted") != std::string::npos);
	// The remedy must be reachable with no folder open, where no bar is on screen to offer it.
	CHECK(detail.find("open as the workspace") != std::string::npos);
}
