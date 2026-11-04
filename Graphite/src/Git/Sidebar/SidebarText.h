#pragma once

#include "Git/Lines/BranchLines.h"
#include "Git/Lines/LineReader.h"
#include "Git/Sidebar/SidebarRows.h"

#include "Core/Git/Lines/LineModel.h"

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
}
