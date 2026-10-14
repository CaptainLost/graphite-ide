#include <doctest/doctest.h>

#include "Editor/StatusBar/ServerStatusText.h"
#include "Language/Analysis/WorkspaceAnalysis.h"

#include <ostream>
#include <string>

TEST_CASE("A language with no server contributes nothing to the bar")
{
	Graphite::ServerStatus status;
	status.State = Graphite::ServerState::NotConfigured;

	CHECK(Graphite::ServerStatusText(status).empty());
}

TEST_CASE("Each running state names the language and the server and what it is doing")
{
	Graphite::ServerStatus status;
	status.Language = Graphite::LanguageId::Cpp;
	status.Name = "clangd";

	status.State = Graphite::ServerState::Starting;
	CHECK(Graphite::ServerStatusText(status) == "C++: clangd starting");

	status.State = Graphite::ServerState::Indexing;
	status.Percent = 42;
	CHECK(Graphite::ServerStatusText(status) == "C++: clangd indexing 42%");

	status.State = Graphite::ServerState::Ready;
	CHECK(Graphite::ServerStatusText(status) == "C++: clangd ready");

	status.State = Graphite::ServerState::NotFound;
	CHECK(Graphite::ServerStatusText(status) == "C++: clangd not found at the configured path");

	status.Problem = Graphite::ProjectProblem{.Summary = "no compile database", .Explanation = "No compile_commands.json, compile_flags.txt or .clangd was found for this project, so clangd can see only the files that are open rather than the whole project."};
	status.State = Graphite::ServerState::ProjectIncomplete;
	CHECK(Graphite::ServerStatusText(status) == "C++: clangd \xE2\x80\x94 no compile database");

	status.State = Graphite::ServerState::Failed;
	CHECK(Graphite::ServerStatusText(status) == "C++: clangd failed");
}

TEST_CASE("Indexing without a reported percentage omits the number")
{
	Graphite::ServerStatus status;
	status.Language = Graphite::LanguageId::Cpp;
	status.Name = "clangd";
	status.State = Graphite::ServerState::Indexing;
	status.Percent = Graphite::kNoPercentReported;

	CHECK(Graphite::ServerStatusText(status) == "C++: clangd indexing");
}

TEST_CASE("A status that names no language has no language prefix")
{
	Graphite::ServerStatus status;
	status.Name = "clangd";
	status.State = Graphite::ServerState::Ready;

	CHECK(Graphite::ServerStatusText(status) == "clangd ready");
}

