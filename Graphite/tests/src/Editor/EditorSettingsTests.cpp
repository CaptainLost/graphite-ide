#include <doctest/doctest.h>

#include "Editor/EditorSettings.h"

#include "TempAppSettings.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"

#include <vector>

namespace {
	constexpr bool kEnabled = true;
	constexpr bool kDisabled = false;
}

TEST_CASE("EditorSettings starts at its documented defaults")
{
	TempAppSettings store;
	const Graphite::EditorSettings settings{store.Get()};

	CHECK(settings.GetFontSize() == doctest::Approx(17.0f));
	CHECK(settings.GetIndentStyle().UseSpaces);
	CHECK(settings.GetIndentStyle().Width == 4);
	CHECK_FALSE(settings.IsWordWrap());
}

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

	settings.SetIndentWithSpaces(kDisabled);
	CHECK_FALSE(settings.GetIndentStyle().UseSpaces);

	settings.SetIndentWithSpaces(kEnabled);
	CHECK(settings.GetIndentStyle().UseSpaces);

	settings.SetIndentWidth(8);
	CHECK(settings.GetIndentStyle().Width == 8);
}

TEST_CASE("EditorSettings refuses an indent width of zero")
{
	TempAppSettings store;
	Graphite::EditorSettings settings{store.Get()};

	settings.SetIndentWidth(2);
	settings.SetIndentWidth(0);

	CHECK(settings.GetIndentStyle().Width == 2);
}

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

	CHECK(settings.HasGutterChangeMarkers());
	CHECK(settings.HasScrollbarChangeMarkers());
}

TEST_CASE("EditorSettings turns each kind of change marker off on its own")
{
	TempAppSettings store;
	Graphite::EditorSettings settings{store.Get()};

	settings.SetGutterChangeMarkers(kDisabled);
	CHECK_FALSE(settings.HasGutterChangeMarkers());
	CHECK(settings.HasScrollbarChangeMarkers());

	settings.SetScrollbarChangeMarkers(kDisabled);
	CHECK_FALSE(settings.HasScrollbarChangeMarkers());

	settings.SetGutterChangeMarkers(kEnabled);
	settings.SetScrollbarChangeMarkers(kEnabled);
	CHECK(settings.HasGutterChangeMarkers());
	CHECK(settings.HasScrollbarChangeMarkers());
}

TEST_CASE("EditorSettings wraps diffs on a switch of their own, off by default")
{
	TempAppSettings store;

	store.Get().SetWordWrap(kEnabled);

	{
		Graphite::EditorSettings settings{store.Get()};
		settings.Load();

		CHECK(settings.IsWordWrap());
		CHECK_FALSE(settings.IsDiffWordWrap());
	}

	store.Get().SetWordWrap(kDisabled);

	{
		Graphite::EditorSettings settings{store.Get()};
		settings.Load();
		settings.SetDiffWordWrap(kEnabled);

		CHECK(settings.IsDiffWordWrap());
	}

	Graphite::EditorSettings reloaded{store.Get()};
	reloaded.Load();

	CHECK(reloaded.IsDiffWordWrap());
	CHECK_FALSE(reloaded.IsWordWrap());
}

TEST_CASE("Diffs start side by side, hunks only, syntax coloured and unwrapped")
{
	TempAppSettings store;
	Graphite::EditorSettings settings{store.Get()};
	settings.Load();

	const Graphite::DiffViewOptions options = settings.GetDiffViewOptions();

	CHECK(options.Mode == Graphite::DiffViewMode::SideBySide);
	CHECK(options.Scope == Graphite::DiffScope::Hunks);
	CHECK(options.ColourSyntax);
	CHECK_FALSE(options.WordWrap);
}

TEST_CASE("Diff options chosen in one diff are what every diff is drawn with, after a restart too")
{
	TempAppSettings store;

	const Graphite::DiffViewOptions chosen{.Mode = Graphite::DiffViewMode::Unified,
										   .Scope = Graphite::DiffScope::WholeFile,
										   .ColourSyntax = false,
										   .WordWrap = true};

	{
		Graphite::EditorSettings settings{store.Get()};
		settings.Load();
		settings.SetDiffViewOptions(chosen);

		CHECK(settings.GetDiffViewOptions() == chosen);
		CHECK(settings.IsDiffWordWrap());
	}

	Graphite::EditorSettings reloaded{store.Get()};
	reloaded.Load();

	CHECK(reloaded.GetDiffViewOptions() == chosen);
}

