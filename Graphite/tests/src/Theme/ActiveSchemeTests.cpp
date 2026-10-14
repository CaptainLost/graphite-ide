#include <doctest/doctest.h>

#include "FileFixtures.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"
#include "Theme/ActiveScheme.h"
#include "Theme/BuiltInSchemes.h"

TEST_CASE("An empty name resolves to the dark built-in without reading anything")
{
	const TempDirectory temp;

	const Graphite::ResolvedScheme resolved = Graphite::ResolveNamed("", temp.GetPath());

	const std::size_t backgroundIndex = static_cast<std::size_t>(Graphite::EditorColor::Background);

	CHECK(resolved.Editor[backgroundIndex].R ==
		  doctest::Approx(Graphite::DarkScheme().Editor[backgroundIndex].R));
}

TEST_CASE("A scheme naming light as its base inherits from light")
{
	const TempDirectory temp;

	WriteFileBytes(temp.GetPath() / "pale.json", R"({"base":"light"})");

	const Graphite::ResolvedScheme resolved = Graphite::ResolveNamed("pale", temp.GetPath());

	const std::size_t backgroundIndex = static_cast<std::size_t>(Graphite::EditorColor::Background);

	CHECK(resolved.Editor[backgroundIndex].R ==
		  doctest::Approx(Graphite::LightScheme().Editor[backgroundIndex].R));
}

TEST_CASE("A built-in name resolves to that built-in without reading a file")
{
	const TempDirectory temp;

	const Graphite::ResolvedScheme resolved = Graphite::ResolveNamed("light", temp.GetPath());

	const std::size_t backgroundIndex = static_cast<std::size_t>(Graphite::EditorColor::Background);

	CHECK(resolved.Editor[backgroundIndex].R ==
		  doctest::Approx(Graphite::LightScheme().Editor[backgroundIndex].R));
}

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

	const std::size_t backgroundIndex = static_cast<std::size_t>(Graphite::EditorColor::Background);

	CHECK(active.Get().Editor[backgroundIndex].R ==
		  doctest::Approx(Graphite::LightScheme().Editor[backgroundIndex].R));
}
