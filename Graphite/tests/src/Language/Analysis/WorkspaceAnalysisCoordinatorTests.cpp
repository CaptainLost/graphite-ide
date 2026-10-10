#include <doctest/doctest.h>

#include "Language/Analysis/WorkspaceAnalysisCoordinator.h"
#include "Language/Registry/LanguageRegistry.h"

#include "Editor/Overlays/AnalysisCache.h"

#include "TempDirectory.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace Graphite;

namespace {

	// Records what the servers were told rather than starting any.
	class FakeServers final : public WorkspaceServersPort
	{
	public:
		void Revalidate() override { ++Revalidations; }

		void WarmUp(const std::filesystem::path& root, bool) override { WarmedUp.push_back(root); }

		int Revalidations = 0;
		std::vector<std::filesystem::path> WarmedUp;
	};

	// Ready, and silent: what it is asked is not what these cases are about, only that the pass
	// can run against it without a process.
	class SilentPort final : public WorkspaceAnalysisPort
	{
	public:
		void Open(const std::filesystem::path&, LanguageId, std::string) override {}

		LanguageServices::Ticket AskSemanticTokens(const std::filesystem::path&, LanguageId) override
		{
			return static_cast<LanguageServices::Ticket>(++Tickets);
		}

		LanguageServices::Ticket AskSymbols(const std::filesystem::path&, LanguageId) override
		{
			return static_cast<LanguageServices::Ticket>(++Tickets);
		}

		void Close(const std::filesystem::path&, LanguageId) override {}

		bool IsServerReady(const std::filesystem::path&, LanguageId) override { return true; }

		bool HasChosenServer(Graphite::LanguageId language) override { return Graphite::LanguageHasServers(language); }

		std::uint64_t Tickets = 0;
	};

	// Never answers, so a file the pass starts stays outstanding and nothing is written to the
	// store under the temporary folder.
	class SilentTexts final : public FileTextSource
	{
	public:
		void Request(const std::filesystem::path&) override {}
		void Take(std::vector<FileText>&) override {}
	};

	struct Harness
	{
		FakeServers Servers;
		SilentPort Port;
		SilentTexts Texts;
		AnalysisCache Cache{64ull * 1024ull * 1024ull};
		WorkspaceAnalysisCoordinator Coordinator{Servers, Port, Texts, Cache};

		std::filesystem::path Root;
		bool Trusted = true;
		bool SettingEnabled = true;
		std::uint64_t TrustRevision = 0;

		double Now = 0.0;

		// One frame, as GraphiteLayer::OnImGuiRender runs it: the edges, then the pass.
		void Frame()
		{
			Coordinator.Update(AnalysisSwitches{.Root = Root,
												.Trusted = Trusted,
												.SettingEnabled = SettingEnabled,
												.TrustRevision = TrustRevision});

			Now += 0.016;

			Coordinator.Tick(AnalysisTickInputs{
				.Now = Now,
				.ForegroundBusy = false,
				.BudgetBytes = 64ull * 1024ull * 1024ull,
				.Concurrency = 1,
				.TabWidth = 4,
				.HeldElsewhere = [](const std::filesystem::path&) { return false; }});
		}

