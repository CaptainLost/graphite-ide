#include <doctest/doctest.h>

#include "UI/ToastEvent.h"
#include "Workspace/Workspace.h"

#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileSystem.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
	// Every toast the workspace raises, in order.
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

	// No folder is open to clear it with.
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

	// Inside the folder, so it reads relative on screen and whole in Copy.
	CHECK(log.Toasts.front().Message.find(Core::ToUtf8(temp.GetPath())) == std::string::npos);
	CHECK(log.Toasts.front().CopyText.find("Broken.slnx") != std::string::npos);
}

// The application clears the folder's toasts when another folder starts to open. A solution
// failure the new folder reports must come after that, or it is cleared in the same call that
// raised it.
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

	const std::filesystem::path broken = temp.MakeDir("other") / "Broken.slnx";
	std::ofstream(broken) << "<Solution><Project";

	CHECK_FALSE(workspace.OpenSolution(broken));

	CHECK(log.Toasts.size() == 1);
}
