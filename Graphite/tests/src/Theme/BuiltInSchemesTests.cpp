#include <doctest/doctest.h>

#include "Theme/BuiltInSchemes.h"
#include "Theme/ColorScheme.h"

#include "Core/Base/Color.h"

namespace {

	Core::Color GitColorOf(const Graphite::ResolvedScheme& scheme, Graphite::GitColor color)
	{
		return scheme.Git[static_cast<std::size_t>(color)];
	}

	bool ColorsDiffer(const Core::Color& first, const Core::Color& second)
	{
		return first.R != second.R || first.G != second.G || first.B != second.B ||
			   first.A != second.A;
	}
}

TEST_CASE("Both built-in schemes are reachable by their enumerator")
{
	CHECK(&Graphite::SchemeFor(Graphite::BuiltInScheme::Dark) == &Graphite::DarkScheme());
	CHECK(&Graphite::SchemeFor(Graphite::BuiltInScheme::Light) == &Graphite::LightScheme());
}

TEST_CASE("An unknown base name resolves to nothing")
{
	CHECK(Graphite::ParseBuiltInName("solarized") == std::nullopt);
	CHECK(Graphite::ParseBuiltInName("") == std::nullopt);
	CHECK(Graphite::ParseBuiltInName("Dark") == std::nullopt);
}

TEST_CASE("The built-in names are exactly the ones the store must refuse")
{
	CHECK(Graphite::ParseBuiltInName("dark") == Graphite::BuiltInScheme::Dark);
	CHECK(Graphite::ParseBuiltInName("light") == Graphite::BuiltInScheme::Light);
	CHECK_FALSE(Graphite::ParseBuiltInName("mine").has_value());
}

TEST_CASE("Neither base leaves a syntax or editor colour unset, since alpha above zero is the only gap a default colour can show")
{
	for (const Graphite::ResolvedScheme* base :
		 {&Graphite::DarkScheme(), &Graphite::LightScheme()})
	{
		for (const Core::Color& color : base->Syntax)
		{
			CHECK(color.A > 0.0f);
		}

		for (const Core::Color& color : base->Editor)
		{
			CHECK(color.A > 0.0f);
		}
	}
}

TEST_CASE("The dark editor background is darker than the light one, so a light editor is a real change")
{
	const std::size_t background = static_cast<std::size_t>(Graphite::EditorColor::Background);

	CHECK(Graphite::DarkScheme().Editor[background].R <
		  Graphite::LightScheme().Editor[background].R);
}

TEST_CASE("Neither base leaves a chrome colour unset")
{
	for (const Graphite::ResolvedScheme* base :
		 {&Graphite::DarkScheme(), &Graphite::LightScheme()})
	{
		for (const Core::Color& color : base->Chrome)
		{
			CHECK(color.A > 0.0f);
		}
	}
}

TEST_CASE("The light scheme's chrome is lighter than the dark scheme's")
{
	constexpr std::size_t background = static_cast<std::size_t>(Graphite::ChromeColor::Background);

	CHECK(Graphite::DarkScheme().Chrome[background].R <
		  Graphite::LightScheme().Chrome[background].R);
}

TEST_CASE("Both bracket highlights are translucent in both built-ins so the bracket under them shows")
{
	constexpr std::size_t matching = static_cast<std::size_t>(Graphite::EditorColor::MatchingBracket);
	constexpr std::size_t unmatched = static_cast<std::size_t>(Graphite::EditorColor::UnmatchedBracket);

	for (const Graphite::ResolvedScheme* base :
		 {&Graphite::DarkScheme(), &Graphite::LightScheme()})
	{
		CHECK(base->Editor[matching].A > 0.0f);
		CHECK(base->Editor[matching].A < 1.0f);
		CHECK(base->Editor[unmatched].A > 0.0f);
		CHECK(base->Editor[unmatched].A < 1.0f);
	}
}

TEST_CASE("Both built-in schemes define every git colour")
{
	for (const Graphite::ResolvedScheme& scheme : {Graphite::DarkScheme(), Graphite::LightScheme()})
	{
		for (std::size_t colorIndex = 0; colorIndex < static_cast<std::size_t>(Graphite::GitColor::Count);
			 ++colorIndex)
		{
			CHECK(scheme.Git[colorIndex].A > 0.0f);
		}
	}
}

TEST_CASE("Both built-in schemes define every session colour")
{
	for (const Graphite::ResolvedScheme& scheme : {Graphite::DarkScheme(), Graphite::LightScheme()})
	{
		for (std::size_t colorIndex = 0;
			 colorIndex < static_cast<std::size_t>(Graphite::SessionColor::Count); ++colorIndex)
		{
			CHECK(scheme.Session[colorIndex].A > 0.0f);
		}
	}
}

TEST_CASE("The graph lanes are visibly different from one another")
{
	const Graphite::ResolvedScheme scheme = Graphite::DarkScheme();

	CHECK(ColorsDiffer(GitColorOf(scheme, Graphite::GitColor::GraphLane1),
					   GitColorOf(scheme, Graphite::GitColor::GraphLane2)));
	CHECK(ColorsDiffer(GitColorOf(scheme, Graphite::GitColor::GraphLane2),
					   GitColorOf(scheme, Graphite::GitColor::GraphLane3)));
}

