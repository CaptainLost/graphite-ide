#include "corepch.h"

#include "Core/Image/IcoDecoding.h"

#include "Core/Image/ImageDecoding.h"

#include <algorithm>
#include <cstring>

namespace Core {

	namespace {
		constexpr std::size_t kDirectorySize = 6;
		constexpr std::size_t kEntrySize = 16;
		constexpr std::size_t kHeaderSize = 40;
		constexpr std::size_t kPaletteEntrySize = 4;
		constexpr std::uint16_t kIconType = 1;
		constexpr int kOpaque = 255;
		constexpr int kFullSize = 256;
		constexpr std::uint8_t kPngSignature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

		struct IcoEntry
		{
			int Width = 0;
			int Height = 0;
			int BitCount = 0;
			std::size_t Size = 0;
			std::size_t Offset = 0;
		};

		Error Unreadable(const char* reason)
		{
			return Error{ErrorCode::InvalidArgument, reason};
		}

		std::uint16_t ReadU16(std::span<const std::uint8_t> bytes, std::size_t at)
		{
			return static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8));
		}

		std::uint32_t ReadU32(std::span<const std::uint8_t> bytes, std::size_t at)
		{
			return static_cast<std::uint32_t>(ReadU16(bytes, at)) | (static_cast<std::uint32_t>(ReadU16(bytes, at + 2)) << 16);
		}

		bool IsPng(std::span<const std::uint8_t> data)
		{
			return data.size() >= sizeof(kPngSignature) && std::memcmp(data.data(), kPngSignature, sizeof(kPngSignature)) == 0;
		}

		std::uint64_t RowBytes(std::uint64_t width, std::uint64_t bitsPerPixel)
		{
			return (width * bitsPerPixel + 31) / 32 * 4;
		}

		// A bitmap entry: a BITMAPINFOHEADER with a doubled height, the colour bitmap bottom-up, then the AND mask.
		Result<RgbaImage> DecodeBitmapEntry(std::span<const std::uint8_t> data)
		{
			if (data.size() < kHeaderSize)
				return Unreadable("a bitmap entry is cut short");

			const std::uint32_t headerSize = ReadU32(data, 0);
			const std::int32_t width = static_cast<std::int32_t>(ReadU32(data, 4));
			const std::int32_t doubledHeight = static_cast<std::int32_t>(ReadU32(data, 8));
			const int bitCount = ReadU16(data, 14);
			const std::uint32_t compression = ReadU32(data, 16);
			const std::uint32_t colourCount = ReadU32(data, 32);

			if (headerSize < kHeaderSize || headerSize > data.size())
				return Unreadable("a bitmap entry has a bad header");
			if (compression != 0)
				return Error{ErrorCode::Unsupported, "Compressed icon bitmaps are not supported"};
			if (bitCount != 1 && bitCount != 4 && bitCount != 8 && bitCount != 24 && bitCount != 32)
				return Error{ErrorCode::Unsupported, std::format("Icons of {} bits per pixel are not supported", bitCount)};
			if (width <= 0 || doubledHeight <= 1)
				return Unreadable("a bitmap entry has no pixels");

			const std::uint64_t height = static_cast<std::uint64_t>(doubledHeight) / 2;
			const std::uint64_t columns = static_cast<std::uint64_t>(width);

			std::uint64_t paletteSize = 0;
			if (bitCount <= 8) {
				const std::uint64_t colours = colourCount != 0 ? colourCount : (std::uint64_t{1} << bitCount);
				paletteSize = std::min<std::uint64_t>(colours, std::uint64_t{1} << bitCount);
			}

			const std::uint64_t paletteBytes = paletteSize * kPaletteEntrySize;
			const std::uint64_t colourStride = RowBytes(columns, static_cast<std::uint64_t>(bitCount));
			const std::uint64_t maskStride = RowBytes(columns, 1);
			const std::uint64_t colourStart = headerSize + paletteBytes;
			const std::uint64_t maskStart = colourStart + colourStride * height;
			if (maskStart + maskStride * height > data.size())
				return Unreadable("a bitmap entry is cut short");

			RgbaImage image;
			image.Width = width;
			image.Height = static_cast<int>(height);
			image.Pixels.assign(static_cast<std::size_t>(columns * height * 4), 0);

			auto maskHides = [&](std::uint64_t row, std::uint64_t column) {
				const std::uint64_t at = maskStart + row * maskStride + column / 8;
				return (data[static_cast<std::size_t>(at)] >> (7 - column % 8) & 1) != 0;
			};

			bool hasAlpha = false;
			if (bitCount == 32) {
				for (std::uint64_t row = 0; row < height && !hasAlpha; ++row)
					for (std::uint64_t column = 0; column < columns && !hasAlpha; ++column)
						hasAlpha = data[static_cast<std::size_t>(colourStart + row * colourStride + column * 4 + 3)] != 0;
			}

			for (std::uint64_t row = 0; row < height; ++row) {
				// Rows are stored bottom-up.
				const std::uint64_t top = height - 1 - row;
				const std::size_t source = static_cast<std::size_t>(colourStart + row * colourStride);
				for (std::uint64_t column = 0; column < columns; ++column) {
					std::uint8_t* out = &image.Pixels[static_cast<std::size_t>((top * columns + column) * 4)];
					std::uint8_t blue = 0;
					std::uint8_t green = 0;
					std::uint8_t red = 0;
					int alpha = kOpaque;
					const std::size_t position = static_cast<std::size_t>(column);

					if (bitCount <= 8) {
						const std::size_t bitOffset = position * static_cast<std::size_t>(bitCount);
						const int shift = 8 - bitCount - static_cast<int>(bitOffset % 8);
						const int index = (data[source + bitOffset / 8] >> shift) & ((1 << bitCount) - 1);
						if (static_cast<std::uint64_t>(index) < paletteSize) {
							const std::size_t entry = static_cast<std::size_t>(headerSize) + static_cast<std::size_t>(index) * kPaletteEntrySize;
							blue = data[entry];
							green = data[entry + 1];
							red = data[entry + 2];
						}
					}
					else {
						const std::size_t pixel = source + position * (bitCount / 8);
						blue = data[pixel];
						green = data[pixel + 1];
						red = data[pixel + 2];
						if (bitCount == 32 && hasAlpha)
							alpha = data[pixel + 3];
					}

					if (bitCount != 32 || !hasAlpha)
						alpha = maskHides(row, column) ? 0 : kOpaque;

					out[0] = red;
					out[1] = green;
					out[2] = blue;
					out[3] = static_cast<std::uint8_t>(alpha);
				}
			}
			return image;
		}
	}

	Result<RgbaImage> DecodeIco(std::span<const std::uint8_t> bytes, std::size_t& entryCount)
	{
		if (bytes.size() < kDirectorySize || ReadU16(bytes, 0) != 0 || ReadU16(bytes, 2) != kIconType)
			return Unreadable("it is not an icon file");

		const std::size_t count = ReadU16(bytes, 4);
		if (count == 0)
			return Unreadable("it holds no images");
		if (bytes.size() < kDirectorySize + count * kEntrySize)
			return Unreadable("its directory is cut short");

		const IcoEntry* chosen = nullptr;
		std::vector<IcoEntry> entries;
		entries.reserve(count);
		for (std::size_t index = 0; index < count; ++index) {
			const std::size_t at = kDirectorySize + index * kEntrySize;
			IcoEntry entry;
			entry.Width = bytes[at] != 0 ? bytes[at] : kFullSize;
			entry.Height = bytes[at + 1] != 0 ? bytes[at + 1] : kFullSize;
			entry.BitCount = ReadU16(bytes, at + 6);
			entry.Size = ReadU32(bytes, at + 8);
			entry.Offset = ReadU32(bytes, at + 12);
			entries.push_back(entry);
		}
		for (const IcoEntry& entry : entries) {
			if (chosen == nullptr) {
				chosen = &entry;
				continue;
			}
			const int area = entry.Width * entry.Height;
			const int chosenArea = chosen->Width * chosen->Height;
			if (area > chosenArea || (area == chosenArea && entry.BitCount > chosen->BitCount))
				chosen = &entry;
		}

		if (chosen->Offset > bytes.size() || chosen->Size > bytes.size() - chosen->Offset)
			return Unreadable("an image lies outside the file");

		entryCount = count;
		const std::span<const std::uint8_t> data = bytes.subspan(chosen->Offset, chosen->Size);
		if (IsPng(data)) {
			Result<DecodedImage> decoded = DecodeImage(data, ImageFormat::Png, DecodeLimits{});
			if (!decoded)
				return decoded.GetError();
			return std::move(decoded).GetValue().Frames.at(0).Image;
		}
		return DecodeBitmapEntry(data);
	}
}
