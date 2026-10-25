#include <doctest/doctest.h>

#include "Core/Base/Color.h"
#include "Theme/BuiltInSchemes.h"
#include "Theme/ChromePalette.h"
#include "Theme/ColorScheme.h"

#include <array>
#include <ostream>
#include <set>
#include <span>
#include <string>

namespace {

	constexpr std::size_t kEditorBackground = static_cast<std::size_t>(Graphite::EditorColor::Background);
	constexpr std::size_t kChromeBackground = static_cast<std::size_t>(Graphite::ChromeColor::Background);
	constexpr std::size_t kPanelGap = static_cast<std::size_t>(Graphite::LayoutMetric::PanelGap);
	constexpr std::size_t kPanelRounding = static_cast<std::size_t>(Graphite::LayoutMetric::PanelRounding);

	float Brightness(const Core::Color& color)
	{
		return color.R + color.G + color.B;
	}

	bool IsLight(const Graphite::ResolvedScheme& scheme)
	{
		return Core::IsLightBackground(scheme.Chrome[kChromeBackground]);
	}

	float EditorBrightness(Graphite::BuiltInScheme which)
	{
		return Brightness(Graphite::SchemeFor(which).Editor[kEditorBackground]);
	}
}

TEST_CASE("Every built-in scheme has its own name, and the name finds the scheme again")
{
	std::set<std::string> names;

	for (const Graphite::BuiltInScheme scheme : Graphite::AllBuiltInSchemes())
	{
		const std::string name{Graphite::NameOf(scheme)};

		CAPTURE(name);
		CHECK_FALSE(name.empty());
		CHECK(Graphite::ParseBuiltInName(name) == scheme);
		CHECK(names.insert(name).second);
	}

	CHECK(names.size() == Graphite::kBuiltInSchemeCount);
}

TEST_CASE("The dark and the light scheme stay first, so the settings list them before the newer ones")
{
	const std::span<const Graphite::BuiltInScheme> all = Graphite::AllBuiltInSchemes();

	REQUIRE(all.size() == Graphite::kBuiltInSchemeCount);
	CHECK(all[0] == Graphite::BuiltInScheme::Dark);
	CHECK(all[1] == Graphite::BuiltInScheme::Light);
}

TEST_CASE("The Graphite and Gruvbox names are the ones a scheme file can name as its base")
{
	CHECK(Graphite::ParseBuiltInName("graphite") == Graphite::BuiltInScheme::Graphite);
	CHECK(Graphite::ParseBuiltInName("graphite-bright") == Graphite::BuiltInScheme::GraphiteBright);
	CHECK(Graphite::ParseBuiltInName("graphite-warm") == Graphite::BuiltInScheme::GraphiteWarm);
	CHECK(Graphite::ParseBuiltInName("graphite-cards") == Graphite::BuiltInScheme::GraphiteCards);
	CHECK(Graphite::ParseBuiltInName("graphite-light") == Graphite::BuiltInScheme::GraphiteLight);
	CHECK(Graphite::ParseBuiltInName("gruvbox-dark-hard") == Graphite::BuiltInScheme::GruvboxDarkHard);
	CHECK(Graphite::ParseBuiltInName("gruvbox-dark-medium") == Graphite::BuiltInScheme::GruvboxDarkMedium);
	CHECK(Graphite::ParseBuiltInName("gruvbox-dark-soft") == Graphite::BuiltInScheme::GruvboxDarkSoft);
	CHECK(Graphite::ParseBuiltInName("gruvbox-light-hard") == Graphite::BuiltInScheme::GruvboxLightHard);
	CHECK(Graphite::ParseBuiltInName("gruvbox-light-medium") == Graphite::BuiltInScheme::GruvboxLightMedium);
	CHECK(Graphite::ParseBuiltInName("gruvbox-light-soft") == Graphite::BuiltInScheme::GruvboxLightSoft);
}

TEST_CASE("Every built-in scheme leaves no colour of any category unset")
{
	for (const Graphite::BuiltInScheme which : Graphite::AllBuiltInSchemes())
	{
		const Graphite::ResolvedScheme& scheme = Graphite::SchemeFor(which);

		CAPTURE(Graphite::NameOf(which));

		for (const Core::Color& color : scheme.Syntax)
		{
			CHECK(color.A > 0.0f);
		}

		for (const Core::Color& color : scheme.Editor)
		{
			CHECK(color.A > 0.0f);
		}

		for (const Core::Color& color : scheme.Chrome)
		{
			CHECK(color.A > 0.0f);
		}

		for (const Core::Color& color : scheme.Git)
		{
			CHECK(color.A > 0.0f);
		}

		for (const Core::Color& color : scheme.Session)
		{
			CHECK(color.A > 0.0f);
		}
	}
}

TEST_CASE("In every built-in scheme the chrome is darker than the editor, so the editor stands out")
{
	for (const Graphite::BuiltInScheme which : Graphite::AllBuiltInSchemes())
	{
		const Graphite::ResolvedScheme& scheme = Graphite::SchemeFor(which);

		CAPTURE(Graphite::NameOf(which));
		CHECK(Brightness(scheme.Chrome[kChromeBackground]) < Brightness(scheme.Editor[kEditorBackground]));
	}
}

