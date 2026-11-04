#pragma once

#include "Git/Lines/BranchLines.h"
#include "Git/Lines/LineReader.h"
#include "Git/Sidebar/SidebarRows.h"

#include "Core/Base/Result.h"
#include "Core/Git/Lines/LineModel.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Stash.h"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	enum class SidebarTone
	{
		Dim,
		Local,   // mockup: amber
		Both,    // blue
		Gone,    // red
		Unusual, // amber
		Merged   // green
	};

	struct SidebarSpan
	{
		std::string Text;
		SidebarTone Tone = SidebarTone::Dim;
	};

	struct SidebarRowText
	{
		std::string Head;              // line 1: last "/" segment of the short name (Branch), the name without its remote (Base)
		bool IsHead = false;           // checked out: HEAD dot before Head, bright
		std::vector<SidebarSpan> Meta; // line 2; the view joins spans with " · "
		std::string Right;             // "2 to push", "3 to pull", "2 to push · 3 to pull", or empty
		std::string Tooltip;
	};

	struct SidebarTextInput
	{
		const LineRead& Read;
		const BranchLines& Lines;
		const std::optional<std::string>& UserEmail;
		std::string_view MostCommonLine; // full name
		std::chrono::sys_seconds Now;
	};

	// Empty for a row that is neither a branch nor a base.
	SidebarRowText DescribeSidebarRow(const SidebarRow& row, const SidebarTextInput& input);

	// The full name of the line most branches are compared with; empty when none is. A tie goes to the earlier ref.
	std::string MostCommonLineOf(const LineRead& read);

	std::string BaseReasonText(const Core::LineBase& base, const LineRead& read);

	// The one row that stands in for the sections that need the line model while there is none.
	struct SidebarWaitingRow
	{
		std::string Text;
		bool Failed = false; // nothing is on its way, so no loader turns beside the text
	};

	struct SidebarWaitInput
	{
		bool HasRead = false;
		bool Reading = false; // a read of the lines is under way
		const std::optional<Core::Error>& LinesError;
		const std::vector<Core::Branch>* Branches; // null while unread
		const std::optional<Core::Error>& BranchesError;
	};

	// Empty once a read of the lines is in hand: a later failure leaves its rows on screen.
	std::optional<SidebarWaitingRow> DescribeSidebarWait(const SidebarWaitInput& input);

	// Line 1 the first line of the message, its age at the right, and the message with when it was made in the tooltip.
	SidebarRowText DescribeStashRow(const Core::StashEntry& stash, std::chrono::sys_seconds now,
									const std::chrono::time_zone* zone);
}
