#include <doctest/doctest.h>

#include "Editor/EditorSettings.h"

// None of these call Load(). AppSettings writes beside the test executable, so a Load() here
// would read whatever an earlier case's setter left behind and make the suite order-dependent.
TEST_CASE("EditorSettings starts at its documented defaults")
{
	const Graphite::EditorSettings settings;

	CHECK(settings.GetFontSize() == doctest::Approx(13.0f));
	CHECK(settings.GetIndentStyle().UseSpaces);
	CHECK(settings.GetIndentStyle().Width == 4);
	CHECK_FALSE(settings.IsWordWrap());
}

// The clamp was a formality while the only way to change the size was a menu item stepping by
// 1.0 from a known value. A slider hands it whatever the user drags to.
TEST_CASE("EditorSettings clamps the font size to its range")
{
	Graphite::EditorSettings settings;

	settings.SetFontSize(1.0f);
	CHECK(settings.GetFontSize() == doctest::Approx(8.0f));

	settings.SetFontSize(1000.0f);
	CHECK(settings.GetFontSize() == doctest::Approx(32.0f));
}

TEST_CASE("EditorSettings keeps a font size exactly on the bounds")
{
	Graphite::EditorSettings settings;

	settings.SetFontSize(8.0f);
	CHECK(settings.GetFontSize() == doctest::Approx(8.0f));

	settings.SetFontSize(32.0f);
	CHECK(settings.GetFontSize() == doctest::Approx(32.0f));
}

TEST_CASE("EditorSettings accepts a font size inside the range unchanged")
{
	Graphite::EditorSettings settings;

	settings.SetFontSize(17.5f);
	CHECK(settings.GetFontSize() == doctest::Approx(17.5f));
}

TEST_CASE("EditorSettings round-trips the indent style")
{
	Graphite::EditorSettings settings;

	settings.SetIndentWithSpaces(false);
	CHECK_FALSE(settings.GetIndentStyle().UseSpaces);

	settings.SetIndentWithSpaces(true);
	CHECK(settings.GetIndentStyle().UseSpaces);

	settings.SetIndentWidth(8);
	CHECK(settings.GetIndentStyle().Width == 8);
}

// Load() already treats a stored zero as "use the default", so a setter that could store one
// would make that branch reachable from the UI and an indent of zero columns possible.
TEST_CASE("EditorSettings refuses an indent width of zero")
{
	Graphite::EditorSettings settings;

	settings.SetIndentWidth(2);
	settings.SetIndentWidth(0);

	CHECK(settings.GetIndentStyle().Width == 2);
}
