#include <doctest/doctest.h>

#include "UI/ToastEvent.h"
#include "Workspace/Workspace.h"

#include "DeferredJobs.h"
#include "Review/ReviewRepository.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/FileSystem/FileTree.h"
#include "Core/Git/Repository.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
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

TEST_CASE("A recent folder's command is labelled by its path in the menu and as Open Recent where commands are listed")
{
	TempDirectory temp;
	TempAppSettings settings;
	Core::EventBus events;
	settings.Get().NoteRecentFolder(temp.GetPath());
	Graphite::Workspace workspace(events, settings.Get());
	Core::CommandRegistry commands;

	workspace.RegisterCommands(commands);

	const Core::Command* recent = commands.Find("workspace.openRecent.0");
	REQUIRE(recent != nullptr);
	CHECK(recent->Label == Core::ToUtf8(temp.GetPath()));
	CHECK(recent->ListLabel == "Open Recent: " + Core::ToUtf8(temp.GetPath()));

	const Core::Command* unused = commands.Find("workspace.openRecent.1");
	REQUIRE(unused != nullptr);
	CHECK(unused->ListLabel.empty());
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

	// WARNING: libgit2 tells a rewritten rules file by its size and its write time in whole seconds.
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
		explicit SolutionWorkspace(DeferredJobs& jobs, Graphite::SolutionReading readSolution = Graphite::ReadSolution)
			: Workspace(Events, Settings.Get(), Core::DirectoryWatcher::Start, jobs.Submitter(), std::move(readSolution))
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

namespace {
	struct ReaderThatThrows
	{
		int* Calls = nullptr;
		int Throws = 0;

		Core::Result<Graphite::Solution> operator()(const std::filesystem::path& solution) const
		{
			++*Calls;

			if (*Calls <= Throws)
			{
				throw std::runtime_error("the disk went away");
			}

			return Graphite::ReadSolution(solution);
		}
	};

	constexpr int kAlways = 1000;
}

TEST_CASE("A solution read that throws once is read again at once, and says nothing when the second read succeeds")
{
	TempDirectory folder;
	MakeSolution(folder, "App.slnx", kOneProjectSolution);

	DeferredJobs jobs;
	int readerCalls = 0;
	SolutionWorkspace opened{jobs, ReaderThatThrows{.Calls = &readerCalls, .Throws = 1}};

	REQUIRE(opened.Workspace.OpenFolder(folder.GetPath()));
	jobs.RunAll();
	opened.Workspace.Update();

	CHECK(opened.Workspace.IsReadingSolution());
	REQUIRE(jobs.Count() == 1);

	jobs.RunAll();
	opened.Workspace.Update();

	REQUIRE(opened.Workspace.GetLoadedSolution().has_value());
	CHECK_FALSE(opened.Workspace.GetSolutionReadFailure().has_value());
	CHECK(opened.Log.Toasts.empty());
	CHECK(readerCalls == 2);
}

TEST_CASE("A solution read that throws every time is reported once, then waits for the solution's files to change")
{
	TempDirectory folder;
	const std::filesystem::path solution = MakeSolution(folder, "App.slnx", kOneProjectSolution);

	DeferredJobs jobs;
	int readerCalls = 0;
	SolutionWorkspace opened{jobs, ReaderThatThrows{.Calls = &readerCalls, .Throws = kAlways}};

	REQUIRE(opened.Workspace.OpenFolder(folder.GetPath()));
	jobs.RunAll();
	opened.Workspace.Update();
	jobs.RunAll();
	opened.Workspace.Update();

	CHECK_FALSE(opened.Workspace.IsReadingSolution());
	REQUIRE(opened.Workspace.GetSolutionReadFailure().has_value());
	CHECK(opened.Workspace.GetSolutionReadFailure()->find("the disk went away") != std::string::npos);
	CHECK(opened.Log.Toasts.size() == 1);
	CHECK(readerCalls == 2);

	opened.Workspace.Reload(Core::IgnoreAnswers::Keep);
	opened.Workspace.Reload(Core::IgnoreAnswers::Keep);

	CHECK(jobs.Count() == 0);
	CHECK(opened.Log.Toasts.size() == 1);

	std::ofstream(solution, std::ios::binary | std::ios::trunc) << kTwoProjectSolution;
	opened.Workspace.Reload(Core::IgnoreAnswers::Keep);

	CHECK(jobs.Count() == 1);
	jobs.RunAll();
	opened.Workspace.Update();
}

TEST_CASE("A rules file appearing above an open subfolder is asked about on the next reload")
{
	TempDirectory temp;
	REQUIRE(Core::Repository::Init(temp.GetPath()));
	const std::filesystem::path subfolder = temp.MakeDirectory("sub");
	std::ofstream(subfolder / "notes.txt") << "x";

	TempAppSettings settings;
	Core::EventBus events;
	Graphite::Workspace workspace(events, settings.Get());

	REQUIRE(workspace.OpenFolder(subfolder));
	REQUIRE_FALSE(IsRootChildIgnored(workspace, "notes.txt"));

	WriteRules(temp.GetPath(), kRulesOfAnotherSize);
	workspace.Reload(Core::IgnoreAnswers::Keep);

	CHECK(IsRootChildIgnored(workspace, "notes.txt"));
}

TEST_CASE("A rules file disappearing above an open subfolder is asked about on the next reload")
{
	IgnoreRulesRepository repository;
	const std::filesystem::path subfolder = repository.Temp.MakeDirectory("sub");
	std::ofstream(subfolder / "build.log") << "x";

	TempAppSettings settings;
	Core::EventBus events;
	Graphite::Workspace workspace(events, settings.Get());

	REQUIRE(workspace.OpenFolder(subfolder));
	REQUIRE(IsRootChildIgnored(workspace, "build.log"));

	std::filesystem::remove(repository.Temp.GetPath() / ".gitignore");
	workspace.Reload(Core::IgnoreAnswers::Keep);

	CHECK_FALSE(IsRootChildIgnored(workspace, "build.log"));
}

TEST_CASE("A rules file above an open subfolder, which the watcher cannot see, is asked about again on the next reload")
{
	IgnoreRulesRepository repository;
	const std::filesystem::path subfolder = repository.Temp.MakeDirectory("sub");
	std::ofstream(subfolder / "build.log") << "x";
	std::ofstream(subfolder / "notes.txt") << "x";

	TempAppSettings settings;
	Core::EventBus events;
	Graphite::Workspace workspace(events, settings.Get());

	REQUIRE(workspace.OpenFolder(subfolder));
	REQUIRE(IsRootChildIgnored(workspace, "build.log"));
	REQUIRE_FALSE(IsRootChildIgnored(workspace, "notes.txt"));

	WriteRules(repository.Temp.GetPath(), kRulesOfAnotherSize);
	workspace.Reload(Core::IgnoreAnswers::Keep);

	CHECK_FALSE(IsRootChildIgnored(workspace, "build.log"));
	CHECK(IsRootChildIgnored(workspace, "notes.txt"));
}

TEST_CASE("A linked worktree's exclude file, kept in the repository it belongs to, is asked about again on the next reload")
{
	ReviewRepository repository;
	repository.Write("build.log", "x");
	repository.Write("notes.txt", "x");
	repository.Commit("one");
	repository.Branch("side");
	const std::filesystem::path worktree = repository.AddWorktree("side");
	const std::filesystem::path commonExclude = repository.GetPath() / ".git" / "info" / "exclude";
	ReviewRepository::WriteFileAt(commonExclude, "*.log\n");

	TempAppSettings settings;
	Core::EventBus events;
	Graphite::Workspace workspace(events, settings.Get());

	REQUIRE(workspace.OpenFolder(worktree));
	REQUIRE(IsRootChildIgnored(workspace, "build.log"));
	REQUIRE_FALSE(IsRootChildIgnored(workspace, "notes.txt"));

	ReviewRepository::WriteFileAt(commonExclude, kRulesOfAnotherSize);
	workspace.Reload(Core::IgnoreAnswers::Keep);

	CHECK_FALSE(IsRootChildIgnored(workspace, "build.log"));
	CHECK(IsRootChildIgnored(workspace, "notes.txt"));
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

TEST_CASE("A folder asked for goes through the replace gate and opens only when the gate runs it")
{
	TempDirectory temp;
	TempAppSettings settings;
	Core::EventBus events;
	Graphite::Workspace workspace(events, settings.Get());

	const std::filesystem::path first = temp.MakeDirectory("first");
	const std::filesystem::path second = temp.MakeDirectory("second");
	REQUIRE(workspace.OpenFolder(first));

	std::vector<std::function<void()>> held;
	workspace.SetReplaceGate([&held](std::function<void()> open) { held.push_back(std::move(open)); });

	workspace.RequestOpenFolder(second);
	REQUIRE(held.size() == 1);
	CHECK(Core::SamePath(workspace.GetRoot(), first));

	held.front()();
	CHECK(Core::SamePath(workspace.GetRoot(), second));
}

TEST_CASE("A folder the gate never runs leaves the open folder as it was")
{
	TempDirectory temp;
	TempAppSettings settings;
	Core::EventBus events;
	Graphite::Workspace workspace(events, settings.Get());

	const std::filesystem::path first = temp.MakeDirectory("first");
	REQUIRE(workspace.OpenFolder(first));

	workspace.SetReplaceGate([](std::function<void()>) {});
	workspace.RequestOpenFolder(temp.MakeDirectory("second"));

	CHECK(Core::SamePath(workspace.GetRoot(), first));
}

TEST_CASE("A solution outside the open folder goes through the replace gate and one inside it does not")
{
	TempDirectory temp;
	TempAppSettings settings;
	Core::EventBus events;
	Graphite::Workspace workspace(events, settings.Get());

	const std::filesystem::path first = temp.MakeDirectory("first");
	const std::filesystem::path inside = temp.MakeFile("first/Inside.slnx", "<Solution />");
	const std::filesystem::path outside = temp.MakeFile("second/Outside.slnx", "<Solution />");
	REQUIRE(workspace.OpenFolder(first));

	int gated = 0;
	workspace.SetReplaceGate([&gated](std::function<void()> open) {
		++gated;
		open();
	});

	workspace.RequestOpenSolution(inside);
	CHECK(gated == 0);
	CHECK(Core::SamePath(workspace.GetRoot(), first));

	workspace.RequestOpenSolution(outside);
	CHECK(gated == 1);
	CHECK(Core::SamePath(workspace.GetRoot(), outside.parent_path()));
}

TEST_CASE("Opening a folder whose state directory has no .gitignore gives it one, and a folder without one gets none")
{
	TempDirectory temp;
	TempAppSettings settings;
	Core::EventBus events;
	Graphite::Workspace workspace(events, settings.Get());

	const std::filesystem::path older = temp.MakeDirectory("older");
	temp.MakeFile("older/.graphite/workspace.json", "{}");
	const std::filesystem::path fresh = temp.MakeDirectory("fresh");

	REQUIRE(workspace.OpenFolder(older));
	CHECK(std::filesystem::exists(older / ".graphite" / ".gitignore"));

	REQUIRE(workspace.OpenFolder(fresh));
	CHECK_FALSE(std::filesystem::exists(fresh / ".graphite"));
}