TEST_CASE("The detail explains what is missing rather than repeating the state")
{
	Graphite::ServerStatus status;
	status.Name = "clangd";
	status.Problem = Graphite::ProjectProblem{.Summary = "no compile database", .Explanation = "No compile_commands.json, compile_flags.txt or .clangd was found for this project, so clangd can see only the files that are open rather than the whole project."};
	status.State = Graphite::ServerState::ProjectIncomplete;

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

TEST_CASE("A file still being analysed is named, so the wait has a subject")
{
	Graphite::ServerStatus status;
	status.Language = Graphite::LanguageId::Cpp;
	status.Name = "clangd";
	status.State = Graphite::ServerState::Ready;

	CHECK(Graphite::AnalysingText(status, "Application.cpp") == "C++: clangd analysing Application.cpp");
}

TEST_CASE("A server that is busy or broken outranks a file waiting on it")
{
	Graphite::ServerStatus status;
	status.Language = Graphite::LanguageId::Cpp;
	status.Name = "clangd";

	status.State = Graphite::ServerState::Indexing;
	status.Percent = 42;
	CHECK(Graphite::AnalysingText(status, "Application.cpp") == "C++: clangd indexing 42%");

	status.State = Graphite::ServerState::Failed;
	CHECK(Graphite::AnalysingText(status, "Application.cpp") == "C++: clangd failed");

	status.State = Graphite::ServerState::NotFound;
	CHECK(Graphite::AnalysingText(status, "Application.cpp") == "C++: clangd not found at the configured path");
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
	status.Language = Graphite::LanguageId::Cpp;
	status.Name = "clangd";
	status.State = Graphite::ServerState::Restricted;

	CHECK(Graphite::ServerStatusText(status) == "C++: clangd \xE2\x80\x94 restricted mode");
	CHECK(Graphite::ServerStatusDetail(status).find("not trusted") != std::string::npos);
}

TEST_CASE("A restricted server names the folder that is not trusted")
{
	Graphite::ServerStatus status;
	status.Name = "clangd";
	status.State = Graphite::ServerState::Restricted;
	status.Detail = "D:\\Elsewhere\\Project";

	const std::string detail = Graphite::ServerStatusDetail(status);

	CHECK(detail.find("D:\\Elsewhere\\Project") != std::string::npos);
	CHECK(detail.find("not trusted") != std::string::npos);
	CHECK(detail.find("open as the workspace") != std::string::npos);
}

TEST_CASE("A language whose server is turned off says so")
{
	Graphite::ServerStatus status;
	status.Language = Graphite::LanguageId::Cpp;
	status.Name = "C++";
	status.State = Graphite::ServerState::Off;

	CHECK(Graphite::ServerStatusText(status) == "C++: language server off");
	CHECK(Graphite::ServerStatusDetail(status).find("turned off") != std::string::npos);
}

TEST_CASE("A server that is not installed or lacks its runtime says so")
{
	Graphite::ServerStatus status;
	status.Language = Graphite::LanguageId::CSharp;
	status.Name = "Roslyn";

	status.State = Graphite::ServerState::NotInstalled;
	CHECK(Graphite::ServerStatusText(status) == "C#: Roslyn not installed");
	CHECK(Graphite::ServerStatusDetail(status) == "Roslyn is not installed.");

	status.Detail = "it is not in Graphite's servers folder or on PATH";
	CHECK(Graphite::ServerStatusDetail(status) == "Roslyn is not installed: it is not in Graphite's servers folder or on PATH.");

	status.State = Graphite::ServerState::MissingRuntime;
	status.Detail = "needs .NET 10";
	CHECK(Graphite::ServerStatusText(status) == "C#: Roslyn needs .NET 10");
	CHECK(Graphite::ServerStatusDetail(status).find("DOTNET_ROOT") != std::string::npos);
}

TEST_CASE("A configured path that does not exist is named in the tooltip")
{
	Graphite::ServerStatus status;
	status.Language = Graphite::LanguageId::Cpp;
	status.Name = "clangd";
	status.State = Graphite::ServerState::NotFound;
	status.Detail = "the configured path 'D:\\Gone\\clangd.exe' does not exist";

	CHECK(Graphite::ServerStatusText(status) == "C++: clangd not found at the configured path");
	CHECK(Graphite::ServerStatusDetail(status) == "clangd was not found: the configured path 'D:\\Gone\\clangd.exe' does not exist.");
}

TEST_CASE("A server loading its projects says what it loads until it reports a percentage")
{
	Graphite::ServerStatus status;
	status.Language = Graphite::LanguageId::CSharp;
	status.Name = "Roslyn";
	status.State = Graphite::ServerState::Indexing;
	status.Loading = "Demo.slnx";

	CHECK(Graphite::ServerStatusText(status) == "C#: Roslyn loading Demo.slnx");

	status.Percent = 40;
	CHECK(Graphite::ServerStatusText(status) == "C#: Roslyn indexing 40%");
}

TEST_CASE("A language that could have a server lacks support and one that could not lacks a server")
{
	CHECK(Graphite::NoServerText(Graphite::LanguageId::CSharp) == "C#: no language support");
	CHECK(Graphite::NoServerDetail(Graphite::LanguageId::CSharp).find("Settings") != std::string::npos);
	CHECK(Graphite::NoServerText(Graphite::LanguageId::TypeScript) == "TypeScript: no language server");
	CHECK(Graphite::NoServerText(Graphite::LanguageId::PlainText).empty());
}
