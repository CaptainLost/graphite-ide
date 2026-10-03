#include <doctest/doctest.h>

#include "Core/Command/MenuItem.h"

TEST_CASE("A section carries the heading it is drawn with")
{
	const Core::MenuItem section = Core::MenuItem::Section("Navigate");

	CHECK(section.GetKind() == Core::MenuItem::Kind::Section);
	CHECK(section.GetText() == "Navigate");
	CHECK(section.GetChildren().empty());
}

TEST_CASE("A section is not a separator, so a renderer can tell the two apart")
{
	CHECK(Core::MenuItem::Separator().GetKind() == Core::MenuItem::Kind::Separator);
	CHECK(Core::MenuItem::Section("Tools").GetKind() != Core::MenuItem::Kind::Separator);
}
