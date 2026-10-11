#include <doctest/doctest.h>

#include "ImGuiFrames.h"

#include "Language/Server/ServerChoice.h"
#include "UI/LanguageSupportBar.h"

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace {

	using Buttons = std::vector<Graphite::LanguageBarChoice>;

	constexpr std::uint64_t kMegabyte = 1024 * 1024;

	Graphite::LanguagePromptInputs RoslynIn(Graphite::ServerState state)
	{
		return Graphite::LanguagePromptInputs{.Language = Graphite::LanguageId::CSharp,
											  .Choice = Graphite::ChooseServer(Graphite::LanguageId::CSharp, std::string{"roslyn"}),
											  .State = state};
	}

	void DrawInWindow(const Graphite::LanguagePrompt& prompt)
	{
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(1200.0f, 200.0f));
		ImGui::Begin("Editor area");
		Graphite::DrawLanguageSupportBar(prompt);
		ImGui::End();
	}
}

TEST_CASE("A language without a chosen server asks for one")
{
	const std::optional<Graphite::LanguagePrompt> prompt = Graphite::LanguagePromptFor(
		{.Language = Graphite::LanguageId::CSharp, .Choice = Graphite::ChooseServer(Graphite::LanguageId::CSharp, std::nullopt)});

	REQUIRE(prompt.has_value());
	CHECK(prompt->Message == "No C# language support. Choose a language server to enable go to definition, completion and diagnostics.");
	CHECK(prompt->Buttons == Buttons{Graphite::LanguageBarChoice::Configure, Graphite::LanguageBarChoice::NotNow});
	CHECK(prompt->Language == Graphite::LanguageId::CSharp);
}

TEST_CASE("A chosen server that is not installed offers its install")
{
	const std::optional<Graphite::LanguagePrompt> prompt = Graphite::LanguagePromptFor(RoslynIn(Graphite::ServerState::NotInstalled));

	REQUIRE(prompt.has_value());
	CHECK(prompt->Message == "C# support needs the Roslyn language server, which is not installed.");
	CHECK(prompt->Buttons ==
		  Buttons{Graphite::LanguageBarChoice::Install, Graphite::LanguageBarChoice::OpenSettings, Graphite::LanguageBarChoice::NotNow});
	CHECK(prompt->ServerId == "roslyn");

	Graphite::LanguagePromptInputs offline = RoslynIn(Graphite::ServerState::NotInstalled);
	offline.Install.CanDownload = false;

	REQUIRE(Graphite::LanguagePromptFor(offline).has_value());
	CHECK(Graphite::LanguagePromptFor(offline)->Buttons == Buttons{Graphite::LanguageBarChoice::OpenSettings, Graphite::LanguageBarChoice::NotNow});
}

TEST_CASE("A missing runtime points at its download")
{
	const std::optional<Graphite::LanguagePrompt> prompt = Graphite::LanguagePromptFor(RoslynIn(Graphite::ServerState::MissingRuntime));

	REQUIRE(prompt.has_value());
	CHECK(prompt->Message == "C# support through Roslyn needs .NET 10, which was not found.");
	CHECK(prompt->Buttons ==
		  Buttons{Graphite::LanguageBarChoice::GetRuntime, Graphite::LanguageBarChoice::OpenSettings, Graphite::LanguageBarChoice::NotNow});
	CHECK(prompt->RuntimeMajor == 10);
}

TEST_CASE("An install under way shows its progress in the bar")
{
	Graphite::LanguagePromptInputs installing = RoslynIn(Graphite::ServerState::NotInstalled);
	installing.Install.Running = Graphite::InstallJobKind::Install;
	installing.Install.Latest = Graphite::InstallProgress{.ServerId = "roslyn",
														  .Step = Graphite::InstallStep::Downloading,
														  .ReceivedBytes = 19 * kMegabyte + kMegabyte / 2,
														  .TotalBytes = 57 * kMegabyte};

	const std::optional<Graphite::LanguagePrompt> prompt = Graphite::LanguagePromptFor(installing);

	REQUIRE(prompt.has_value());
	CHECK(prompt->Message == "Installing Roslyn for C#: Downloading 34% (19 / 57 MB)");
	CHECK(prompt->Buttons == Buttons{Graphite::LanguageBarChoice::Cancel, Graphite::LanguageBarChoice::NotNow});

	installing.Install.Cancelling = true;

	REQUIRE(Graphite::LanguagePromptFor(installing).has_value());
	CHECK(Graphite::LanguagePromptFor(installing)->Message == "Installing Roslyn for C#: Cancelling");
	CHECK(Graphite::LanguagePromptFor(installing)->Buttons == Buttons{Graphite::LanguageBarChoice::NotNow});
}

