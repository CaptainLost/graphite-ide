#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace Core {

	enum class ImageFormat
	{
		Png,
		Jpeg,
		Gif,
		Bmp,
		Ico
	};

	// By extension only, whatever its case; the bytes are not looked at.
	std::optional<ImageFormat> ImageFormatOfPath(const std::filesystem::path& path);

	std::string_view ImageFormatName(ImageFormat format);
}
