#include <doctest/doctest.h>

#include "Theme/BuiltInSchemes.h"

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
