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

TEST_CASE("Both built-in schemes define every git colour")
{
	for (const Graphite::ResolvedScheme& scheme : {Graphite::DarkScheme(), Graphite::LightScheme()})
	{
		for (std::size_t index = 0; index < static_cast<std::size_t>(Graphite::GitColor::Count);
			 ++index)
		{
			// A colour left at its default is fully transparent black, which draws as
			// nothing at all — the row simply disappears rather than looking wrong.
			CHECK(scheme.Git[index].A > 0.0f);
		}
	}
}

TEST_CASE("Both built-in schemes define every session colour")
{
	for (const Graphite::ResolvedScheme& scheme : {Graphite::DarkScheme(), Graphite::LightScheme()})
	{
		for (std::size_t index = 0;
			 index < static_cast<std::size_t>(Graphite::SessionColor::Count); ++index)
		{
			// A colour left at its default is fully transparent black, which draws as
			// nothing at all — the row simply disappears rather than looking wrong.
			CHECK(scheme.Session[index].A > 0.0f);
		}
	}
}

TEST_CASE("The graph lanes are visibly different from one another")
{
	const Graphite::ResolvedScheme scheme = Graphite::DarkScheme();

	const auto lane = [&scheme](Graphite::GitColor color) {
		return scheme.Git[static_cast<std::size_t>(color)];
	};

	// Adjacent lanes carry adjacent branches. Two lanes that read the same defeat the
	// entire point of drawing lanes. Core::Color has no operator==, so the channels are
	// compared one at a time.
	const auto differ = [](const Core::Color& a, const Core::Color& b) {
		return a.R != b.R || a.G != b.G || a.B != b.B || a.A != b.A;
	};

	CHECK(differ(lane(Graphite::GitColor::GraphLane1), lane(Graphite::GitColor::GraphLane2)));
	CHECK(differ(lane(Graphite::GitColor::GraphLane2), lane(Graphite::GitColor::GraphLane3)));
}