		// Runs frames until the walk has finished and the pass has queued `expected` files. The
		// walk is a real thread over a real folder, so this is the one wait in these cases.
		bool FramesUntilQueued(std::size_t expected)
		{
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

			while (std::chrono::steady_clock::now() < deadline)
			{
				Frame();

				const AnalysisProgress progress = Coordinator.GetPass().Progress();

				if (!progress.Walking && progress.Total == expected)
				{
					return true;
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}

			return false;
		}
	};
}

TEST_CASE("Revoking trust in the open folder stops the analysis")
{
	TempDirectory folder;
	folder.MakeFile("a.cpp", "int a;");
	folder.MakeFile("b.cpp", "int b;");

	Harness harness;
	harness.Root = folder.GetPath();
	harness.TrustRevision = 1;

	REQUIRE(harness.FramesUntilQueued(2));
	REQUIRE(harness.Coordinator.GetStore().GetRoot() == folder.GetPath());
	REQUIRE(harness.Servers.Revalidations == 1);

	harness.Trusted = false;
	harness.TrustRevision = 2;
	harness.Frame();

	CHECK(harness.Servers.Revalidations == 2);
	CHECK_FALSE(harness.Coordinator.IsEnabled());
	CHECK_FALSE(harness.Coordinator.GetPass().IsEnabled());
	CHECK(harness.Coordinator.GetPass().Progress().Total == 0);
	CHECK(harness.Coordinator.GetStore().GetRoot().empty());

	// Nothing comes back on later frames either: the walk was cancelled, not merely ignored.
	harness.Frame();
	harness.Frame();

	CHECK(harness.Coordinator.GetPass().Progress().Total == 0);
	CHECK_FALSE(harness.Coordinator.GetPass().Progress().Walking);
}

TEST_CASE("Changing the root restarts the analysis for the new folder")
{
	TempDirectory first;
	first.MakeFile("a.cpp", "int a;");

	TempDirectory second;
	second.MakeFile("b.cpp", "int b;");
	second.MakeFile("c.cpp", "int c;");
	second.MakeFile("d.cpp", "int d;");

	Harness harness;
	harness.Root = first.GetPath();

	REQUIRE(harness.FramesUntilQueued(1));

	harness.Root = second.GetPath();

	// Three, not four: the first folder's queue was dropped rather than added to.
	CHECK(harness.FramesUntilQueued(3));
	CHECK(harness.Coordinator.GetStore().GetRoot() == second.GetPath());
	CHECK(harness.Servers.WarmedUp ==
		  std::vector<std::filesystem::path>{first.GetPath(), second.GetPath()});
}

TEST_CASE("Switching the setting back on starts the analysis again")
{
	TempDirectory folder;
	folder.MakeFile("a.cpp", "int a;");

	Harness harness;
	harness.Root = folder.GetPath();

	REQUIRE(harness.FramesUntilQueued(1));

	harness.SettingEnabled = false;
	harness.Frame();

	REQUIRE_FALSE(harness.Coordinator.IsEnabled());
	REQUIRE(harness.Coordinator.GetPass().Progress().Total == 0);
	REQUIRE(harness.Coordinator.GetStore().GetRoot().empty());

	harness.SettingEnabled = true;

	CHECK(harness.FramesUntilQueued(1));
	CHECK(harness.Coordinator.GetStore().GetRoot() == folder.GetPath());

	// The setting is about the analysis, not the servers: they were warmed once, for the folder.
	CHECK(harness.Servers.WarmedUp == std::vector<std::filesystem::path>{folder.GetPath()});
}

TEST_CASE("Trusting the open folder warms its servers and starts the analysis")
{
	TempDirectory folder;
	folder.MakeFile("a.cpp", "int a;");

	Harness harness;
	harness.Root = folder.GetPath();
	harness.Trusted = false;

	harness.Frame();
	harness.Frame();

	REQUIRE(harness.Servers.WarmedUp.empty());
	REQUIRE(harness.Coordinator.GetStore().GetRoot().empty());
	REQUIRE(harness.Coordinator.GetPass().Progress().Total == 0);

	harness.Trusted = true;
	harness.TrustRevision = 1;

	CHECK(harness.FramesUntilQueued(1));
	CHECK(harness.Servers.Revalidations == 1);
	CHECK(harness.Servers.WarmedUp == std::vector<std::filesystem::path>{folder.GetPath()});
	CHECK(harness.Coordinator.GetStore().GetRoot() == folder.GetPath());
}

TEST_CASE("With no folder open nothing is warmed, walked or stored")
{
	// What the layer passes with no folder open: an empty root is never trusted.
	Harness harness;
	harness.Trusted = false;

	harness.Frame();
	harness.Frame();

	CHECK(harness.Servers.WarmedUp.empty());
	CHECK(harness.Servers.Revalidations == 0);
	CHECK(harness.Coordinator.GetStore().GetRoot().empty());
	CHECK(harness.Coordinator.GetPass().Progress().Total == 0);
}
