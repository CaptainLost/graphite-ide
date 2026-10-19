#pragma once

#include "Editor/StatusBar/ServerHealth.h"
#include "Git/GitStatusText.h"

#include "Core/Base/Color.h"

#include <imgui.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace Core {
	class DocumentSet;

}

namespace Graphite {
	inline constexpr float kStatusItemGap = 16.0f;

	inline constexpr float kServerDotDiameter = 7.0f;

	struct ProblemCounts
	{
		int Errors = 0;
		int Warnings = 0;
	};

	struct StatusBarColours
	{
		ImU32 Error = 0;
		ImU32 Warning = 0;
		ImU32 Ready = 0;
	};

	struct StatusMessages
	{
		std::optional<std::string> Transient;
		std::optional<std::string> Search;

		std::optional<std::string> Server;

		std::optional<std::string> ServerDetail;

		bool ServerOpensSettings = false;

		ServerHealth Health = ServerHealth::None;

		std::optional<ProblemCounts> Problems;

		std::optional<std::string> ProblemsDetail;

		std::optional<std::string> Background;

		std::optional<std::string> BackgroundDetail;

		std::optional<GitStatusSegment> Git;

		std::optional<Core::Color> MissedNotificationColour;

		StatusBarColours Colours;
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
