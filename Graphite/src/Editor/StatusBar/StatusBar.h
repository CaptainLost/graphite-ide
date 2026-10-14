#pragma once

#include "Git/GitStatusText.h"

#include "Core/Base/Color.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace Core {
	class DocumentSet;

}

namespace Graphite {
	struct StatusMessages
	{
		std::optional<std::string> Transient;
		std::optional<std::string> Search;

		std::optional<std::string> Server;

		std::optional<std::string> ServerDetail;

		bool ServerOpensSettings = false;

		std::optional<std::string> Problems;

		std::optional<std::string> Background;

		std::optional<std::string> BackgroundDetail;

		std::optional<GitStatusSegment> Git;

		std::optional<Core::Color> MissedNotificationColour;
	};

	constexpr const char* kStatusBarWindowName = "##StatusBar";

	enum class StatusBarClick
	{
		None,
		GitBranch,
		GitChanges,
		Server,
		Notifications
	};

	StatusBarClick DrawStatusBar(const Core::DocumentSet& documents,
								 std::optional<std::size_t> shown,
								 const std::filesystem::path& root,
								 const StatusMessages& messages);

}
