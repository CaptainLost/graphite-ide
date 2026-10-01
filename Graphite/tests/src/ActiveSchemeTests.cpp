#include <doctest/doctest.h>

#include "TempAppSettings.h"
#include "TempDirectory.h"
#include "Theme/ActiveScheme.h"
#include "Theme/BuiltInSchemes.h"

#include <fstream>

namespace {

	void WriteFile(const std::filesystem::path& path, std::string_view contents)
	{
		std::ofstream out(path, std::ios::binary);
		out << contents;
	}
}

TEST_CASE("An empty name resolves to the dark built-in without reading anything")
{
	const TempDirectory temp;

	const Graphite::ResolvedScheme resolved = Graphite::ResolveNamed("", temp.GetPath());

	const std::size_t background = static_cast<std::size_t>(Graphite::EditorColor::Background);

	CHECK(resolved.Editor[background].R ==
		  doctest::Approx(Graphite::DarkScheme().Editor[background].R));
}

TEST_CASE("A scheme naming light as its base inherits from light")
{
	const TempDirectory temp;

	WriteFile(temp.GetPath() / "pale.json", R"({"base":"light"})");

	const Graphite::ResolvedScheme resolved = Graphite::ResolveNamed("pale", temp.GetPath());

	const std::size_t background = static_cast<std::size_t>(Graphite::EditorColor::Background);

	CHECK(resolved.Editor[background].R ==
		  doctest::Approx(Graphite::LightScheme().Editor[background].R));
}

// A bare built-in name must short-circuit before the file route. Without that branch, "light"
// would fall through to LoadScheme, find no light.json, and come back as an empty scheme whose
// Base defaults to Dark — so choosing the light built-in would silently give a dark editor.
TEST_CASE("A built-in name resolves to that built-in without reading a file")
{
	const TempDirectory temp;

	const Graphite::ResolvedScheme resolved = Graphite::ResolveNamed("light", temp.GetPath());

	const std::size_t background = static_cast<std::size_t>(Graphite::EditorColor::Background);

	CHECK(resolved.Editor[background].R ==
		  doctest::Approx(Graphite::LightScheme().Editor[background].R));
}

// The version is what tells the layer to re-apply the chrome. Applying every frame would work
// by accident and break the moment anything pushes a style colour around a widget.
TEST_CASE("The active scheme's version moves whenever the colours do")
{
	Graphite::ActiveScheme active;

	const std::uint32_t initial = active.GetVersion();

	active.Set(Graphite::LightScheme());

	const std::uint32_t afterSet = active.GetVersion();
	CHECK(afterSet != initial);

	active.Set(Graphite::DarkScheme());

	CHECK(active.GetVersion() != afterSet);
}

TEST_CASE("The active scheme loads the one the settings name")
{
	TempAppSettings store;
	store.Get().SetColorScheme("light");

	Graphite::ActiveScheme active;
	active.Load(store.Get());

	const std::size_t background = static_cast<std::size_t>(Graphite::EditorColor::Background);

	CHECK(active.Get().Editor[background].R ==
		  doctest::Approx(Graphite::LightScheme().Editor[background].R));
}
