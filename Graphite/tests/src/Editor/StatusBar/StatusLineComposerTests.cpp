#include <doctest/doctest.h>

#include "Editor/StatusBar/StatusLineComposer.h"

using namespace Graphite;

namespace {

	ServerStatus Clangd(ServerState state)
	{
		ServerStatus status;
		status.State = state;
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
	CHECK(line->Text == "clangd: ready");
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
	CHECK(line->Text == "clangd: analysing main.cpp");
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
	CHECK(line->Text == "C#: no language server");
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
	CHECK(line->Text == "clangd: indexing");
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
}