TEST_CASE("In both built-ins the chrome background is darker than the editor background, so the editor stands out")
{
	for (const Graphite::ResolvedScheme* base :
		 {&Graphite::DarkScheme(), &Graphite::LightScheme()})
	{
		const Core::Color chrome = base->Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Background)];
		const Core::Color editor = base->Editor[static_cast<std::size_t>(Graphite::EditorColor::Background)];

		CHECK(chrome.R + chrome.G + chrome.B < editor.R + editor.G + editor.B);
	}
}

TEST_CASE("The dark chrome is #141518 with a #26292E border")
{
	const Graphite::ResolvedScheme& dark = Graphite::DarkScheme();
	const Core::Color background = dark.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Background)];
	const Core::Color border = dark.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Border)];

	CHECK(Core::ChannelToByte(background.R) == 20);
	CHECK(Core::ChannelToByte(background.G) == 21);
	CHECK(Core::ChannelToByte(background.B) == 24);
	CHECK(Core::ChannelToByte(border.R) == 38);
	CHECK(Core::ChannelToByte(border.G) == 41);
	CHECK(Core::ChannelToByte(border.B) == 46);
}

namespace {

	struct EditorBytes
	{
		int R = 0;
		int G = 0;
		int B = 0;
		int A = 0;
	};

	EditorBytes EditorBytesOf(const Graphite::ResolvedScheme& scheme, Graphite::EditorColor color)
	{
		const Core::Color value = scheme.Editor[static_cast<std::size_t>(color)];

		return EditorBytes{.R = Core::ChannelToByte(value.R),
						   .G = Core::ChannelToByte(value.G),
						   .B = Core::ChannelToByte(value.B),
						   .A = Core::ChannelToByte(value.A)};
	}

	void CheckEditorColour(const Graphite::ResolvedScheme& scheme, Graphite::EditorColor color,
						   EditorBytes expected)
	{
		const EditorBytes actual = EditorBytesOf(scheme, color);

		CHECK(actual.R == expected.R);
		CHECK(actual.G == expected.G);
		CHECK(actual.B == expected.B);
		CHECK(actual.A == expected.A);
	}

	constexpr int kOpaque = 255;
	constexpr int kCurrentLineAlpha = 10;
	constexpr int kDarkGuideAlpha = 45;
	constexpr int kLightGuideAlpha = 20;
}

TEST_CASE("The dark editor dims its line numbers, guides and lens labels")
{
	const Graphite::ResolvedScheme& dark = Graphite::DarkScheme();

	CheckEditorColour(dark, Graphite::EditorColor::LineNumber, {77, 82, 91, kOpaque});
	CheckEditorColour(dark, Graphite::EditorColor::CurrentLine, {255, 255, 255, kCurrentLineAlpha});
	CheckEditorColour(dark, Graphite::EditorColor::Guide, {123, 130, 141, kDarkGuideAlpha});
	CheckEditorColour(dark, Graphite::EditorColor::Lens, {107, 113, 128, kOpaque});
}

TEST_CASE("The light editor dims its line numbers, guides and lens labels")
{
	const Graphite::ResolvedScheme& light = Graphite::LightScheme();

	CheckEditorColour(light, Graphite::EditorColor::LineNumber, {180, 180, 180, kOpaque});
	CheckEditorColour(light, Graphite::EditorColor::CurrentLine, {0, 0, 0, kCurrentLineAlpha});
	CheckEditorColour(light, Graphite::EditorColor::Guide, {0, 0, 0, kLightGuideAlpha});
	CheckEditorColour(light, Graphite::EditorColor::Lens, {160, 160, 160, kOpaque});
}

TEST_CASE("A user scheme's editor colours win over the built-in ones")
{
	constexpr Core::Color kMine{0.1f, 0.2f, 0.3f, 1.0f};

	Graphite::ColorScheme scheme;

	for (const Graphite::EditorColor color :
		 {Graphite::EditorColor::LineNumber, Graphite::EditorColor::CurrentLine,
		  Graphite::EditorColor::Guide, Graphite::EditorColor::Lens})
	{
		scheme.Editor[static_cast<std::size_t>(color)] = kMine;
	}

	for (const Graphite::ResolvedScheme* base : {&Graphite::DarkScheme(), &Graphite::LightScheme()})
	{
		const Graphite::ResolvedScheme resolved = Graphite::ResolveSchemeOverBase(scheme, *base);

		for (const Graphite::EditorColor color :
			 {Graphite::EditorColor::LineNumber, Graphite::EditorColor::CurrentLine,
			  Graphite::EditorColor::Guide, Graphite::EditorColor::Lens})
		{
			CHECK(resolved.Editor[static_cast<std::size_t>(color)].R == doctest::Approx(kMine.R));
		}
	}
}
