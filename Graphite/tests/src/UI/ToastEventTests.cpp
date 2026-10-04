#include <doctest/doctest.h>

#include "UI/ToastEvent.h"

#include <filesystem>

TEST_CASE("A folder toast shows its paths relative to the folder and copies them whole")
{
	const Graphite::ToastEvent toast =
		Graphite::FolderToast(Core::ToastKind::Error, "Rename failed", "Cannot rename 'D:/Repos/IDE/src/a.cpp': denied",
							  std::filesystem::path("D:/Repos/IDE"));

	CHECK(toast.Kind == Core::ToastKind::Error);
	CHECK(toast.Title == "Rename failed");
	CHECK(toast.Message == "Cannot rename 'src/a.cpp': denied");
	CHECK(toast.CopyText == "Rename failed\nCannot rename 'D:/Repos/IDE/src/a.cpp': denied");
	CHECK(toast.Scope == Graphite::ToastScope::Folder);
}

TEST_CASE("A folder toast with no folder open shows its message as it is")
{
	const Graphite::ToastEvent toast =
		Graphite::FolderToast(Core::ToastKind::Warning, "Save skipped", "D:/Repos/a.cpp changed on disk", {});

	CHECK(toast.Message == "D:/Repos/a.cpp changed on disk");
	CHECK(toast.Scope == Graphite::ToastScope::Folder);
}

TEST_CASE("An application toast keeps its message and leaves Copy to the title and the message")
{
	const Graphite::ToastEvent toast =
		Graphite::ApplicationToast(Core::ToastKind::Warning, "No shell found", "No shell was found to open a terminal with");

	CHECK(toast.Kind == Core::ToastKind::Warning);
	CHECK(toast.Title == "No shell found");
	CHECK(toast.Message == "No shell was found to open a terminal with");
	CHECK(toast.CopyText.empty());
	CHECK(toast.Scope == Graphite::ToastScope::Application);
}
