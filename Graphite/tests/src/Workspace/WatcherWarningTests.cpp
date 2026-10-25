#include <doctest/doctest.h>

#include "Workspace/WatcherWarning.h"

#include <filesystem>

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
