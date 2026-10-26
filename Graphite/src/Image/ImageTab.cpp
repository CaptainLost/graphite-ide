#include "Image/ImageTab.h"

#include "Core/App/Redraw.h"
#include "Core/Image/ImageFormat.h"

#include <fstream>
#include <ios>
#include <new>
#include <span>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {
		constexpr std::size_t kReadChunkBytes = 64 * 1024;

		// Read whole and closed before decoding starts, so the file is never held while the tab shows it.
		Core::Result<std::vector<std::uint8_t>> ReadWholeFile(const std::filesystem::path& path)
		{
			std::error_code sizeError;
			const std::uintmax_t size = std::filesystem::file_size(path, sizeError);

			if (sizeError)
			{
				return Core::Error{Core::ErrorCode::IoFailure, sizeError.message()};
			}

			std::ifstream file(path, std::ios::binary);

			if (!file)
			{
				return Core::Error{Core::ErrorCode::AccessDenied, "the file cannot be opened"};
			}

			std::vector<std::uint8_t> bytes;
			bytes.reserve(static_cast<std::size_t>(size));
			std::vector<char> chunk(kReadChunkBytes);

			// Read to the end rather than to the size taken above, because the file can change in between.
			while (file)
			{
				file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
				const auto* begin = reinterpret_cast<const std::uint8_t*>(chunk.data());
				bytes.insert(bytes.end(), begin, begin + file.gcount());
			}

			if (file.bad())
			{
				return Core::Error{Core::ErrorCode::IoFailure, "reading the file failed"};
			}

			return bytes;
		}

		std::string FailureLine(const Core::Error& error)
		{
			if (error.Code == Core::ErrorCode::TooLarge)
			{
				return error.Message;
			}

			return "Could not read this image: " + error.Message;
		}
	}

	ImageTab::ImageTab(std::filesystem::path path, Core::JobSubmitter submit, Core::TextureFactory& textures)
		: m_Path(std::move(path)), m_Submit(std::move(submit)), m_Textures(textures)
	{
	}

	void ImageTab::Load()
	{
		if (m_LoadRunning)
		{
			return;
		}

		m_LoadRunning = true;
		m_NeedsLoad = false;
		m_CheckAfterLoad = false;

		if (m_State == ImageTabState::Failed)
		{
			m_State = ImageTabState::Loading;
			m_Failure.clear();
		}

		Core::DecodeLimits limits;
		limits.MaximumSide = m_Textures.GetMaximumSide();

		m_Submit([path = m_Path, generation = ++m_Generation, limits, landed = m_Landed] {
			landed->Post(ReadAndDecode(path, generation, limits));
		});
	}

	void ImageTab::Update(std::chrono::steady_clock::time_point now, bool shown)
	{
		TakeLanded();

		if (shown && m_NeedsLoad)
		{
			Load();
		}

		Animate(now, shown);
	}

	void ImageTab::CheckDisk()
	{
		// A tab holding nothing reads the file as it is when it next loads.
		if (m_NeedsLoad)
		{
			return;
		}

		if (m_LoadRunning)
		{
			m_CheckAfterLoad = true;
			return;
		}

		const std::optional<DiskStamp> onDisk = ReadStamp(m_Path);

		if (onDisk && onDisk != m_Stamp)
		{
			Load();
		}
	}

	void ImageTab::Release()
	{
		m_Texture.reset();
		m_Frames = {};
		m_FrameIndex = 0;
		m_FrameShownAt.reset();
		m_ReloadFailure.clear();

		++m_Generation;
		m_LoadRunning = false;
		m_CheckAfterLoad = false;

		if (m_State != ImageTabState::Failed)
		{
			m_State = ImageTabState::Loading;
			m_NeedsLoad = true;
		}
	}

	ImageTabState ImageTab::GetState() const
	{
		return m_State;
	}

	const std::string& ImageTab::GetFailure() const
	{
		return m_Failure;
	}

	const std::string& ImageTab::GetReloadFailure() const
	{
		return m_ReloadFailure;
	}

	const Core::ImageTexture* ImageTab::GetTexture()
	{
		if (!m_Texture && !m_Frames.empty())
		{
			m_Texture = m_Textures.Create(m_Frames[m_FrameIndex].Image);
		}

		return m_Texture.get();
	}

	const ImageFacts* ImageTab::GetFacts() const
	{
		return m_Facts ? &*m_Facts : nullptr;
	}

	std::optional<HoveredPixel> ImageTab::PixelAt(PixelPoint at) const
	{
		if (m_Frames.empty() || !m_Facts || at.X < 0 || at.Y < 0 || at.X >= m_Facts->Width || at.Y >= m_Facts->Height)
		{
			return std::nullopt;
		}

		const Core::RgbaImage& image = m_Frames[m_FrameIndex].Image;
		const std::int64_t x = static_cast<std::int64_t>(at.X) * image.Width / m_Facts->Width;
		const std::int64_t y = static_cast<std::int64_t>(at.Y) * image.Height / m_Facts->Height;
		const std::size_t offset = static_cast<std::size_t>((y * image.Width + x) * 4);

		HoveredPixel pixel;
		pixel.At = at;
		pixel.R = image.Pixels[offset];
		pixel.G = image.Pixels[offset + 1];
		pixel.B = image.Pixels[offset + 2];
		pixel.A = image.Pixels[offset + 3];
		return pixel;
	}

	std::uint64_t ImageTab::GetDecodedBytes() const
	{
		std::uint64_t bytes = 0;

		for (const Core::ImageFrame& frame : m_Frames)
		{
			bytes += frame.Image.ByteSize();
		}

		// The texture keeps a whole copy of its frame on the CPU for the renderer to upload.
		if (m_Texture)
		{
			bytes += m_Frames[m_FrameIndex].Image.ByteSize();
		}

		return bytes;
	}

	ZoomState& ImageTab::GetZoom()
	{
		return m_Zoom;
	}

	std::optional<ImageTab::DiskStamp> ImageTab::ReadStamp(const std::filesystem::path& path)
	{
		std::error_code timeError;
		const std::filesystem::file_time_type time = std::filesystem::last_write_time(path, timeError);

		if (timeError)
		{
			return std::nullopt;
		}

		std::error_code sizeError;
		const std::uintmax_t size = std::filesystem::file_size(path, sizeError);

		if (sizeError)
		{
			return std::nullopt;
		}

		return DiskStamp{time, size};
	}

	ImageTab::LoadedPicture ImageTab::ReadAndDecode(const std::filesystem::path& path, std::uint64_t generation,
	                                                const Core::DecodeLimits& limits)
	{
		// Taken before the read, so a change during it shows as a newer stamp at the next check.
		const std::optional<DiskStamp> stamp = ReadStamp(path);

		const std::optional<Core::ImageFormat> format = Core::ImageFormatOfPath(path);

		if (!format)
		{
			return {generation, Core::Error{Core::ErrorCode::Unsupported, "its extension names no image format"}, stamp};
		}

		try
		{
			Core::Result<std::vector<std::uint8_t>> bytes = ReadWholeFile(path);

			if (!bytes)
			{
				return {generation, bytes.GetError(), stamp};
			}

			const std::span<const std::uint8_t> read = bytes.GetValue();
			return {generation, Core::DecodeImage(read, *format, limits), stamp, read.size()};
		}
		catch (const std::bad_alloc&)
		{
			return {generation, Core::Error{Core::ErrorCode::IoFailure, "there is not enough memory to read it"}, stamp};
		}
	}

	void ImageTab::TakeLanded()
	{
		for (LoadedPicture& picture : m_Landed->TakeAll())
		{
			// Only the newest load counts; one asked for before a release or another load lands as nothing.
			if (picture.Generation == m_Generation)
			{
				m_LoadRunning = false;
				Apply(std::move(picture));
			}
		}

		if (m_CheckAfterLoad && !m_LoadRunning)
		{
			m_CheckAfterLoad = false;
			CheckDisk();
		}
	}

	void ImageTab::Apply(LoadedPicture picture)
	{
		m_Stamp = picture.Stamp;

		if (!picture.Decoded)
		{
			const Core::Error& error = picture.Decoded.GetError();

			if (!m_Frames.empty())
			{
				m_ReloadFailure = "Could not read the new version: " + error.Message;
				return;
			}

			// The facts of a released picture would describe one that is no longer shown.
			m_Facts.reset();
			m_Failure = FailureLine(error);
			m_State = ImageTabState::Failed;
			return;
		}

		Core::DecodedImage decoded = std::move(picture.Decoded).GetValue();

		if (!m_Facts || m_Facts->Width != decoded.FileWidth || m_Facts->Height != decoded.FileHeight)
		{
			m_Zoom.Opened = false;
		}

		ImageFacts facts;
		facts.Format = decoded.Format;
		facts.Width = decoded.FileWidth;
		facts.Height = decoded.FileHeight;
		facts.FileSize = picture.FileSize;
		facts.EntryCount = decoded.EntryCount;
		facts.FileFrameCount = decoded.FileFrameCount;
		facts.ShownFrameCount = decoded.Frames.size();
		m_Facts = facts;

		m_Frames = std::move(decoded.Frames);
		m_FrameIndex = 0;
		m_FrameShownAt.reset();
		m_Texture.reset();

		m_Failure.clear();
		m_ReloadFailure.clear();
		m_State = ImageTabState::Ready;
	}

	void ImageTab::Animate(std::chrono::steady_clock::time_point now, bool shown)
	{
		if (m_Frames.size() < 2)
		{
			return;
		}

		// A hidden gif stands still and asks for nothing; shown again, its frame gets its whole delay.
		if (!shown)
		{
			m_FrameShownAt.reset();
			return;
		}

		if (!m_FrameShownAt)
		{
			m_FrameShownAt = now;
		}

		if (now - *m_FrameShownAt >= m_Frames[m_FrameIndex].Delay)
		{
			m_FrameIndex = (m_FrameIndex + 1) % m_Frames.size();
			m_FrameShownAt = now;
			m_Texture.reset();
		}

		Core::Redraw::RequestIn(*m_FrameShownAt + m_Frames[m_FrameIndex].Delay - now);
	}
}
