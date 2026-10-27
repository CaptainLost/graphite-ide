#include "corepch.h"

#include "Core/Image/ImageFormat.h"

#include "Core/FileSystem/FileSystem.h"

#include <string>

namespace Core {

	std::optional<ImageFormat> ImageFormatOfPath(const std::filesystem::path& path)
	{
		std::string extension = ToUtf8(path.extension());
		for (char& letter : extension)
		{
			if (letter >= 'A' && letter <= 'Z')
			{
				letter = static_cast<char>(letter - 'A' + 'a');
			}
		}

		if (extension == ".png")
		{
			return ImageFormat::Png;
		}
		if (extension == ".jpg" || extension == ".jpeg")
		{
			return ImageFormat::Jpeg;
		}
		if (extension == ".gif")
		{
			return ImageFormat::Gif;
		}
		if (extension == ".bmp")
		{
			return ImageFormat::Bmp;
		}
		if (extension == ".ico")
		{
			return ImageFormat::Ico;
		}
		return std::nullopt;
	}

	std::string_view ImageFormatName(ImageFormat format)
	{
		switch (format)
		{
		case ImageFormat::Png:
			return "PNG";
		case ImageFormat::Jpeg:
			return "JPEG";
		case ImageFormat::Gif:
			return "GIF";
		case ImageFormat::Bmp:
			return "BMP";
		case ImageFormat::Ico:
			return "ICO";
		}
		return "";
	}
}
