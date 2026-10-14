#include <doctest/doctest.h>

#include "Editor/StatusBar/StatusLineComposer.h"

#include <optional>
#include <ostream>
#include <string>

using namespace Graphite;

namespace {

	ServerStatus Clangd(ServerState state)
	{
		ServerStatus status;
		status.State = state;
		status.Language = LanguageId::Cpp;
		status.Name = "clangd";
		status.Detail = "clangd version 19";

		return status;
	}
}

TEST_CASE("No problems leaves the problems slot empty rather than saying zero")
{
	CHECK_FALSE(ProblemCountText(0, 0).has_value());
}

TEST_CASE("Problem counts are spelled out, singular for one and joined errors first")
{
	CHECK(ProblemCountText(1, 0) == "1 error");
	CHECK(ProblemCountText(3, 0) == "3 errors");
	CHECK(ProblemCountText(0, 1) == "1 warning");
	CHECK(ProblemCountText(0, 2) == "2 warnings");
	CHECK(ProblemCountText(1, 1) == "1 error, 1 warning");
	CHECK(ProblemCountText(2, 5) == "2 errors, 5 warnings");
}

TEST_CASE("The active document's own server outranks everything else")
{
	ServerLineInputs inputs;
	inputs.Active = Clangd(ServerState::Ready);
	inputs.ActiveLanguage = LanguageId::Cpp;
	inputs.FolderOpen = true;
	inputs.FolderTrusted = false;
	inputs.Overall = Clangd(ServerState::Indexing);

	const std::optional<ServerLine> line = ChooseServerLine(inputs);

	REQUIRE(line.has_value());
	CHECK(line->Text == "C++: clangd ready");
	CHECK(line->Detail == ServerStatusDetail(inputs.Active));
}

TEST_CASE("A document whose answers are still coming is named while its server is ready")
{
	ServerLineInputs inputs;
	inputs.Active = Clangd(ServerState::Ready);
	inputs.ActiveLanguage = LanguageId::Cpp;
	inputs.AnalysingFile = "main.cpp";

	const std::optional<ServerLine> line = ChooseServerLine(inputs);

	REQUIRE(line.has_value());
	CHECK(line->Text == "C++: clangd analysing main.cpp");
	CHECK(line->Detail == ServerStatusDetail(inputs.Active));
}

TEST_CASE("A recognised language with no server says so ahead of restricted mode")
{
	ServerLineInputs inputs;
	inputs.ActiveLanguage = LanguageId::CSharp;
	inputs.FolderOpen = true;
	inputs.FolderTrusted = false;
	inputs.Overall = Clangd(ServerState::Ready);

	const std::optional<ServerLine> line = ChooseServerLine(inputs);

	REQUIRE(line.has_value());
	CHECK(line->Text == "C#: no language support");
	CHECK(line->Detail == NoServerDetail(LanguageId::CSharp));
}

TEST_CASE("An untrusted folder says restricted mode ahead of the workspace-wide status")
{
	ServerLineInputs inputs;
	inputs.FolderOpen = true;
	inputs.FolderTrusted = false;
	inputs.Overall = Clangd(ServerState::Ready);

	const std::optional<ServerLine> line = ChooseServerLine(inputs);

	REQUIRE(line.has_value());
	CHECK(line->Text == "Restricted mode");
	CHECK(line->Detail.find("not trusted") != std::string::npos);
}

TEST_CASE("With nothing open to speak for, the workspace's servers as a whole are shown")
{
	ServerLineInputs inputs;
	inputs.FolderOpen = true;
	inputs.FolderTrusted = true;
	inputs.Overall = Clangd(ServerState::Indexing);

	const std::optional<ServerLine> line = ChooseServerLine(inputs);

	REQUIRE(line.has_value());
	CHECK(line->Text == "C++: clangd indexing");
	CHECK(line->Detail == "clangd version 19");
}

