#include <doctest/doctest.h>

#include "UI/ToastEvent.h"
#include "Workspace/Workspace.h"

#include "DeferredJobs.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/FileSystem/FileTree.h"
#include "Core/Git/Repository.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {
	struct ToastLog
	{
		explicit ToastLog(Core::EventBus& events)
		{
			events.Subscribe<Graphite::ToastEvent>([this](const Graphite::ToastEvent& event) { Toasts.push_back(event); });
		}

		std::vector<Graphite::ToastEvent> Toasts;
	};
}

TEST_CASE("A folder that cannot be opened is reported in an application toast")
{
	TempDirectory temp;
	TempAppSettings settings;
	Core::EventBus events;
	ToastLog log(events);
	Graphite::Workspace workspace(events, settings.Get());

	CHECK_FALSE(workspace.OpenFolder(temp.GetPath() / "missing"));

	REQUIRE(log.Toasts.size() == 1);
	CHECK(log.Toasts.front().Kind == Core::ToastKind::Error);
	CHECK(log.Toasts.front().Title == "Cannot open folder");
	CHECK_FALSE(log.Toasts.front().Message.empty());
	CHECK(log.Toasts.front().Scope == Graphite::ToastScope::Application);
}

TEST_CASE("A file that is not a solution is refused with one application toast")
{
	TempDirectory temp;
	TempAppSettings settings;
	Core::EventBus events;
	ToastLog log(events);
	Graphite::Workspace workspace(events, settings.Get());

	const std::filesystem::path notes = temp.GetPath() / "notes.txt";
	std::ofstream(notes) << "hello";

	CHECK_FALSE(workspace.OpenSolution(notes));

	REQUIRE(log.Toasts.size() == 1);
	CHECK(log.Toasts.front().Kind == Core::ToastKind::Error);
	CHECK(log.Toasts.front().Title == "Cannot open solution");
	CHECK(log.Toasts.front().Scope == Graphite::ToastScope::Application);
}

TEST_CASE("A solution in the open folder that cannot be read is reported once, about the folder")
{
	TempDirectory temp;
	TempAppSettings settings;
	Core::EventBus events;
	ToastLog log(events);
	Graphite::Workspace workspace(events, settings.Get());

	std::ofstream(temp.GetPath() / "Broken.slnx") << "<Solution><Project";

	REQUIRE(workspace.OpenFolder(temp.GetPath()));

	REQUIRE(log.Toasts.size() == 1);
	CHECK(log.Toasts.front().Kind == Core::ToastKind::Error);
	CHECK(log.Toasts.front().Title == "Cannot open solution");
	CHECK(log.Toasts.front().Scope == Graphite::ToastScope::Folder);

	CHECK(log.Toasts.front().Message.find(Core::ToUtf8(temp.GetPath())) == std::string::npos);
	CHECK(log.Toasts.front().CopyText.find("Broken.slnx") != std::string::npos);
}

TEST_CASE("A folder's solution failure is reported after the folder starts opening, not before")
{
	TempDirectory temp;
	TempAppSettings settings;
	Core::EventBus events;
	Graphite::Workspace workspace(events, settings.Get());

	std::vector<std::string> order;

	events.Subscribe<Graphite::WorkspaceOpeningEvent>(
		[&](const Graphite::WorkspaceOpeningEvent&) { order.push_back("opening"); });
	events.Subscribe<Graphite::ToastEvent>([&](const Graphite::ToastEvent& event) { order.push_back(event.Title); });
	events.Subscribe<Graphite::WorkspaceOpenedEvent>(
		[&](const Graphite::WorkspaceOpenedEvent&) { order.push_back("opened"); });

	std::ofstream(temp.GetPath() / "Broken.slnx") << "<Solution><Project";

	REQUIRE(workspace.OpenFolder(temp.GetPath()));

	CHECK(order == std::vector<std::string>{"opening", "Cannot open solution", "opened"});
}

TEST_CASE("A folder that cannot be opened leaves the open folder's toasts alone")
{
	TempDirectory temp;
	TempAppSettings settings;
	Core::EventBus events;
	Graphite::Workspace workspace(events, settings.Get());

	int opening = 0;
	events.Subscribe<Graphite::WorkspaceOpeningEvent>([&](const Graphite::WorkspaceOpeningEvent&) { ++opening; });

	REQUIRE(workspace.OpenFolder(temp.GetPath()));
	CHECK(opening == 1);

	CHECK_FALSE(workspace.OpenFolder(temp.GetPath() / "missing"));
	CHECK(opening == 1);
}

