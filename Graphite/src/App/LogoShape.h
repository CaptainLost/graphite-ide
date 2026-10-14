#pragma once

#include <imgui.h>

#include <string_view>
#include <vector>

namespace Graphite {

	struct LogoFace
	{
		std::vector<ImVec2> Points;
		ImVec2 From;
		ImVec2 To;
		ImU32 FromColour = 0;
		ImU32 ToColour = 0;
	};

	struct LogoShape
	{
		std::vector<LogoFace> Faces;
		ImVec2 Min;
		ImVec2 Max;
	};

	LogoShape ParseLogoSvg(std::string_view svg);
}