TEST_CASE("No folder, no document and no server leaves the server slot empty, even beside a leftover server or an untrusted closed folder")
{
	const ServerLineInputs nothingOpen{};
	CHECK_FALSE(ChooseServerLine(nothingOpen).has_value());

	ServerLineInputs closed;
	closed.FolderOpen = false;
	closed.FolderTrusted = false;

	CHECK_FALSE(ChooseServerLine(closed).has_value());

	closed.Overall = Clangd(ServerState::Ready);

	CHECK_FALSE(ChooseServerLine(closed).has_value());
}

TEST_CASE("An install under way speaks for the active document's language first")
{
	ServerLineInputs inputs;
	inputs.Active = Clangd(ServerState::Ready);
	inputs.ActiveLanguage = LanguageId::CSharp;
	inputs.ActiveJob = ServerJob{.Kind = InstallJobKind::Install, .Server = "Roslyn", .Percent = 34};

	const std::optional<ServerLine> installing = ChooseServerLine(inputs);

	REQUIRE(installing.has_value());
	CHECK(installing->Text == "C#: installing Roslyn 34%");
	CHECK(installing->Detail == "Graphite is downloading and installing Roslyn. It starts as soon as it is in place.");
	CHECK(installing->SettingsLanguage == LanguageId::CSharp);

	inputs.ActiveJob->Percent.reset();
	const std::optional<ServerLine> installingWithoutPercent = ChooseServerLine(inputs);
	REQUIRE(installingWithoutPercent.has_value());
	CHECK(installingWithoutPercent->Text == "C#: installing Roslyn");

	inputs.ActiveJob = ServerJob{.Kind = InstallJobKind::Uninstall, .Server = "Roslyn"};
	const std::optional<ServerLine> uninstalling = ChooseServerLine(inputs);
	REQUIRE(uninstalling.has_value());
	CHECK(uninstalling->Text == "C#: uninstalling Roslyn");
	CHECK(uninstalling->Detail == "Graphite is removing its install of Roslyn.");

	inputs.ActiveJob = ServerJob{.Kind = InstallJobKind::CheckForUpdates, .Server = "Roslyn"};
	const std::optional<ServerLine> checking = ChooseServerLine(inputs);
	REQUIRE(checking.has_value());
	CHECK(checking->Text == "C++: clangd ready");
}

TEST_CASE("The server's words open the settings of the language they speak for")
{
	ServerLineInputs active;
	active.Active = Clangd(ServerState::Ready);
	active.ActiveLanguage = LanguageId::Cpp;
	const std::optional<ServerLine> activeLine = ChooseServerLine(active);
	REQUIRE(activeLine.has_value());
	CHECK(activeLine->SettingsLanguage == LanguageId::Cpp);

	ServerLineInputs unserved;
	unserved.ActiveLanguage = LanguageId::CSharp;
	const std::optional<ServerLine> unservedLine = ChooseServerLine(unserved);
	REQUIRE(unservedLine.has_value());
	CHECK(unservedLine->SettingsLanguage == LanguageId::CSharp);

	ServerLineInputs typeScript;
	typeScript.ActiveLanguage = LanguageId::TypeScript;
	const std::optional<ServerLine> typeScriptLine = ChooseServerLine(typeScript);
	REQUIRE(typeScriptLine.has_value());
	CHECK_FALSE(typeScriptLine->SettingsLanguage.has_value());

	ServerLineInputs restricted;
	restricted.FolderOpen = true;
	const std::optional<ServerLine> restrictedLine = ChooseServerLine(restricted);
	REQUIRE(restrictedLine.has_value());
	CHECK_FALSE(restrictedLine->SettingsLanguage.has_value());

	ServerLineInputs workspace;
	workspace.FolderOpen = true;
	workspace.FolderTrusted = true;
	workspace.Overall = Clangd(ServerState::Indexing);
	const std::optional<ServerLine> workspaceLine = ChooseServerLine(workspace);
	REQUIRE(workspaceLine.has_value());
	CHECK(workspaceLine->SettingsLanguage == LanguageId::Cpp);
}
