#include "Workspace/WatcherWarning.h"

#include <string>
#include <string_view>

namespace Graphite {

	namespace {

		constexpr std::string_view kNotWatchingTitle = "Not watching for changes";
		constexpr std::string_view kNotWatchingMessage =
			"Graphite is not watching this folder for changes; use Refresh to see changes made outside it.";
	}

	ToastEvent NotWatchingWarning(const std::filesystem::path& root)
	{
		return FolderToast(Core::ToastKind::Warning, std::string{kNotWatchingTitle}, kNotWatchingMessage, root);
	}
}
