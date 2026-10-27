#pragma once

#include "Core/Base/Result.h"
#include "Core/Image/ImageFormat.h"
#include "Core/Image/RgbaImage.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Core {

	struct DecodedImage
	{
		ImageFormat Format = ImageFormat::Png;
		// The size the file declares; Frames may be smaller when it was scaled to fit MaximumSide.
		int FileWidth = 0;
		int FileHeight = 0;
		std::vector<ImageFrame> Frames;
		// Frames the file holds, which exceeds Frames.size() when the animation was cut to its first frame.
		std::size_t FileFrameCount = 1;
		// Sizes an ico holds; one for every other format.
		std::size_t EntryCount = 1;
	};

	struct DecodeLimits
	{
		std::uint64_t MaximumPixels = 100'000'000;
		std::size_t MaximumAnimationBytes = 256ull << 20;
		int MaximumSide = 16384;
	};

	// A TooLarge error's message is a whole line for the reader; any other's is the reason alone, for the caller to
	// put after its own words, such as "Could not read this image: ".
	Result<DecodedImage> DecodeImage(std::span<const std::uint8_t> bytes, ImageFormat format, const DecodeLimits& limits);

	// Walks the GIF blocks and counts the image descriptors without decoding any pixels; 0 for bytes that are not a GIF.
	std::size_t CountGifFrames(std::span<const std::uint8_t> bytes);
}
