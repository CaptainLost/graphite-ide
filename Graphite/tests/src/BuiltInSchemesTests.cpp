#include <doctest/doctest.h>

#include "Theme/BuiltInSchemes.h"

TEST_CASE("Both built-in schemes are reachable by name")
{
	CHECK(Graphite::BuiltInScheme("dark") == &Graphite::DarkScheme());
	CHECK(Graphite::BuiltInScheme("light") == &Graphite::LightScheme());
}

TEST_CASE("An unknown base name resolves to nothing")
{
	CHECK(Graphite::BuiltInScheme("solarized") == nullptr);
	CHECK(Graphite::BuiltInScheme("") == nullptr);
	CHECK(Graphite::BuiltInScheme("Dark") == nullptr);
}

TEST_CASE("The built-in names are exactly the ones the store must refuse")
{
	CHECK(Graphite::IsBuiltInName("dark"));
	CHECK(Graphite::IsBuiltInName("light"));
	CHECK_FALSE(Graphite::IsBuiltInName("mine"));
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
