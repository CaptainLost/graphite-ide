#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Core {

	// Straight-alpha RGBA, four bytes a pixel, rows top to bottom.
	struct RgbaImage
	{
		int Width = 0;
		int Height = 0;
		std::vector<std::uint8_t> Pixels;

		std::size_t ByteSize() const;
	};

	struct ImageFrame
	{
		RgbaImage Image;
		std::chrono::milliseconds Delay{0};
	};

	// Brings an image within a side limit, keeping its aspect ratio; one already within it is copied.
	RgbaImage ScaleToFit(const RgbaImage& image, int maximumSide);
}
