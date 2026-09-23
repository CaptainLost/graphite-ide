#pragma once

#include "Sessions/ToolFacts.h"
#include "Theme/ColorScheme.h"

#include <imgui.h>

namespace Graphite {

	struct SessionPalette
	{
		ImU32 Read = 0;
		ImU32 Edit = 0;
		ImU32 Run = 0;
		ImU32 Agent = 0;
		ImU32 Web = 0;
		ImU32 Other = 0;
		ImU32 Failed = 0;
		ImU32 UserBlock = 0;
		ImU32 ClaudeBlock = 0;
		ImU32 CodeBlock = 0;
		ImU32 Injected = 0;

		// From the chrome group: names and the quiet text of headers, usage lines and gaps.
		ImU32 Accent = 0;
		ImU32 TextDisabled = 0;

		ImU32 CategoryColor(ToolCategory category) const
		{
			switch (category)
			{
			case ToolCategory::Read: return Read;
			case ToolCategory::Edit: return Edit;
			case ToolCategory::Run: return Run;
			case ToolCategory::Agent: return Agent;
			case ToolCategory::Web: return Web;
			case ToolCategory::Other: return Other;
			}

			return Other;
		}
	};

	SessionPalette ReadSessionPalette(const ResolvedScheme& scheme);
}
