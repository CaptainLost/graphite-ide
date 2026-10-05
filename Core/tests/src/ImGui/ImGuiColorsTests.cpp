#include <doctest/doctest.h>

#include "Core/Base/Color.h"
#include "Core/ImGui/ImGuiColors.h"

#include <imgui.h>

#include <cmath>
#include <limits>

TEST_CASE("Colour helpers change only the channel they name")
{
	const ImVec4 converted = Core::ToImVec4(Core::Color{.R = 0.1f, .G = 0.2f, .B = 0.3f, .A = 0.4f});
	CHECK(converted.x == 0.1f);
	CHECK(converted.w == 0.4f);

	CHECK(Core::WithAlphaScaled(IM_COL32(10, 20, 30, 255), 0.6f) == IM_COL32(10, 20, 30, 153));
	CHECK(Core::WithAlphaScaled(IM_COL32(10, 20, 30, 200), 2.0f) == IM_COL32(10, 20, 30, 255));
	CHECK(Core::WithAlphaScaled(IM_COL32(10, 20, 30, 200), 0.0f) == IM_COL32(10, 20, 30, 0));

	const ImVec4 halved = Core::WithAlphaScaled(ImVec4(1.0f, 0.5f, 0.25f, 0.5f), 0.5f);
	CHECK(halved.y == 0.5f);
	CHECK(halved.w == 0.25f);

	static_assert(Core::WithAlphaReplaced(IM_COL32(1, 2, 3, 4), 0x38u) == IM_COL32(1, 2, 3, 0x38));
	static_assert(Core::WithAlphaReplaced(IM_COL32(1, 2, 3, 4), 0x1FFu) == IM_COL32(1, 2, 3, 0xFF));
	CHECK(Core::WithAlphaReplaced(ImVec4(1.0f, 0.5f, 0.25f, 0.5f), 0.35f).w == 0.35f);

	static_assert(Core::WithOpaqueAlpha(IM_COL32(1, 2, 3, 0)) == IM_COL32(1, 2, 3, 255));
	static_assert(Core::AlphaByteOf(IM_COL32(1, 2, 3, 0x7F)) == 0x7Fu);

	const ImVec4 brighter = Core::WithBrightnessScaled(ImVec4(0.5f, 0.9f, 0.0f, 0.3f), 1.5f);
	CHECK(brighter.x == 0.75f);
	CHECK(brighter.y == 1.0f);
	CHECK(brighter.w == 0.3f);
	CHECK(Core::WithBrightnessScaled(ImVec4(0.5f, 0.0f, 0.0f, 1.0f), -1.0f).x == -0.5f);
}

TEST_CASE("ToImVec4 passes a NaN channel through")
{
	CHECK(std::isnan(Core::ToImVec4(Core::Color{.R = std::numeric_limits<float>::quiet_NaN()}).x));
}
