#pragma once

#include "Core/Base/Result.h"
#include "Core/Image/RgbaImage.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Core {

	// Decodes the largest entry of an ico (by area, then by bit depth); entryCount receives how many entries the file holds.
	Result<RgbaImage> DecodeIco(std::span<const std::uint8_t> bytes, std::size_t& entryCount);
}