TEST_CASE("No bar is offered where there is nothing to offer or it was dismissed")
{
	Graphite::LanguagePromptInputs dismissed = RoslynIn(Graphite::ServerState::NotInstalled);
	dismissed.Dismissed = true;

	Graphite::LanguagePromptInputs restricted = RoslynIn(Graphite::ServerState::NotInstalled);
	restricted.RestrictedModeShown = true;

	Graphite::LanguagePromptInputs uninstalling = RoslynIn(Graphite::ServerState::NotInstalled);
	uninstalling.Install.Running = Graphite::InstallJobKind::Uninstall;

	const Graphite::LanguagePromptInputs turnedOff{.Language = Graphite::LanguageId::CSharp,
												   .Choice = Graphite::ChooseServer(Graphite::LanguageId::CSharp, std::string{"none"})};
	const Graphite::LanguagePromptInputs typeScript{.Language = Graphite::LanguageId::TypeScript};

	CHECK_FALSE(Graphite::LanguagePromptFor(dismissed).has_value());
	CHECK_FALSE(Graphite::LanguagePromptFor(restricted).has_value());
	CHECK_FALSE(Graphite::LanguagePromptFor(uninstalling).has_value());
	CHECK_FALSE(Graphite::LanguagePromptFor(turnedOff).has_value());
	CHECK_FALSE(Graphite::LanguagePromptFor(typeScript).has_value());
	CHECK_FALSE(Graphite::LanguagePromptFor(RoslynIn(Graphite::ServerState::Ready)).has_value());
	CHECK_FALSE(Graphite::LanguagePromptFor(RoslynIn(Graphite::ServerState::Restricted)).has_value());
}

TEST_CASE("An install job running over a ready server puts no bar over the editor")
{
	Graphite::LanguagePromptInputs updating = RoslynIn(Graphite::ServerState::Ready);
	updating.Install.Running = Graphite::InstallJobKind::Install;

	CHECK_FALSE(Graphite::LanguagePromptFor(updating).has_value());
}

TEST_CASE("Each bar button has its label")
{
	CHECK(std::string{Graphite::LanguageBarChoiceLabel(Graphite::LanguageBarChoice::Configure)} == "Configure");
	CHECK(std::string{Graphite::LanguageBarChoiceLabel(Graphite::LanguageBarChoice::Install)} == "Install");
	CHECK(std::string{Graphite::LanguageBarChoiceLabel(Graphite::LanguageBarChoice::Cancel)} == "Cancel");
	CHECK(std::string{Graphite::LanguageBarChoiceLabel(Graphite::LanguageBarChoice::GetRuntime)} == "Get .NET");
	CHECK(std::string{Graphite::LanguageBarChoiceLabel(Graphite::LanguageBarChoice::OpenSettings)} == "Settings");
	CHECK(std::string{Graphite::LanguageBarChoiceLabel(Graphite::LanguageBarChoice::NotNow)} == "Not now");
}

TEST_CASE("Every kind of bar draws without an ImGui assertion")
{
	ImGuiFrames frames;
	const std::optional<Graphite::LanguagePrompt> choose = Graphite::LanguagePromptFor(
		{.Language = Graphite::LanguageId::CSharp, .Choice = Graphite::ChooseServer(Graphite::LanguageId::CSharp, std::nullopt)});
	const std::optional<Graphite::LanguagePrompt> install = Graphite::LanguagePromptFor(RoslynIn(Graphite::ServerState::NotInstalled));
	const std::optional<Graphite::LanguagePrompt> runtime = Graphite::LanguagePromptFor(RoslynIn(Graphite::ServerState::MissingRuntime));

	REQUIRE(choose.has_value());
	REQUIRE(install.has_value());
	REQUIRE(runtime.has_value());

	frames.Next([&choose] { DrawInWindow(*choose); });
	frames.Next([&install] { DrawInWindow(*install); });
	frames.Next([&runtime] { DrawInWindow(*runtime); });
	frames.Next([&runtime] { DrawInWindow(*runtime); });
}

TEST_CASE("C++ with no server chosen offers to choose one")
{
	const std::optional<Graphite::LanguagePrompt> prompt = Graphite::LanguagePromptFor(
		Graphite::LanguagePromptInputs{.Language = Graphite::LanguageId::Cpp,
									   .Choice = Graphite::ChooseServer(Graphite::LanguageId::Cpp, std::nullopt),
									   .State = Graphite::ServerState::NotConfigured});

	REQUIRE(prompt.has_value());
	CHECK(prompt->Message == "No C++ language support. Choose a language server to enable go to definition, completion and diagnostics.");
	CHECK(prompt->Buttons == Buttons{Graphite::LanguageBarChoice::Configure, Graphite::LanguageBarChoice::NotNow});
}
