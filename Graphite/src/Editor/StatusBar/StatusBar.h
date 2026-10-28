#pragma once

#include "Editor/StatusBar/ServerHealth.h"
#include "Git/GitStatusText.h"

#include "Core/Base/Color.h"

#include <imgui.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Core {
	class DocumentSet;

}

namespace Graphite {
	inline constexpr float kStatusBarHeight = 23.0f;
	inline constexpr float kStatusBarFontSize = 15.0f;
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

	// One item the view on screen puts on the right of the bar when no text document is shown.
	struct ViewStatusItem
	{
		std::string Text;
		bool Code = false;

		// A square of this colour, over a checkerboard so its alpha shows, before the text.
		std::optional<ImU32> Swatch;

		// Words in the interface face right before the text, whatever face the text takes: `Offset ` before a number in
		// the code face. Empty for none.
		std::string Label;

		bool operator==(const ViewStatusItem&) const = default;
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

		std::optional<std::string> ExplorerFileOperation;

		std::optional<Core::Color> MissedNotificationColour;

		std::vector<ViewStatusItem> View;

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
								 const StatusMessages& messages);

	// The bar's own words at rest.
	ImU32 StatusBarTextColour();

	// The dot before a ready server's words, for the chrome it is drawn on.
	ImU32 ServerReadyColour(const Core::Color& chromeBackground);

}
