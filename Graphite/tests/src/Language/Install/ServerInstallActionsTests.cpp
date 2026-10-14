#include <doctest/doctest.h>

#include "DeferredJobs.h"
#include "FakeHttpClient.h"
#include "InstalledServers.h"
#include "Language/Install/InstalledServer.h"
#include "Language/Install/ServerInstallActions.h"
#include "Language/Install/ServerInstalls.h"
#include "Language/Server/LanguageServices.h"
#include "Language/Server/ServerChoice.h"
#include "TempDirectory.h"

#include "Core/FileSystem/FileLock.h"

#include <cstddef>
#include <filesystem>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

	struct CountingFactory
	{
		std::size_t Made = 0;

		Graphite::LanguageServices::ClientFactory Get()
		{
			return [this](const Graphite::ServerLaunch& launch, const std::filesystem::path& root) { return Make(launch, root); };
		}

		Core::Scope<Graphite::LspClient> Make(const Graphite::ServerLaunch& launch, const std::filesystem::path& root)
		{
			++Made;
			return Core::CreateScope<Graphite::LspClient>(launch, root, Graphite::LspClient::WithoutServer{});
		}
	};

	Graphite::ServerChoice ChooseCsharpLs(Graphite::LanguageId language)
	{
		return Graphite::ChooseServer(language, std::string{"csharp-ls"});
	}

	std::vector<Graphite::DotnetInstallation> DotnetTen()
	{
		return {Graphite::DotnetInstallation{.Executable = "C:/dotnet/dotnet.exe", .RuntimeVersions = {"10.0.10"}}};
	}

	struct RunningCsharpLs
	{
		TempDirectory Temp;
		std::filesystem::path Servers = Temp.GetPath() / "servers";
		std::filesystem::path Version = InstallCsharpLsIn(Servers);
		std::filesystem::path Source = Temp.GetPath() / "Demo" / "Program.cs";
		DeferredJobs Jobs;
		Graphite::ServerInstalls Installs{nullptr, Servers, Jobs.Submitter()};
		Graphite::LanguageServices Services{Temp.GetPath()};
		CountingFactory Factory;

		RunningCsharpLs()
		{
			Installs.SweepAtStartup();
			Jobs.RunAll();
			Installs.Poll();

			Services.SetClientFactory(Factory.Get());
			Services.SetServersDirectory(Servers);
			Services.SetDotnetLocator(DotnetTen);
			Services.SetChoiceProvider(ChooseCsharpLs);
			Services.SetUninstallingQuery([this](std::string_view serverId) { return Installs.RunningJobOf(serverId) == Graphite::InstallJobKind::Uninstall; });

			Services.EnsureStarted(Source, Graphite::LanguageId::CSharp);
			Services.OpenDocument(Source, Graphite::LanguageId::CSharp, "class Program {}\n");
			REQUIRE(Factory.Made == 1);
		}

		void FinishJobs()
		{
			Jobs.RunAll();
			Installs.Poll();

			std::vector<Graphite::InstallProgress> finished;
			Installs.TakeFinished(finished);

			for (const Graphite::InstallProgress& outcome : finished)
			{
				Graphite::RestartServersAfter(outcome, Services);
			}
		}

		Graphite::ServerState State()
		{
			return Services.StatusFor(Source, Graphite::LanguageId::CSharp).State;
		}
	};
}

TEST_CASE("Uninstalling a running server stops it and holds it stopped until the job ends")
{
	RunningCsharpLs running;

	REQUIRE(Graphite::UninstallStoppingServers(running.Installs, running.Services, "csharp-ls"));

	CHECK(running.Factory.Made == 1);
	CHECK(running.State() == Graphite::ServerState::NotInstalled);
	CHECK(running.Services.Holds(running.Source, Graphite::LanguageId::CSharp));

	running.FinishJobs();

	CHECK(running.Factory.Made == 1);
	CHECK(running.State() == Graphite::ServerState::NotInstalled);
	CHECK_FALSE(std::filesystem::exists(running.Version));
	CHECK(running.Services.Holds(running.Source, Graphite::LanguageId::CSharp));
	CHECK(running.Services.StatusFor(running.Source, Graphite::LanguageId::CSharp).Detail != "it is being uninstalled");
}

TEST_CASE("A failed uninstall starts the stopped server again")
{
	RunningCsharpLs running;
	const Core::Result<Core::FileLock> otherWindow =
		Core::FileLock::TryAcquire(Graphite::LockFileOf(Graphite::ServerDirectoryOf(running.Servers, "csharp-ls")));
	REQUIRE(otherWindow);

	REQUIRE(Graphite::UninstallStoppingServers(running.Installs, running.Services, "csharp-ls"));
	running.FinishJobs();

	CHECK(running.Factory.Made == 2);
	CHECK(running.State() == Graphite::ServerState::Starting);
	CHECK(std::filesystem::exists(running.Version));
}

TEST_CASE("An install that placed a version restarts the language that runs it and nothing else")
{
	RunningCsharpLs running;

	Graphite::RestartServersAfter(Graphite::InstallProgress{.ServerId = "roslyn", .Step = Graphite::InstallStep::Installed}, running.Services);
	CHECK(running.Factory.Made == 1);

	Graphite::RestartServersAfter(Graphite::InstallProgress{.ServerId = "csharp-ls", .Step = Graphite::InstallStep::UpToDate}, running.Services);
	CHECK(running.Factory.Made == 1);

	Graphite::RestartServersAfter(
		Graphite::InstallProgress{.ServerId = "csharp-ls", .Step = Graphite::InstallStep::UpdateAvailable, .Kind = Graphite::InstallJobKind::CheckForUpdates},
		running.Services);
	CHECK(running.Factory.Made == 1);

	Graphite::RestartServersAfter(Graphite::InstallProgress{.ServerId = "csharp-ls", .Step = Graphite::InstallStep::Installed}, running.Services);
	CHECK(running.Factory.Made == 2);
}

TEST_CASE("The install command is offered from the install record")
{
	TempDirectory temp;
	InstallCsharpLsIn(temp.GetPath() / "servers");
	DeferredJobs jobs;
	Graphite::ServerInstalls installs{Core::CreateRef<FakeHttpClient>(), temp.GetPath() / "servers", jobs.Submitter()};
	Graphite::ServerInstalls offline{nullptr, temp.GetPath() / "servers", jobs.Submitter()};

	CHECK_FALSE(Graphite::MayInstall(installs, *Graphite::FindServer("roslyn")));

	installs.SweepAtStartup();
	offline.SweepAtStartup();
	jobs.RunAll();
	installs.Poll();
	offline.Poll();

	CHECK(Graphite::MayInstall(installs, *Graphite::FindServer("roslyn")));
	CHECK_FALSE(Graphite::MayInstall(installs, *Graphite::FindServer("csharp-ls")));
	CHECK_FALSE(Graphite::MayInstall(offline, *Graphite::FindServer("roslyn")));

	REQUIRE(installs.Install("roslyn"));
	CHECK_FALSE(Graphite::MayInstall(installs, *Graphite::FindServer("roslyn")));
}
