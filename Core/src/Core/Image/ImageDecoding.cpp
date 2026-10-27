#include "corepch.h"

#include "Core/Image/ImageDecoding.h"

#include "Core/Image/IcoDecoding.h"

#include <stb_image.h>

#include <limits>
#include <memory>
#include <string>

namespace Core {

	namespace {
		constexpr int kChannels = 4;
		constexpr std::chrono::milliseconds kShortestDelay{20};
		constexpr std::chrono::milliseconds kDefaultDelay{100};

		struct StbFree
		{
			void operator()(void* memory) const { stbi_image_free(memory); }
		};

		using StbBuffer = std::unique_ptr<unsigned char, StbFree>;
		using StbDelays = std::unique_ptr<int, StbFree>;

		Error Unreadable()
		{
			const char* reason = stbi_failure_reason();
			return Error{ErrorCode::InvalidArgument, reason != nullptr ? reason : "unknown format"};
		}

		std::chrono::milliseconds FrameDelay(int milliseconds)
		{
			const std::chrono::milliseconds delay{milliseconds};
			return delay < kShortestDelay ? kDefaultDelay : delay;
		}

		RgbaImage ImageFromPixels(const unsigned char* pixels, int width, int height)
		{
			RgbaImage image;
			image.Width = width;
			image.Height = height;
			const std::size_t size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * kChannels;
			image.Pixels.assign(pixels, pixels + size);
			return image;
		}

		Result<std::vector<ImageFrame>> DecodeStill(const stbi_uc* data, int length)
		{
			int width = 0;
			int height = 0;
			int channels = 0;
			StbBuffer pixels{stbi_load_from_memory(data, length, &width, &height, &channels, kChannels)};
			if (!pixels)
			{
				return Unreadable();
			}

			std::vector<ImageFrame> frames;
			frames.push_back(ImageFrame{ImageFromPixels(pixels.get(), width, height), std::chrono::milliseconds{0}});
			return frames;
		}

		Result<std::vector<ImageFrame>> DecodeAnimation(const stbi_uc* data, int length)
		{
			int* rawDelays = nullptr;
			int width = 0;
			int height = 0;
			int frameCount = 0;
			int channels = 0;
			StbBuffer pixels{stbi_load_gif_from_memory(data, length, &rawDelays, &width, &height, &frameCount, &channels, kChannels)};
			StbDelays delays{rawDelays};
			if (!pixels)
			{
				return Unreadable();
			}

			const std::size_t frameBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * kChannels;
			std::vector<ImageFrame> frames;
			frames.reserve(static_cast<std::size_t>(frameCount));
			for (int index = 0; index < frameCount; ++index)
			{
				ImageFrame frame;
				frame.Image = ImageFromPixels(pixels.get() + static_cast<std::size_t>(index) * frameBytes, width, height);
				frame.Delay = FrameDelay(delays.get()[index]);
				frames.push_back(std::move(frame));
			}
			return frames;
		}

		Result<DecodedImage> DecodeIcoImage(std::span<const std::uint8_t> bytes, const DecodeLimits& limits)
		{
			std::size_t entryCount = 0;
			Result<RgbaImage> decoded = DecodeIco(bytes, entryCount);
			if (!decoded)
			{
				return decoded.GetError();
			}

			RgbaImage entry = std::move(decoded).GetValue();
			const std::uint64_t pixelCount = static_cast<std::uint64_t>(entry.Width) * static_cast<std::uint64_t>(entry.Height);
			if (pixelCount > limits.MaximumPixels)
			{
				return Error{ErrorCode::TooLarge, std::format("Image is too large to preview ({} × {})", entry.Width, entry.Height)};
			}

			DecodedImage image;
			image.Format = ImageFormat::Ico;
			image.FileWidth = entry.Width;
			image.FileHeight = entry.Height;
			image.EntryCount = entryCount;
			image.Frames.push_back(ImageFrame{ScaleToFit(entry, limits.MaximumSide), std::chrono::milliseconds{0}});
			return image;
		}
	}

