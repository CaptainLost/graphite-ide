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

	constexpr std::size_t kCacheBudgetBytes = 64ull * 1024ull * 1024ull;
	constexpr double kFrameSeconds = 0.016;
	constexpr std::chrono::seconds kWaitLimit{10};
	constexpr std::chrono::milliseconds kPollInterval{5};
	constexpr std::size_t kTestTabWidth = 4;

	class FakeServers final : public WorkspaceServersPort
	{
	public:
		void Revalidate() override { ++Revalidations; }

		void WarmUp(const std::filesystem::path& root, bool) override { WarmedUp.push_back(root); }

		int Revalidations = 0;
		std::vector<std::filesystem::path> WarmedUp;
	};

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

		ServerReadiness ReadinessOf(const std::filesystem::path&, LanguageId) override { return ServerReadiness::Ready; }

		bool HasChosenServer(Graphite::LanguageId language) override { return Graphite::LanguageHasServers(language); }

		std::uint64_t Tickets = 0;
	};

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
		AnalysisCache Cache{kCacheBudgetBytes};
		WorkspaceAnalysisCoordinator Coordinator{Servers, Port, Texts, Cache};

		std::filesystem::path Root;
		std::filesystem::path SelectedSolution;
		bool Trusted = true;
		bool SettingEnabled = true;
		std::uint64_t TrustRevision = 0;

		double Now = 0.0;

		void Frame()
		{
			Coordinator.Update(AnalysisSwitches{.Root = Root,
												.SelectedSolution = SelectedSolution,
												.Trusted = Trusted,
												.SettingEnabled = SettingEnabled,
												.TrustRevision = TrustRevision});

			Now += kFrameSeconds;

			Coordinator.Tick(AnalysisTickInputs{
				.Now = Now,
				.ForegroundBusy = false,
				.BudgetBytes = kCacheBudgetBytes,
				.Concurrency = 1,
				.TabWidth = kTestTabWidth,
				.HeldElsewhere = [](const std::filesystem::path&) { return false; }});
		}

		bool FramesUntilQueued(std::size_t expected)
		{
			const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kWaitLimit;

			while (std::chrono::steady_clock::now() < deadline)
			{
				Frame();

				const AnalysisProgress progress = Coordinator.GetPass().Progress();

				if (!progress.Walking && progress.Total == expected)
				{
					return true;
				}

				std::this_thread::sleep_for(kPollInterval);
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

	harness.Frame();
	harness.Frame();

	CHECK(harness.Coordinator.GetPass().Progress().Total == 0);
	CHECK_FALSE(harness.Coordinator.GetPass().Progress().Walking);
}

TEST_CASE("Changing the root restarts the analysis for the new folder")
{
	TempDirectory firstFolder;
	firstFolder.MakeFile("a.cpp", "int a;");

	TempDirectory secondFolder;
	secondFolder.MakeFile("b.cpp", "int b;");
	secondFolder.MakeFile("c.cpp", "int c;");
	secondFolder.MakeFile("d.cpp", "int d;");

	Harness harness;
	harness.Root = firstFolder.GetPath();

	REQUIRE(harness.FramesUntilQueued(1));

	harness.Root = secondFolder.GetPath();

	CHECK(harness.FramesUntilQueued(3));
	CHECK(harness.Coordinator.GetStore().GetRoot() == secondFolder.GetPath());
	CHECK(harness.Servers.WarmedUp ==
		  std::vector<std::filesystem::path>{firstFolder.GetPath(), secondFolder.GetPath()});
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
	Harness harness;
	harness.Trusted = false;

	harness.Frame();
	harness.Frame();

	CHECK(harness.Servers.WarmedUp.empty());
	CHECK(harness.Servers.Revalidations == 0);
	CHECK(harness.Coordinator.GetStore().GetRoot().empty());
	CHECK(harness.Coordinator.GetPass().Progress().Total == 0);
}

TEST_CASE("Selecting another solution walks the folder again for it")
{
	TempDirectory folder;
	folder.MakeFile("main.cpp", "int a;");
	folder.MakeFile("app/App.slnx", "");
	folder.MakeFile("app/Program.cs", "class Program { }");
	folder.MakeFile("tool/Tool.slnx", "");
	folder.MakeFile("tool/Tool.cs", "class Tool { }");
	folder.MakeFile("tool/Helper.cs", "class Helper { }");

	Harness harness;
	harness.Root = folder.GetPath();
	harness.SelectedSolution = folder.GetPath() / "app" / "App.slnx";

	REQUIRE(harness.FramesUntilQueued(2));

	harness.SelectedSolution = folder.GetPath() / "tool" / "Tool.slnx";

	CHECK(harness.FramesUntilQueued(3));
	CHECK(harness.Servers.WarmedUp == std::vector<std::filesystem::path>{folder.GetPath()});
}

namespace {

	void MakeTwoSolutions(TempDirectory& folder)
	{
		folder.MakeFile("main.cpp", "int a;");
		folder.MakeFile("app/App.slnx", "");
		folder.MakeFile("app/Program.cs", "class Program { }");
		folder.MakeFile("tool/Tool.slnx", "");
		folder.MakeFile("tool/Tool.cs", "class Tool { }");
		folder.MakeFile("tool/Helper.cs", "class Helper { }");
		folder.MakeFile("tool/Other.cs", "class Other { }");
	}

	void RunFramesAndExpectTotal(Harness& harness, std::size_t expected)
	{
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kWaitLimit;

		harness.Frame();

		while (harness.Coordinator.GetPass().Progress().Walking && std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(kPollInterval);
			harness.Frame();
		}

		constexpr int kSettlingFrames = 5;

		for (int frame = 0; frame < kSettlingFrames; ++frame)
		{
			std::this_thread::sleep_for(kPollInterval);
			harness.Frame();
		}

		CHECK(harness.Coordinator.GetPass().Progress().Total == expected);
		CHECK_FALSE(harness.Coordinator.GetPass().Progress().Walking);
	}
}

TEST_CASE("A solution selected while the analysis is off is the one walked when it comes back on")
{
	TempDirectory folder;
	MakeTwoSolutions(folder);

	Harness harness;
	harness.Root = folder.GetPath();
	harness.SelectedSolution = folder.GetPath() / "app" / "App.slnx";

	REQUIRE(harness.FramesUntilQueued(2));

	harness.SettingEnabled = false;
	harness.Frame();
	harness.SelectedSolution = folder.GetPath() / "tool" / "Tool.slnx";
	harness.Frame();
	harness.Frame();

	REQUIRE(harness.Coordinator.GetPass().Progress().Total == 0);

	harness.SettingEnabled = true;

	CHECK(harness.FramesUntilQueued(4));
	RunFramesAndExpectTotal(harness, 4);
}

TEST_CASE("A solution selected in the frame the analysis goes off queues nothing")
{
	TempDirectory folder;
	MakeTwoSolutions(folder);

	Harness harness;
	harness.Root = folder.GetPath();
	harness.SelectedSolution = folder.GetPath() / "app" / "App.slnx";

	REQUIRE(harness.FramesUntilQueued(2));

	harness.SettingEnabled = false;
	harness.SelectedSolution = folder.GetPath() / "tool" / "Tool.slnx";
	harness.Frame();

	CHECK(harness.Coordinator.GetStore().GetRoot().empty());
	RunFramesAndExpectTotal(harness, 0);
	CHECK(harness.Coordinator.GetStore().GetRoot().empty());
}

TEST_CASE("A solution selected in the frame the root changes is walked once for the new root")
{
	TempDirectory firstFolder;
	firstFolder.MakeFile("a.cpp", "int a;");

	TempDirectory secondFolder;
	MakeTwoSolutions(secondFolder);

	Harness harness;
	harness.Root = firstFolder.GetPath();

	REQUIRE(harness.FramesUntilQueued(1));

	harness.Root = secondFolder.GetPath();
	harness.SelectedSolution = secondFolder.GetPath() / "tool" / "Tool.slnx";

	CHECK(harness.FramesUntilQueued(4));
	RunFramesAndExpectTotal(harness, 4);
}

TEST_CASE("A solution selected in the frame the analysis comes on is walked once")
{
	TempDirectory folder;
	MakeTwoSolutions(folder);

	Harness harness;
	harness.Root = folder.GetPath();
	harness.SelectedSolution = folder.GetPath() / "app" / "App.slnx";
	harness.SettingEnabled = false;
	harness.Frame();

	harness.SettingEnabled = true;
	harness.SelectedSolution = folder.GetPath() / "tool" / "Tool.slnx";

	CHECK(harness.FramesUntilQueued(4));
	RunFramesAndExpectTotal(harness, 4);
	CHECK(harness.Coordinator.GetStore().GetRoot() == folder.GetPath());
}
