#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace Core {

	// An image as four bytes a pixel, red, green, blue and alpha, rows top to bottom. Alpha is
	// straight, not premultiplied.
	struct RgbaImage
	{
		int Width = 0;
		int Height = 0;
		std::vector<std::uint8_t> Pixels;

		bool Empty() const { return Pixels.empty(); }
	};

	// Reads the ICON resource `name` out of the running executable at `size` pixels square.
	// The system picks the image in the icon nearest that size and scales it when none
	// matches exactly. Empty when the executable has no such icon, or where the platform has
	// no resources to read.
	//
	// The icon is expected to carry its own alpha, as every 32-bit image does; one that leaves
	// alpha at zero throughout comes back fully transparent.
	RgbaImage LoadIconResource(std::string_view name, int size);

	// The sizes the ICON resource `name` holds images for, ascending, each the side of a
	// square image. Asking LoadIconResource for one of these returns that image untouched;
	// any other size is scaled, and the scaling loses alpha at every edge. Empty when the
	// executable has no such icon, or where the platform has no resources to read.
	std::vector<int> IconResourceSizes(std::string_view name);
}
