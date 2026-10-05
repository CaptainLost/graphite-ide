#include <doctest/doctest.h>

#include "Theme/ColorScheme.h"
#include "Theme/EditorColor.h"
#include "Theme/Palettes.h"

#include "Core/Base/Color.h"

#include <imgui.h>

#include <cstddef>

using namespace Graphite;

TEST_CASE("The danger colour is the scheme's diagnostic error as ImGui reads it")
{
	ResolvedScheme scheme{};
	scheme.Editor[static_cast<std::size_t>(EditorColor::DiagnosticError)] =
		Core::Color{.R = 1.0f, .G = 0.0f, .B = 0.0f, .A = 1.0f};

	const ImVec4 danger = DangerColourOf(scheme);

	CHECK(danger.x == 1.0f);
	CHECK(danger.y == 0.0f);
	CHECK(danger.w == 1.0f);

	static_assert(kErrorTextColour.x == 0.94f);
	static_assert(kErrorTextColour.w == 1.0f);
}