TEST_CASE("Picking an unreadable solution again reports it again")
{
	TempDirectory temp;
	TempAppSettings settings;
	Core::EventBus events;
	ToastLog log(events);
	Graphite::Workspace workspace(events, settings.Get());

	const std::filesystem::path broken = temp.GetPath() / "Broken.slnx";
	std::ofstream(broken) << "<Solution><Project";

	REQUIRE(workspace.OpenFolder(temp.GetPath()));
	REQUIRE(log.Toasts.size() == 1);

	CHECK_FALSE(workspace.OpenSolution(broken));
	CHECK_FALSE(workspace.OpenSolution(broken));

	REQUIRE(log.Toasts.size() == 3);
	CHECK(log.Toasts[1].Title == "Cannot open solution");
	CHECK(log.Toasts[2].Message == log.Toasts[1].Message);
}

TEST_CASE("Opening an unreadable solution from outside the open folder reports it once")
{
	TempDirectory temp;
	TempAppSettings settings;
	Core::EventBus events;
	ToastLog log(events);
	Graphite::Workspace workspace(events, settings.Get());

	const std::filesystem::path broken = temp.MakeDirectory("other") / "Broken.slnx";
	std::ofstream(broken) << "<Solution><Project";

	CHECK_FALSE(workspace.OpenSolution(broken));

	CHECK(log.Toasts.size() == 1);
}

TEST_CASE("A solution whose extension is written in capitals is found in the folder")
{
	TempDirectory temp;
	TempAppSettings settings;
	Core::EventBus events;
	Graphite::Workspace workspace(events, settings.Get());

	const std::filesystem::path solution = temp.MakeFile("Demo.SLN", "");

	REQUIRE(workspace.OpenFolder(temp.GetPath()));

	CHECK(workspace.GetSolutionCandidates() == std::vector<std::filesystem::path>{solution});
}

TEST_CASE("A folder's solutions are listed by name whatever its case, so the first is the one the language server picks")
{
	TempDirectory temp;
	TempAppSettings settings;
	Core::EventBus events;
	Graphite::Workspace workspace(events, settings.Get());

	const std::filesystem::path later = temp.MakeFile("Beta.slnx", "");
	const std::filesystem::path first = temp.MakeFile("alpha.sln", "");

	REQUIRE(workspace.OpenFolder(temp.GetPath()));

	CHECK(workspace.GetSolutionCandidates() == std::vector<std::filesystem::path>{first, later});
}

namespace {
	const Core::FileNode* RootChildNamed(const Graphite::Workspace& workspace, const std::string& name)
	{
		for (const Core::FileNode& child : workspace.GetTree().GetRoot().Children)
		{
			if (child.Name == name)
			{
				return &child;
			}
		}

		return nullptr;
	}

	bool IsRootChildIgnored(const Graphite::Workspace& workspace, const std::string& name)
	{
		const Core::FileNode* child = RootChildNamed(workspace, name);
		REQUIRE(child != nullptr);
		return child->Ignored;
	}

	// WARNING: libgit2 tells a rewritten rules file by its size and its write time in whole seconds, so rules of the
	// same size written within the second read as unchanged.
	constexpr const char* kRulesOfAnotherSize = "notes.txt\n";

	void WriteRules(const std::filesystem::path& folder, const std::string& rules)
	{
		std::ofstream(folder / ".gitignore", std::ios::binary | std::ios::trunc) << rules;
	}

	struct IgnoreRulesRepository
	{
		IgnoreRulesRepository()
		{
			REQUIRE(Core::Repository::Init(Temp.GetPath()));
			WriteRules(Temp.GetPath(), "*.log\n");
			std::ofstream(Temp.GetPath() / "build.log") << "x";
			std::ofstream(Temp.GetPath() / "notes.txt") << "x";
		}

		TempDirectory Temp;
	};
}

