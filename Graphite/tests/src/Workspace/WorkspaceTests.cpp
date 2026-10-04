#include <doctest/doctest.h>

#include "UI/ToastEvent.h"
#include "Workspace/Workspace.h"

#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileSystem.h"

#include <filesystem>
#include <fstream>
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
