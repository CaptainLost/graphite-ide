#include <doctest/doctest.h>

#include "Notifications/NotificationCentre.h"

#include <filesystem>
#include <optional>
#include <vector>

using Graphite::NotificationCentre;
using Graphite::NotificationEntry;
using Graphite::ToastEvent;
using Graphite::ToastScope;

namespace {

	constexpr bool kWithConfirmations = true;

	const std::filesystem::path kFolder = "C:/work/project";

	ToastEvent Toast(Core::ToastKind kind, ToastScope scope, const char* title)
	{
		return ToastEvent{.Kind = kind, .Title = title, .Message = "message", .CopyText = "copied", .Scope = scope};
	}
}

TEST_CASE("Notifications: each toast shown is recorded, under the open folder only when it is about the folder")
{
	NotificationCentre centre;

	centre.Show(Toast(Core::ToastKind::Error, ToastScope::Folder, "Push failed"), kFolder);
	centre.Show(Toast(Core::ToastKind::Warning, ToastScope::Application, "Update ready"), kFolder);

	const std::vector<const NotificationEntry*> entries = centre.GetHistory().Entries(kWithConfirmations);
	REQUIRE(entries.size() == 2);
	CHECK(entries[0]->Title == "Update ready");
	CHECK(entries[0]->Folder.empty());
	CHECK(entries[1]->Title == "Push failed");
	CHECK(entries[1]->Folder == kFolder);
	CHECK(entries[1]->CopyText == "copied");
}

TEST_CASE("Notifications: a folder's error cleared with the folder colours the bell; the application's toasts stay")
{
	NotificationCentre centre;
	centre.Show(Toast(Core::ToastKind::Error, ToastScope::Folder, "Pull failed"), kFolder);
	centre.Show(Toast(Core::ToastKind::Warning, ToastScope::Application, "Update ready"), kFolder);

	CHECK_FALSE(centre.MissedForTheBell().has_value());

	centre.ClearFolder();

	CHECK(centre.MissedForTheBell() == std::optional<Core::ToastKind>{Core::ToastKind::Error});
	CHECK(centre.GetHistory().Entries(kWithConfirmations).size() == 2);
}
