#pragma once

#include "Core/Image/ImageFormat.h"
#include "Editor/StatusBar/StatusBar.h"
#include "Image/Zoom.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Graphite {

	struct ImageFacts
	{
		Core::ImageFormat Format{};
		int Width = 0;
		int Height = 0;
		std::uint64_t FileSize = 0;
		std::size_t EntryCount = 1;
		std::size_t FileFrameCount = 1;
		std::size_t ShownFrameCount = 1;
	};

	struct HoveredPixel
	{
		PixelPoint At;
		std::uint8_t R = 0, G = 0, B = 0, A = 0;
	};

	// The hovered pixel, when there is one, then the facts.
	std::vector<ViewStatusItem> ImageStatusItems(const ImageFacts& facts, const std::optional<HoveredPixel>& hovered);
}
