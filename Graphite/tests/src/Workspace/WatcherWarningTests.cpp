#include <doctest/doctest.h>

#include "Workspace/WatcherWarning.h"
#include "Workspace/Workspace.h"

#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Core/Events/EventBus.h"

#include <filesystem>
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

	Core::Result<Core::Scope<Core::FolderWatch>> FailToWatch(const std::filesystem::path&)
	{
		return Core::Error{Core::ErrorCode::IoFailure, "no watching here"};
	}
}

TEST_CASE("A folder nobody watches is reported once as a warning about the folder that points at Refresh")
{
	const std::filesystem::path root{"D:/Work/proj"};

	const Graphite::ToastEvent warning = Graphite::NotWatchingWarning(root);

	CHECK(warning.Kind == Core::ToastKind::Warning);
	CHECK(warning.Scope == Graphite::ToastScope::Folder);
	CHECK(warning.Title == "Not watching for changes");
	CHECK(warning.Message ==
		  "Graphite is not watching this folder for changes; use Refresh to see changes made outside it.");
}

TEST_CASE("A folder whose watcher cannot start gets one warning toast, however many frames follow")
{
	constexpr int kFramesAfterOpening = 3;

	TempDirectory temp;
	TempAppSettings settings;
	Core::EventBus events;
	ToastLog log(events);
	Graphite::Workspace workspace(events, settings.Get(), FailToWatch);

	REQUIRE(workspace.OpenFolder(temp.GetPath()));

	for (int frameIndex = 0; frameIndex < kFramesAfterOpening; ++frameIndex)
	{
		workspace.Update();
	}

	REQUIRE(log.Toasts.size() == 1);
	CHECK(log.Toasts.front().Title == "Not watching for changes");
	CHECK(log.Toasts.front().Kind == Core::ToastKind::Warning);
	CHECK(log.Toasts.front().Scope == Graphite::ToastScope::Folder);
}
