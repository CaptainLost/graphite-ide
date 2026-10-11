#include <doctest/doctest.h>

#include "Editor/StatusBar/StatusLineComposer.h"

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

TEST_CASE("No folder, no document and no server leaves the server slot empty")
{
	CHECK_FALSE(ChooseServerLine(ServerLineInputs{}).has_value());

	// A closed folder is never restricted: there is nothing for the bar to be about.
	ServerLineInputs closed;
	closed.FolderOpen = false;
	closed.FolderTrusted = false;

	CHECK_FALSE(ChooseServerLine(closed).has_value());

	// A server still running from the folder just closed says nothing about no folder.
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
	CHECK(ChooseServerLine(inputs)->Text == "C#: installing Roslyn");

	inputs.ActiveJob = ServerJob{.Kind = InstallJobKind::Uninstall, .Server = "Roslyn"};
	CHECK(ChooseServerLine(inputs)->Text == "C#: uninstalling Roslyn");
	CHECK(ChooseServerLine(inputs)->Detail == "Graphite is removing its install of Roslyn.");

	inputs.ActiveJob = ServerJob{.Kind = InstallJobKind::CheckForUpdates, .Server = "Roslyn"};
	CHECK(ChooseServerLine(inputs)->Text == "C++: clangd ready");
}

TEST_CASE("The server's words open the settings of the language they speak for")
{
	ServerLineInputs active;
	active.Active = Clangd(ServerState::Ready);
	active.ActiveLanguage = LanguageId::Cpp;
	CHECK(ChooseServerLine(active)->SettingsLanguage == LanguageId::Cpp);

	ServerLineInputs unserved;
	unserved.ActiveLanguage = LanguageId::CSharp;
	CHECK(ChooseServerLine(unserved)->SettingsLanguage == LanguageId::CSharp);

	ServerLineInputs typeScript;
	typeScript.ActiveLanguage = LanguageId::TypeScript;
	CHECK_FALSE(ChooseServerLine(typeScript)->SettingsLanguage.has_value());

	ServerLineInputs restricted;
	restricted.FolderOpen = true;
	CHECK_FALSE(ChooseServerLine(restricted)->SettingsLanguage.has_value());

	ServerLineInputs workspace;
	workspace.FolderOpen = true;
	workspace.FolderTrusted = true;
	workspace.Overall = Clangd(ServerState::Indexing);
	CHECK(ChooseServerLine(workspace)->SettingsLanguage == LanguageId::Cpp);
}
