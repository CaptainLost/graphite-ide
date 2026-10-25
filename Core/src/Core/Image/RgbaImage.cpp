#include "corepch.h"

#include "Core/Image/RgbaImage.h"

#include <stb_image_resize2.h>

#include <algorithm>
#include <cmath>

namespace Core {

	namespace {
		constexpr int kChannels = 4;

		int ScaledSide(int side, int longest, int maximumSide)
		{
			const double scaled = static_cast<double>(side) * maximumSide / longest;
			return std::max(1, static_cast<int>(std::lround(scaled)));
		}
	}

	std::size_t RgbaImage::ByteSize() const
	{
		return Pixels.size();
	}

	RgbaImage ScaleToFit(const RgbaImage& image, int maximumSide)
	{
		const int longest = std::max(image.Width, image.Height);
		if (maximumSide <= 0 || longest <= maximumSide || image.Width <= 0 || image.Height <= 0)
			return image;

		RgbaImage scaled;
		scaled.Width = ScaledSide(image.Width, longest, maximumSide);
		scaled.Height = ScaledSide(image.Height, longest, maximumSide);
		scaled.Pixels.resize(static_cast<std::size_t>(scaled.Width) * static_cast<std::size_t>(scaled.Height) * kChannels);

		const unsigned char* resized = stbir_resize_uint8_srgb(image.Pixels.data(), image.Width, image.Height, 0, scaled.Pixels.data(), scaled.Width,
			scaled.Height, 0, STBIR_RGBA);
		if (resized == nullptr)
			return image;
		return scaled;
	}
}