TEST_CASE("A reload nobody asked for keeps the ignore answers it has, and an explicit refresh asks again")
{
	IgnoreRulesRepository repository;
	TempAppSettings settings;
	Core::EventBus events;
	Graphite::Workspace workspace(events, settings.Get());

	REQUIRE(workspace.OpenFolder(repository.Temp.GetPath()));
	REQUIRE(IsRootChildIgnored(workspace, "build.log"));
	REQUIRE_FALSE(IsRootChildIgnored(workspace, "notes.txt"));

	WriteRules(repository.Temp.GetPath(), kRulesOfAnotherSize);

	workspace.Reload(Core::IgnoreAnswers::Keep);

	CHECK(IsRootChildIgnored(workspace, "build.log"));
	CHECK_FALSE(IsRootChildIgnored(workspace, "notes.txt"));

	workspace.Reload(Core::IgnoreAnswers::AskAgain);

	CHECK_FALSE(IsRootChildIgnored(workspace, "build.log"));
	CHECK(IsRootChildIgnored(workspace, "notes.txt"));
}

TEST_CASE("A rules file changed on disk makes the tree ask again about every entry once the watcher reports it")
{
	IgnoreRulesRepository repository;
	TempAppSettings settings;
	Core::EventBus events;
	Graphite::Workspace workspace(events, settings.Get());

	REQUIRE(workspace.OpenFolder(repository.Temp.GetPath()));
	REQUIRE_FALSE(IsRootChildIgnored(workspace, "notes.txt"));

	WriteRules(repository.Temp.GetPath(), kRulesOfAnotherSize);

	constexpr std::chrono::seconds kWatcherTimeout{5};
	constexpr std::chrono::milliseconds kPollInterval{20};
	const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kWatcherTimeout;

	while (std::chrono::steady_clock::now() < deadline && !IsRootChildIgnored(workspace, "notes.txt"))
	{
		workspace.Update();
		std::this_thread::sleep_for(kPollInterval);
	}

	CHECK(IsRootChildIgnored(workspace, "notes.txt"));
	CHECK_FALSE(IsRootChildIgnored(workspace, "build.log"));
}

namespace {
	constexpr const char* kProject = R"(<Project Sdk="Microsoft.NET.Sdk"></Project>)";

	constexpr const char* kOneProjectSolution = R"(<Solution>
  <Project Path="Tools\Tools.csproj" />
</Solution>
)";

	constexpr const char* kTwoProjectSolution = R"(<Solution>
  <Project Path="Tools\Tools.csproj" />
  <Project Path="Engine\Engine.csproj" />
</Solution>
)";

	std::filesystem::path MakeSolution(TempDirectory& folder, const std::string& name, const char* text)
	{
		folder.MakeFile("Tools/Tools.csproj", kProject);
		folder.MakeFile("Engine/Engine.csproj", kProject);
		return folder.MakeFile(name, text);
	}

	struct SolutionWorkspace
	{
		explicit SolutionWorkspace(DeferredJobs& jobs)
			: Workspace(Events, Settings.Get(), Core::DirectoryWatcher::Start, jobs.Submitter())
		{
		}

		TempAppSettings Settings;
		Core::EventBus Events;
		ToastLog Log{Events};
		Graphite::Workspace Workspace;
	};
}

TEST_CASE("A folder's solution is read on a worker, and the workspace says it is reading until the read lands")
{
	TempDirectory folder;
	MakeSolution(folder, "App.slnx", kOneProjectSolution);

	DeferredJobs jobs;
	SolutionWorkspace opened{jobs};

	REQUIRE(opened.Workspace.OpenFolder(folder.GetPath()));

	CHECK(opened.Workspace.IsReadingSolution());
	CHECK_FALSE(opened.Workspace.GetLoadedSolution().has_value());
	CHECK(jobs.Count() == 1);

	jobs.RunAll();
	opened.Workspace.Update();

	CHECK_FALSE(opened.Workspace.IsReadingSolution());
	REQUIRE(opened.Workspace.GetLoadedSolution().has_value());
	CHECK(opened.Workspace.GetLoadedSolution()->ProjectCount == 1);
	CHECK(opened.Log.Toasts.empty());
}

