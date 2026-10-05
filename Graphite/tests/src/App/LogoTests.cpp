#include <doctest/doctest.h>

#include "App/Logo.h"

#include "Core/Platform/IconResource.h"

#include <algorithm>
#include <vector>

namespace {
	// A transparent image with the given columns of one row painted.
	Core::RgbaImage ImageWithColumns(int width, int height, int first, int end)
	{
		Core::RgbaImage image;
		image.Width = width;
		image.Height = height;
		image.Pixels.assign(static_cast<std::size_t>(width) * height * 4, 0);

		for (int x = first; x < end; ++x)
		{
			image.Pixels[(static_cast<std::size_t>(height / 2) * width + x) * 4 + 3] = 1;
		}

		return image;
	}
}

TEST_CASE("OpaqueColumns spans the columns that paint anything")
{
	const Graphite::ColumnSpan span = Graphite::OpaqueColumns(ImageWithColumns(10, 6, 3, 7));

	CHECK(span.First == 3);
	CHECK(span.End == 7);
}

TEST_CASE("OpaqueColumns reaches both edges of an image painted edge to edge")
{
	const Graphite::ColumnSpan span = Graphite::OpaqueColumns(ImageWithColumns(8, 8, 0, 8));

	CHECK(span.First == 0);
	CHECK(span.End == 8);
}

TEST_CASE("OpaqueColumns is empty for a fully transparent image")
{
	const Graphite::ColumnSpan span = Graphite::OpaqueColumns(ImageWithColumns(8, 8, 0, 0));

	CHECK(span.First == 0);
	CHECK(span.End == 0);
}

TEST_CASE("OpaqueColumns is empty for an empty image")
{
	const Graphite::ColumnSpan span = Graphite::OpaqueColumns(Core::RgbaImage{});

	CHECK(span.First == 0);
	CHECK(span.End == 0);
}

#ifdef CORE_PLATFORM_WINDOWS
TEST_CASE("The title bar's mark holds an image for each size it is drawn at, with opaque pixels")
{
	const std::vector<int> sizes = Core::IconResourceSizes(Graphite::kLogoIcon);
	REQUIRE_FALSE(sizes.empty());

	for (const int size : sizes)
	{
		CAPTURE(size);

		const Core::RgbaImage image = Core::LoadIconResource(Graphite::kLogoIcon, size);
		REQUIRE(image.Width == size);
		REQUIRE(image.Height == size);

		// A scaled image has none: the system's scaling loses alpha at every edge.
		bool anyOpaque = false;

		for (std::size_t i = 3; i < image.Pixels.size(); i += 4)
		{
			anyOpaque = anyOpaque || image.Pixels[i] == 255;
		}

		CHECK(anyOpaque);

		// Cropped to the mark, which is narrower than it is tall. Checked on the largest image
		// only: at the smallest the faint edge reaches every column.
		const Graphite::ColumnSpan span = Graphite::OpaqueColumns(image);

		if (size == sizes.back())
		{
			CHECK(span.End - span.First < size);
		}

		CHECK(Graphite::LogoWidth(static_cast<float>(size)) == static_cast<float>(span.End - span.First));
	}
}

TEST_CASE("The title bar's mark is drawn at the nearest size the icon holds")
{
	const std::vector<int> sizes = Core::IconResourceSizes(Graphite::kLogoIcon);
	REQUIRE(std::find(sizes.begin(), sizes.end(), 20) != sizes.end());

	// The caption asks for 19 pixels at 100%, which the icon has no image for.
	const Core::RgbaImage twenty = Core::LoadIconResource(Graphite::kLogoIcon, 20);
	const Graphite::ColumnSpan span = Graphite::OpaqueColumns(twenty);

	CHECK(Graphite::LogoWidth(19.0f) == static_cast<float>(span.End - span.First));
}
#endif