TEST_CASE("EditorSettings loads back what it set, after a restart")
{
	TempAppSettings store;

	{
		Graphite::EditorSettings settings{store.Get()};
		settings.SetFontSize(17.0f);
		settings.SetIndentWithSpaces(kDisabled);
		settings.SetIndentWidth(2);
		settings.SetGutterChangeMarkers(kDisabled);
	}

	Graphite::EditorSettings reloaded{store.Reload()};
	reloaded.Load();

	CHECK(reloaded.GetFontSize() == doctest::Approx(17.0f));
	CHECK_FALSE(reloaded.GetIndentStyle().UseSpaces);
	CHECK(reloaded.GetIndentStyle().Width == 2);
	CHECK_FALSE(reloaded.HasGutterChangeMarkers());
	CHECK(reloaded.HasScrollbarChangeMarkers());
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
		settings.SetHoverInfo(kDisabled);
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

TEST_CASE("The image background is kept in the settings")
{
	TempAppSettings store;

	{
		Graphite::EditorSettings settings{store.Get()};
		CHECK(settings.GetImageBackground() == Graphite::PictureBackground::Checkerboard);

		settings.SetImageBackground(Graphite::PictureBackground::Dark);
		CHECK(settings.GetImageBackground() == Graphite::PictureBackground::Dark);
	}

	Graphite::EditorSettings reloaded{store.Get()};
	reloaded.Load();
	CHECK(reloaded.GetImageBackground() == Graphite::PictureBackground::Dark);

	reloaded.SetImageBackground(Graphite::PictureBackground::Light);
	CHECK(store.Get().GetImageBackground() == "light");
}

TEST_CASE("An unknown image background reads as the checkerboard")
{
	TempAppSettings store;
	store.Get().SetImageBackground("sepia");

	Graphite::EditorSettings settings{store.Get()};
	settings.Load();

	CHECK(settings.GetImageBackground() == Graphite::PictureBackground::Checkerboard);
}

namespace {
	struct ZoomCommands
	{
		TempAppSettings Store;
		Graphite::EditorSettings Settings{Store.Get()};
		Core::CommandRegistry Commands;
		Core::Keymap Keys;
		std::vector<Graphite::PictureZoom> Asked;

		ZoomCommands() { Settings.RegisterCommands(Commands, Keys); }

		void PictureTakes(bool takes)
		{
			Settings.SetPictureZoom([this, takes](Graphite::PictureZoom zoom) {
				Asked.push_back(zoom);
				return takes;
			});
		}
	};
}

TEST_CASE("The zoom commands zoom a picture that takes them and leave the font as it was")
{
	ZoomCommands zoom;
	zoom.PictureTakes(true);
	const float before = zoom.Settings.GetFontSize();

	CHECK(zoom.Commands.Run("editor.zoomIn"));
	CHECK(zoom.Commands.Run("editor.zoomOut"));
	CHECK(zoom.Commands.Run("editor.resetZoom"));

	CHECK(zoom.Asked == std::vector<Graphite::PictureZoom>{Graphite::PictureZoom::In, Graphite::PictureZoom::Out,
														   Graphite::PictureZoom::Fit});
	CHECK(zoom.Settings.GetFontSize() == doctest::Approx(before));
}

TEST_CASE("The zoom commands change the font when no picture takes them")
{
	ZoomCommands zoom;
	const float before = zoom.Settings.GetFontSize();

	SUBCASE("A picture that declines the zoom")
	{
		zoom.PictureTakes(false);
	}

	SUBCASE("No picture asked at all")
	{
	}

	zoom.Commands.Run("editor.zoomIn");
	CHECK(zoom.Settings.GetFontSize() == doctest::Approx(before + 1.0f));

	zoom.Commands.Run("editor.zoomOut");
	zoom.Commands.Run("editor.zoomOut");
	CHECK(zoom.Settings.GetFontSize() == doctest::Approx(before - 1.0f));

	zoom.Commands.Run("editor.resetZoom");
	CHECK(zoom.Settings.GetFontSize() == doctest::Approx(17.0f));
}