	std::size_t CountGifFrames(std::span<const std::uint8_t> bytes)
	{
		constexpr std::size_t kHeaderSize = 6;
		constexpr std::size_t kScreenDescriptorSize = 7;
		constexpr std::size_t kImageDescriptorSize = 9;
		constexpr std::uint8_t kExtensionIntroducer = 0x21;
		constexpr std::uint8_t kImageSeparator = 0x2C;
		constexpr std::uint8_t kTrailer = 0x3B;
		constexpr std::uint8_t kHasColourTable = 0x80;
		constexpr std::uint8_t kColourTableSizeMask = 0x07;

		auto colourTableBytes = [](std::uint8_t flags) -> std::size_t {
			if ((flags & kHasColourTable) == 0)
			{
				return 0;
			}
			return std::size_t{3} << ((flags & kColourTableSizeMask) + 1);
		};

		if (bytes.size() < kHeaderSize + kScreenDescriptorSize || bytes[0] != 'G' || bytes[1] != 'I' || bytes[2] != 'F')
		{
			return 0;
		}

		std::size_t position = kHeaderSize + kScreenDescriptorSize + colourTableBytes(bytes[kHeaderSize + 4]);

		auto skipSubBlocks = [&] {
			while (position < bytes.size())
			{
				const std::size_t length = bytes[position];
				position += 1 + length;
				if (length == 0)
				{
					return true;
				}
			}
			return false;
		};

		std::size_t frames = 0;
		while (position < bytes.size())
		{
			const std::uint8_t block = bytes[position++];
			if (block == kTrailer)
			{
				break;
			}
			if (block == kExtensionIntroducer)
			{
				++position;
				if (!skipSubBlocks())
				{
					break;
				}
			}
			else if (block == kImageSeparator)
			{
				if (position + kImageDescriptorSize >= bytes.size())
				{
					break;
				}
				const std::uint8_t flags = bytes[position + kImageDescriptorSize - 1];
				position += kImageDescriptorSize + colourTableBytes(flags);
				++position;
				if (!skipSubBlocks())
				{
					break;
				}
				++frames;
			}
			else
			{
				break;
			}
		}
		return frames;
	}

	Result<DecodedImage> DecodeImage(std::span<const std::uint8_t> bytes, ImageFormat format, const DecodeLimits& limits)
	{
		if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
		{
			return Error{ErrorCode::TooLarge, "Image file is too large to preview"};
		}
		if (format == ImageFormat::Ico)
		{
			return DecodeIcoImage(bytes, limits);
		}

		const stbi_uc* data = bytes.data();
		const int length = static_cast<int>(bytes.size());

		int width = 0;
		int height = 0;
		int channels = 0;
		if (stbi_info_from_memory(data, length, &width, &height, &channels) == 0)
		{
			return Unreadable();
		}
		if (width <= 0 || height <= 0)
		{
			return Error{ErrorCode::InvalidArgument, "it has no pixels"};
		}

		const std::uint64_t pixelCount = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
		if (pixelCount > limits.MaximumPixels)
		{
			return Error{ErrorCode::TooLarge, std::format("Image is too large to preview ({} × {})", width, height)};
		}

		std::size_t fileFrameCount = 1;
		bool animated = false;
		if (format == ImageFormat::Gif)
		{
			fileFrameCount = std::max<std::size_t>(CountGifFrames(bytes), 1);
			const std::uint64_t animationBytes = pixelCount * kChannels * fileFrameCount;
			animated = fileFrameCount > 1 && animationBytes <= limits.MaximumAnimationBytes;
		}

		Result<std::vector<ImageFrame>> decoded = animated ? DecodeAnimation(data, length) : DecodeStill(data, length);
		if (!decoded)
		{
			return decoded.GetError();
		}

		DecodedImage image;
		image.Format = format;
		image.FileWidth = width;
		image.FileHeight = height;
		image.FileFrameCount = fileFrameCount;
		image.Frames = std::move(decoded).GetValue();
		for (ImageFrame& frame : image.Frames)
		{
			frame.Image = ScaleToFit(frame.Image, limits.MaximumSide);
		}
		return image;
	}
}
