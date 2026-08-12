#pragma once

#include <imgui.h>

#include <string_view>

namespace Core {

	const char* IconForExtension(std::string_view extension);

	const ImWchar* UsedGlyphRanges();
}