TEST_CASE("In every built-in scheme an occurrence of the selection is fainter than the selection itself")
{
	constexpr std::size_t kSelection = static_cast<std::size_t>(Graphite::EditorColor::Selection);
	constexpr std::size_t kSelectionOccurrence = static_cast<std::size_t>(Graphite::EditorColor::SelectionOccurrence);

	for (const Graphite::BuiltInScheme which : Graphite::AllBuiltInSchemes())
	{
		const Graphite::ResolvedScheme& scheme = Graphite::SchemeFor(which);

		CAPTURE(Graphite::NameOf(which));
		CHECK(scheme.Editor[kSelectionOccurrence].A < scheme.Editor[kSelection].A);
	}
}

TEST_CASE("The schemes named light are light, and every other one is dark")
{
	const std::set<Graphite::BuiltInScheme> light = {
		Graphite::BuiltInScheme::Light, Graphite::BuiltInScheme::GraphiteLight, Graphite::BuiltInScheme::GruvboxLightHard,
		Graphite::BuiltInScheme::GruvboxLightMedium, Graphite::BuiltInScheme::GruvboxLightSoft};

	for (const Graphite::BuiltInScheme which : Graphite::AllBuiltInSchemes())
	{
		CAPTURE(Graphite::NameOf(which));
		CHECK(IsLight(Graphite::SchemeFor(which)) == light.contains(which));
	}
}

TEST_CASE("Graphite is a lighter grey than the dark scheme, with the editor at #2B2C2F")
{
	const Core::Color graphite = Graphite::SchemeFor(Graphite::BuiltInScheme::Graphite).Editor[kEditorBackground];

	CHECK(EditorBrightness(Graphite::BuiltInScheme::Graphite) > Brightness(Graphite::DarkScheme().Editor[kEditorBackground]));
	CHECK(Core::ChannelToByte(graphite.R) == 0x2B);
	CHECK(Core::ChannelToByte(graphite.G) == 0x2C);
	CHECK(Core::ChannelToByte(graphite.B) == 0x2F);
}

TEST_CASE("The three Gruvbox darks differ by their backgrounds, hard darkest and soft lightest")
{
	CHECK(EditorBrightness(Graphite::BuiltInScheme::GruvboxDarkHard) < EditorBrightness(Graphite::BuiltInScheme::GruvboxDarkMedium));
	CHECK(EditorBrightness(Graphite::BuiltInScheme::GruvboxDarkMedium) < EditorBrightness(Graphite::BuiltInScheme::GruvboxDarkSoft));
}

TEST_CASE("The three Gruvbox lights differ by their backgrounds, soft darkest and hard lightest")
{
	CHECK(EditorBrightness(Graphite::BuiltInScheme::GruvboxLightSoft) < EditorBrightness(Graphite::BuiltInScheme::GruvboxLightMedium));
	CHECK(EditorBrightness(Graphite::BuiltInScheme::GruvboxLightMedium) < EditorBrightness(Graphite::BuiltInScheme::GruvboxLightHard));
}

TEST_CASE("Every scheme's layout stays inside the range its slider allows")
{
	for (const Graphite::BuiltInScheme which : Graphite::AllBuiltInSchemes())
	{
		const Graphite::ResolvedScheme& scheme = Graphite::SchemeFor(which);

		CAPTURE(Graphite::NameOf(which));

		for (std::size_t metricIndex = 0; metricIndex < Graphite::kLayoutMetricCount; ++metricIndex)
		{
			const Graphite::LayoutRange range = Graphite::RangeOf(static_cast<Graphite::LayoutMetric>(metricIndex));

			CHECK(scheme.Layout[metricIndex] >= range.Smallest);
			CHECK(scheme.Layout[metricIndex] <= range.Largest);
		}
	}
}

TEST_CASE("Only Graphite Cards sets its own layout: wider gaps between panels and rounder corners")
{
	const std::array<float, Graphite::kLayoutMetricCount> defaults = Graphite::DefaultLayout();

	for (const Graphite::BuiltInScheme which : Graphite::AllBuiltInSchemes())
	{
		const Graphite::ResolvedScheme& scheme = Graphite::SchemeFor(which);

		CAPTURE(Graphite::NameOf(which));

		if (which == Graphite::BuiltInScheme::GraphiteCards)
		{
			CHECK(scheme.Layout[kPanelGap] > defaults[kPanelGap]);
			CHECK(scheme.Layout[kPanelRounding] > defaults[kPanelRounding]);
		}
		else
		{
			CHECK(scheme.Layout == defaults);
		}
	}
}

TEST_CASE("The chrome palette carries the scheme's layout as style metrics")
{
	const Core::ChromePalette cards = Graphite::ChromePaletteFor(Graphite::SchemeFor(Graphite::BuiltInScheme::GraphiteCards));
	const Core::ChromePalette dark = Graphite::ChromePaletteFor(Graphite::DarkScheme());

	CHECK(cards.Metrics.PanelGap == doctest::Approx(6.0f));
	CHECK(cards.Metrics.PanelRounding == doctest::Approx(8.0f));
	CHECK(dark.Metrics == Core::StyleMetrics{});
}

TEST_CASE("A scheme's own layout wins over its base's, one measurement at a time")
{
	Graphite::ColorScheme scheme;

	scheme.Layout[kPanelGap] = 9.0f;

	const Graphite::ResolvedScheme resolved =
		Graphite::ResolveSchemeOverBase(scheme, Graphite::SchemeFor(Graphite::BuiltInScheme::GraphiteCards));

	CHECK(resolved.Layout[kPanelGap] == doctest::Approx(9.0f));
	CHECK(resolved.Layout[kPanelRounding] == doctest::Approx(8.0f));
}
