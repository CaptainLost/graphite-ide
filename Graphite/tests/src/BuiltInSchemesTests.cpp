#include <doctest/doctest.h>

#include "Theme/BuiltInSchemes.h"

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

// A base is what every omitted colour falls back to, so a base with a gap in it is a colour
// nobody can see and nobody can set. Alpha is exempt: a translucent highlight is a real value.
TEST_CASE("Neither base leaves a colour unset")
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

// The two bases exist so a light editor is three changes rather than twenty-seven. If their
// backgrounds were similar, that would not be true.
TEST_CASE("The two bases differ where it matters")
{
	const std::size_t background = static_cast<std::size_t>(Graphite::EditorColor::Background);

	CHECK(Graphite::DarkScheme().Editor[background].R <
		  Graphite::LightScheme().Editor[background].R);
}

// The same rule the syntax and editor tables are held to: a base with a gap in it is a colour
// nobody can see and nobody can set.
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

// Chrome and editor backgrounds are separate colours on purpose — the panels are a shade off
// the page. What must hold is the direction: the light scheme's chrome cannot be darker than
// the dark scheme's.
TEST_CASE("The light scheme's chrome is lighter than the dark scheme's")
{
	constexpr auto background = static_cast<std::size_t>(Graphite::ChromeColor::Background);

	CHECK(Graphite::DarkScheme().Chrome[background].R <
		  Graphite::LightScheme().Chrome[background].R);
}

// Drawn as a box behind the glyph, so a fully opaque one would hide the bracket it is marking.
TEST_CASE("Both bracket highlights are translucent in both built-ins")
{
	constexpr auto matching = static_cast<std::size_t>(Graphite::EditorColor::MatchingBracket);
	constexpr auto unmatched = static_cast<std::size_t>(Graphite::EditorColor::UnmatchedBracket);

	for (const Graphite::ResolvedScheme* base :
		 {&Graphite::DarkScheme(), &Graphite::LightScheme()})
	{
		CHECK(base->Editor[matching].A > 0.0f);
		CHECK(base->Editor[matching].A < 1.0f);
		CHECK(base->Editor[unmatched].A > 0.0f);
		CHECK(base->Editor[unmatched].A < 1.0f);
	}
}
