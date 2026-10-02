#include <doctest/doctest.h>

#include "Editor/EditorSettings.h"

#include "TempAppSettings.h"

// Every case gets its own AppSettings over a temporary file (TempAppSettings), so a case starts
// from the defaults whatever an earlier one set, and the user's own settings are never touched.
TEST_CASE("EditorSettings starts at its documented defaults")
{
	TempAppSettings store;
	const Graphite::EditorSettings settings{store.Get()};

	CHECK(settings.GetFontSize() == doctest::Approx(13.0f));
	CHECK(settings.GetIndentStyle().UseSpaces);
	CHECK(settings.GetIndentStyle().Width == 4);
	CHECK_FALSE(settings.IsWordWrap());
}

// The clamp was a formality while the only way to change the size was a menu item stepping by
// 1.0 from a known value. A slider hands it whatever the user drags to.
TEST_CASE("EditorSettings clamps the font size to its range")
{
	TempAppSettings store;
	Graphite::EditorSettings settings{store.Get()};

	settings.SetFontSize(1.0f);
	CHECK(settings.GetFontSize() == doctest::Approx(8.0f));

	settings.SetFontSize(1000.0f);
	CHECK(settings.GetFontSize() == doctest::Approx(32.0f));
}

TEST_CASE("EditorSettings keeps a font size exactly on the bounds")
{
	TempAppSettings store;
	Graphite::EditorSettings settings{store.Get()};

	settings.SetFontSize(8.0f);
	CHECK(settings.GetFontSize() == doctest::Approx(8.0f));

	settings.SetFontSize(32.0f);
	CHECK(settings.GetFontSize() == doctest::Approx(32.0f));
}

TEST_CASE("EditorSettings accepts a font size inside the range unchanged")
{
	TempAppSettings store;
	Graphite::EditorSettings settings{store.Get()};

	settings.SetFontSize(17.5f);
	CHECK(settings.GetFontSize() == doctest::Approx(17.5f));
}

TEST_CASE("EditorSettings round-trips the indent style")
{
	TempAppSettings store;
	Graphite::EditorSettings settings{store.Get()};

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
	TempAppSettings store;
	Graphite::EditorSettings settings{store.Get()};

	settings.SetIndentWidth(2);
	settings.SetIndentWidth(0);

	CHECK(settings.GetIndentStyle().Width == 2);
}

// The slider offers 1 to 8, but Ctrl+Click turns it into a text input that ImGui does not clamp
// on its own — SliderInt is given ImGuiSliderFlags_AlwaysClamp for that, but the setter has to
// hold the line too, since the same value can arrive from a hand-edited graphite.json.
TEST_CASE("EditorSettings clamps an indent width above its maximum")
{
	TempAppSettings store;
	Graphite::EditorSettings settings{store.Get()};

	settings.SetIndentWidth(999999999);

	CHECK(settings.GetIndentStyle().Width == 8);
}

TEST_CASE("EditorSettings keeps an indent width exactly on the maximum")
{
	TempAppSettings store;
	Graphite::EditorSettings settings{store.Get()};

	settings.SetIndentWidth(8);

	CHECK(settings.GetIndentStyle().Width == 8);
}

TEST_CASE("EditorSettings shows both kinds of change marker by default")
{
	TempAppSettings store;
	const Graphite::EditorSettings settings{store.Get()};

	CHECK(settings.ShowsGutterChangeMarkers());
	CHECK(settings.ShowsScrollbarChangeMarkers());
}

TEST_CASE("EditorSettings turns each kind of change marker off on its own")
{
	TempAppSettings store;
	Graphite::EditorSettings settings{store.Get()};

	settings.SetGutterChangeMarkers(false);
	CHECK_FALSE(settings.ShowsGutterChangeMarkers());
	CHECK(settings.ShowsScrollbarChangeMarkers());

	settings.SetScrollbarChangeMarkers(false);
	CHECK_FALSE(settings.ShowsScrollbarChangeMarkers());

	settings.SetGutterChangeMarkers(true);
	settings.SetScrollbarChangeMarkers(true);
	CHECK(settings.ShowsGutterChangeMarkers());
	CHECK(settings.ShowsScrollbarChangeMarkers());
}

TEST_CASE("EditorSettings loads back what it set, after a restart")
{
	TempAppSettings store;

	{
		Graphite::EditorSettings settings{store.Get()};
		settings.SetFontSize(17.0f);
		settings.SetIndentWithSpaces(false);
		settings.SetIndentWidth(2);
		settings.SetGutterChangeMarkers(false);
	}

	Graphite::EditorSettings reloaded{store.Reload()};
	reloaded.Load();

	CHECK(reloaded.GetFontSize() == doctest::Approx(17.0f));
	CHECK_FALSE(reloaded.GetIndentStyle().UseSpaces);
	CHECK(reloaded.GetIndentStyle().Width == 2);
	CHECK_FALSE(reloaded.ShowsGutterChangeMarkers());
	CHECK(reloaded.ShowsScrollbarChangeMarkers());
}

TEST_CASE("EditorSettings hover defaults to information on, preview with Ctrl, 500 ms")
{
	TempAppSettings store;
	const Graphite::EditorSettings settings{store.Get()};

	const Graphite::HoverSettings hover = settings.GetHoverSettings();

	CHECK(hover.ShowInfo);
	CHECK(hover.Preview == Graphite::DefinitionPreviewMode::WithCtrl);
	CHECK(hover.DelayMs == 500);
}

TEST_CASE("EditorSettings hover values survive a reload")
{
	TempAppSettings store;

	{
		Graphite::EditorSettings settings{store.Get()};
		settings.SetHoverInfo(false);
		settings.SetDefinitionPreview(Graphite::DefinitionPreviewMode::Always);
		settings.SetHoverDelayMs(800);
	}

	Graphite::EditorSettings reloaded{store.Get()};
	reloaded.Load();

	const Graphite::HoverSettings hover = reloaded.GetHoverSettings();

	CHECK_FALSE(hover.ShowInfo);
	CHECK(hover.Preview == Graphite::DefinitionPreviewMode::Always);
	CHECK(hover.DelayMs == 800);
}

TEST_CASE("EditorSettings clamps the hover delay to 200..1500")
{
	TempAppSettings store;
	Graphite::EditorSettings settings{store.Get()};

	settings.SetHoverDelayMs(10);
	CHECK(settings.GetHoverSettings().DelayMs == 200);

	settings.SetHoverDelayMs(99999);
	CHECK(settings.GetHoverSettings().DelayMs == 1500);
}

TEST_CASE("EditorSettings reads an unknown stored preview mode as the default")
{
	TempAppSettings store;
	store.Get().SetHoverPreview("sometimes");

	Graphite::EditorSettings settings{store.Get()};
	settings.Load();

	CHECK(settings.GetHoverSettings().Preview == Graphite::DefinitionPreviewMode::WithCtrl);
}