#pragma once

#include <imgui.h>

#include <span>
#include <string_view>

namespace Core {

	const char* IconForExtension(std::string_view extension);

	const ImWchar* UsedGlyphRanges();

	const ImWchar* UsedIconSpan();

	std::span<const char* const> UsedIcons();
}