TEST_CASE("Reading the shown solution again keeps it until the new read lands")
{
	TempDirectory folder;
	const std::filesystem::path solution = MakeSolution(folder, "App.slnx", kOneProjectSolution);

	DeferredJobs jobs;
	SolutionWorkspace opened{jobs};

	REQUIRE(opened.Workspace.OpenFolder(folder.GetPath()));
	jobs.RunAll();
	opened.Workspace.Update();
	REQUIRE(opened.Workspace.GetLoadedSolution().has_value());
	const std::uint64_t loadBefore = opened.Workspace.GetSolutionLoad();

	std::ofstream(solution, std::ios::binary | std::ios::trunc) << kTwoProjectSolution;
	opened.Workspace.ReloadSolution();

	CHECK(opened.Workspace.IsReadingSolution());
	REQUIRE(opened.Workspace.GetLoadedSolution().has_value());
	CHECK(opened.Workspace.GetLoadedSolution()->ProjectCount == 1);
	CHECK(opened.Workspace.GetSolutionLoad() == loadBefore);

	jobs.RunAll();
	opened.Workspace.Update();

	CHECK_FALSE(opened.Workspace.IsReadingSolution());
	REQUIRE(opened.Workspace.GetLoadedSolution().has_value());
	CHECK(opened.Workspace.GetLoadedSolution()->ProjectCount == 2);
	CHECK(opened.Workspace.GetSolutionLoad() != loadBefore);
}

TEST_CASE("A read that a newer one replaced is dropped when it lands")
{
	TempDirectory folder;
	const std::filesystem::path first = MakeSolution(folder, "Alpha.slnx", kOneProjectSolution);
	const std::filesystem::path second = folder.MakeFile("Beta.slnx", kTwoProjectSolution);

	DeferredJobs jobs;
	SolutionWorkspace opened{jobs};

	REQUIRE(opened.Workspace.OpenFolder(folder.GetPath()));
	REQUIRE(opened.Workspace.GetSelectedSolution() == first);

	opened.Workspace.SelectSolution(second);
	CHECK(jobs.Count() == 2);

	jobs.RunAll();
	opened.Workspace.Update();

	CHECK_FALSE(opened.Workspace.IsReadingSolution());
	REQUIRE(opened.Workspace.GetLoadedSolution().has_value());
	CHECK(opened.Workspace.GetLoadedSolution()->Path == second);
	CHECK(opened.Workspace.GetLoadedSolution()->ProjectCount == 2);
}

TEST_CASE("A read of a folder closed before it lands is dropped, and an unreadable one says nothing then")
{
	TempDirectory folder;
	folder.MakeFile("Broken.slnx", "<Solution><Project");

	DeferredJobs jobs;
	SolutionWorkspace opened{jobs};

	REQUIRE(opened.Workspace.OpenFolder(folder.GetPath()));
	REQUIRE(opened.Workspace.IsReadingSolution());

	opened.Workspace.Close();
	jobs.RunAll();
	opened.Workspace.Update();

	CHECK_FALSE(opened.Workspace.IsReadingSolution());
	CHECK_FALSE(opened.Workspace.GetLoadedSolution().has_value());
	CHECK(opened.Log.Toasts.empty());
}

TEST_CASE("Opening a solution while it is read is not a failure, and the failure is reported once the read says so")
{
	TempDirectory folder;
	const std::filesystem::path broken = folder.MakeFile("Broken.slnx", "<Solution><Project");

	DeferredJobs jobs;
	SolutionWorkspace opened{jobs};

	CHECK(opened.Workspace.OpenSolution(broken));
	CHECK(opened.Log.Toasts.empty());

	jobs.RunAll();
	opened.Workspace.Update();

	REQUIRE(opened.Log.Toasts.size() == 1);
	CHECK(opened.Log.Toasts.front().Title == "Cannot open solution");
	CHECK_FALSE(opened.Workspace.GetLoadedSolution().has_value());
}

TEST_CASE("An .slnx hides the .sln of the same name in the folder whatever the case of the name")
{
	TempDirectory temp;
	TempAppSettings settings;
	Core::EventBus events;
	Graphite::Workspace workspace(events, settings.Get());

	const std::filesystem::path newer = temp.MakeFile("Demo.slnx", "");
	temp.MakeFile("demo.sln", "");

	REQUIRE(workspace.OpenFolder(temp.GetPath()));

	CHECK(workspace.GetSolutionCandidates() == std::vector<std::filesystem::path>{newer});
}
