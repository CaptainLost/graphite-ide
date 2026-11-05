#include <doctest/doctest.h>

#include "Theme/BuiltInSchemes.h"
#include "Theme/ChromeColor.h"
#include "Theme/ColorScheme.h"
#include "Theme/EditorColor.h"
#include "Theme/GitPalette.h"
#include "Theme/Palettes.h"

#include "Core/Base/Color.h"

#include <imgui.h>

#include <cstddef>
#include <utility>

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

namespace {

	float LightnessOf(ImU32 colour)
	{
		const ImVec4 channels = ImGui::ColorConvertU32ToFloat4(colour);
		constexpr float kRed = 0.2126f;
		constexpr float kGreen = 0.7152f;
		constexpr float kBlue = 0.0722f;

		return kRed * channels.x + kGreen * channels.y + kBlue * channels.z;
	}

	ImU32 Packed(const Core::Color& colour)
	{
		return IM_COL32(Core::ChannelToByte(colour.R), Core::ChannelToByte(colour.G), Core::ChannelToByte(colour.B),
						Core::ChannelToByte(colour.A));
	}
}

TEST_CASE("Git window: the ref pills take the scheme's colours with dark words on light pills in the light scheme")
{
	const GitPalette light = ReadGitPalette(LightScheme());
	const GitPalette dark = ReadGitPalette(DarkScheme());

	for (const auto& [fill, text] : {std::pair{light.RefLocalFill, light.RefLocalText}, std::pair{light.RefRemoteFill, light.RefRemoteText},
									 std::pair{light.RefTagFill, light.RefTagText}})
	{
		CHECK(LightnessOf(fill) > LightnessOf(text));
		CHECK(LightnessOf(fill) > 0.6f);
	}

	for (const auto& [fill, text] : {std::pair{dark.RefLocalFill, dark.RefLocalText}, std::pair{dark.RefRemoteFill, dark.RefRemoteText},
									 std::pair{dark.RefTagFill, dark.RefTagText}})
	{
		CHECK(LightnessOf(fill) < LightnessOf(text));
		CHECK(LightnessOf(fill) < 0.4f);
	}

	CHECK(light.RefHeadFill == Packed(ColourFromScheme(LightScheme(), ChromeColor::Accent)));
	CHECK(light.RefHeadText == Packed(ColourFromScheme(LightScheme(), ChromeColor::Background)));
	CHECK(light.RefLocalFill != dark.RefLocalFill);
	CHECK(light.RefTagText != dark.RefTagText);
}
