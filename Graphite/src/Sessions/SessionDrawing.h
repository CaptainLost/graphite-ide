#pragma once

#include "Sessions/Transcript/ToolFacts.h"
#include "UI/TypographicMarks.h"

#include <imgui.h>

#include <span>
#include <string>

namespace Graphite {

	inline constexpr const char* kDot = " \xc2\xb7 ";

	void ColouredText(ImU32 colour, const std::string& text);

	void CategoryBar(ImU32 colour, float height);

	std::string Fitted(const std::string& text, float width);

	const char* IconOf(ToolCategory category);

	void DrawPiece(ImVec2& origin, ImU32 colour, const std::string& text);

	bool SegmentButton(const char* label, bool selected);

	bool SegmentedChoice(std::span<const char* const> labels, int& selected);
}
